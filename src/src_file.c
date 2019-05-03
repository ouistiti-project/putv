/*****************************************************************************
 * src_file.c
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pwd.h>

#include "player.h"
#include "event.h"
typedef struct src_ops_s src_ops_t;
typedef struct src_ctx_s src_ctx_t;
struct src_ctx_s
{
	const src_ops_t *ops;
	int fd;
	player_ctx_t *ctx;
	const char *mime;
	jitter_t *out;
	decoder_t *estream;
	event_listener_t *listener;
};
#define SRC_CTX
#include "src.h"
#include "media.h"
#include "jitter.h"
#include "decoder.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define src_dbg(...)

static int src_read(src_ctx_t *ctx, unsigned char *buff, int len)
{
	int ret = 0;
	if (player_waiton(ctx->ctx, STATE_PAUSE) < 0)
	{
		return 0;
	}
	fd_set rfds;
	int maxfd = ctx->fd;
	FD_ZERO(&rfds);
	FD_SET(ctx->fd, &rfds);
	struct timeval timeout = {1,0};
	ret = select(maxfd + 1, &rfds, NULL, NULL, &timeout);
	if (ret > 0 && FD_ISSET(ctx->fd,&rfds))
		ret = read(ctx->fd, buff, len);
	else if (ret == 0)
	{
		warn("src: timeout");
	}
	src_dbg("src: read %d", ret);
	if (ret < 0)
		err("src file %d error: %s", ctx->fd, strerror(errno));
	if (ret == 0)
	{
		ctx->out->ops->flush(ctx->out->ctx);
		dbg("src: end of file");
	}
	return ret;
}

static src_ctx_t *src_init(player_ctx_t *ctx, const char *url, const char *mime)
{
	int fd = -1;
	char *path = NULL;
	if (!strcmp(url, "-"))
		fd = 0;
	else
	{
		char *protocol = NULL;
		char *path = NULL;
		char *value = utils_parseurl(url, &protocol, NULL, NULL, &path, NULL);
		if (protocol && strcmp(protocol, "file") != 0)
		{
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
			fd = open(path, O_RDONLY);
		}
		free(value);
	}
	if (fd >= 0)
	{
		src_ctx_t *src = calloc(1, sizeof(*src));
		src->ops = src_file;
		src->fd = fd;
		src->ctx = ctx;
		src->mime = mime;
		return src;
	}
	if (path != NULL)
		err("src file %s error: %s", path, strerror(errno));
	else
		err("src file %s error: %s", url, strerror(errno));
	return NULL;
}

static int src_run(src_ctx_t *ctx)
{
	const event_new_es_t event = {.pid = 0, .mime = ctx->mime};
	event_listener_t *listener = ctx->listener;
	while (listener)
	{
		listener->cb(ctx->listener->arg, SRC_EVENT_NEW_ES, (void *)&event);
		listener = listener->next;
	}
	return 0;
}

static void src_eventlistener(src_ctx_t *ctx, event_listener_cb_t cb, void *arg)
{
	event_listener_t *listener = calloc(1, sizeof(*listener));
	listener->cb = cb;
	listener->arg = arg;
	if (ctx->listener == NULL)
		ctx->listener = listener;
	else
	{
		/**
		 * add listener to the end of the list. this allow to call
		 * a new listener with the current event when the function is
		 * called from a callback
		 */
		event_listener_t *previous = ctx->listener;
		while (previous->next != NULL) previous = previous->next;
		previous->next = listener;
	}
}

static int src_attach(src_ctx_t *ctx, int index, decoder_t *decoder)
{
	if (index > 0)
		return -1;
	ctx->estream = decoder;
	ctx->out = ctx->estream->ops->jitter(ctx->estream->ctx);
	dbg("src: add producter to %s", ctx->out->ctx->name);
	ctx->out->ctx->produce = (produce_t)src_read;
	ctx->out->ctx->producter = (void *)ctx;
}

static decoder_t *src_estream(src_ctx_t *ctx, int index)
{
	return ctx->estream;
}

static void src_destroy(src_ctx_t *ctx)
{
	if (ctx->estream != NULL)
		ctx->estream->ops->destroy(ctx->estream->ctx);
	event_listener_t *listener = ctx->listener;
	while (listener)
	{
		event_listener_t *next = listener->next;
		free(listener);
		listener = next;
	}
	close(ctx->fd);
	free(ctx);
}

const src_ops_t *src_file = &(src_ops_t)
{
	.protocol = "file://",
	.init = src_init,
	.run = src_run,
	.eventlistener = src_eventlistener,
	.attach = src_attach,
	.estream = src_estream,
	.destroy = src_destroy,
	.mime = NULL,
};
