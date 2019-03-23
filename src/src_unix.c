/*****************************************************************************
 * src_unix.c
 * this file is part of https://github.com/ouistiti-project/putv
 *****************************************************************************
 * Copyright (C) 2016-2017
 *
 * Authors: Marc Chalain <marc.chalain@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <pwd.h>

#include "player.h"
#include "jitter.h"
typedef enum event_e event_t;
typedef void (*src_listener_t)(void *arg, event_t event, void *data);
typedef struct src_ops_s src_ops_t;
typedef struct src_ctx_s src_ctx_t;
struct src_ctx_s
{
	player_ctx_t *player;
	const char *mime;
	int handle;
	state_t state;
	pthread_t thread;
	jitter_t *out;
	unsigned int samplerate;
	decoder_t *estream;
	struct
	{
		src_listener_t cb;
		void *arg;
	} listener;
};
#define SRC_CTX
#include "src.h"
#include "media.h"
#include "decoder.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define src_dbg(...)

static const char *jitter_name = "unix socket";
static src_ctx_t *src_init(player_ctx_t *player, const char *url, const char *mime)
{
	int count = 2;
	int ret;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;

	char *protocol = NULL;
	char *path = NULL;
	char *value = utils_parseurl(url, &protocol, NULL, NULL, &path, NULL);
	if (protocol && strcmp(protocol, "unix") != 0)
	{
		free(value);
		return NULL;
	}
	if (path != NULL)
	{
		if (path[0] == '~')
		{
			struct passwd *pw = NULL;
			pw = getpwuid(geteuid());
			chdir(pw->pw_dir);
			path++;
			if (path[0] == '/')
				path++;
		}
		struct stat filestat;
		ret = stat(path, &filestat);
		if (ret != 0 || !S_ISSOCK(filestat.st_mode))
		{
			err("error: %s is not a socket", path);
			free(value);
			return NULL;
		}
		strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	}
	else
	{
		free(value);
		return NULL;
	}
	free(value);



	int handle = socket(AF_UNIX, SOCK_STREAM, 0);
	if (handle == 0)
	{
		err("connection error on socket %s", path);
		return NULL;
	}

	ret = connect(handle, (struct sockaddr *) &addr, sizeof(addr));
	if (ret < 0)
	{
		err("connection error on socket %s", path);
		close(handle);
		return NULL;
	}

	src_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->player = player;
	ctx->handle = handle;
	ctx->mime = mime;
	if (ctx->mime == mime_octetstream)
	{
#ifdef DECODER_LAME
		ctx->mime = mime_audiomp3;
#elif defined(DECODER_FLAC)
		ctx->mime = mime_audioflac;
#endif
	}
	return ctx;
}

static void *src_thread(void *arg)
{
	src_ctx_t *ctx = (src_ctx_t *)arg;
	int ret;
	while (ctx->state != STATE_ERROR)
	{
		if (player_waiton(ctx->player, STATE_PAUSE) < 0)
		{
			if (player_state(ctx->player, STATE_UNKNOWN) == STATE_ERROR)
			{
				ctx->state = STATE_ERROR;
				continue;
			}
		}

		unsigned char *buff = ctx->out->ops->pull(ctx->out->ctx);
		ret = recv(ctx->handle, buff, ctx->out->ctx->size, MSG_NOSIGNAL);
		if (ret == -EPIPE)
		{
		}
		if (ret < 0)
		{
			ctx->state = STATE_ERROR;
			err("sink: error write pcm %d", ret);
		}
		else if (ret == 0)
		{
			ctx->out->ops->push(ctx->out->ctx, 0, NULL);
			ctx->out->ops->flush(ctx->out->ctx);
			break;
		}
		else
		{
			src_dbg("sink: play %d", ret);
			ctx->out->ops->push(ctx->out->ctx, ret, NULL);
		}
	}
	dbg("sink: thread end");
	return NULL;
}

static int src_run(src_ctx_t *ctx, jitter_t *encoder)
{
	if (ctx->estream)
	{
		ctx->estream->ops->run(ctx->estream->ctx, encoder);
		ctx->out = ctx->estream->ops->jitter(ctx->estream->ctx);
	}
	pthread_create(&ctx->thread, NULL, src_thread, ctx);
	return 0;
}

static const char *src_mime(src_ctx_t *ctx, int index)
{
	if (index > 0)
		return NULL;
	return ctx->mime;
}

static void src_eventlistener(src_ctx_t *ctx, src_listener_t listener, void *arg)
{
	ctx->listener.cb = listener;
	ctx->listener.arg = arg;
	const event_new_es_t event = {.pid = 0, .mime = ctx->mime};
	ctx->listener.cb(ctx->listener.arg, SRC_EVENT_NEW_ES, &event);
}

static int src_attach(src_ctx_t *ctx, int index, decoder_t *decoder)
{
	if (index > 0)
		return -1;
	ctx->estream = decoder;
}

static decoder_t *src_estream(src_ctx_t *ctx, int index)
{
	return ctx->estream;
}

static void src_destroy(src_ctx_t *ctx)
{
	if (ctx->estream != NULL)
		ctx->estream->ops->destroy(ctx->estream->ctx);
	if (ctx->thread)
		pthread_join(ctx->thread, NULL);
	close(ctx->handle);
	free(ctx);
}

const src_ops_t *src_unix = &(src_ops_t)
{
	.protocol = "unix://|file://",
	.init = src_init,
	.run = src_run,
	.mime = src_mime,
	.eventlistener = src_eventlistener,
	.attach = src_attach,
	.estream = src_estream,
	.destroy = src_destroy,
};
