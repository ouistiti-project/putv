/*****************************************************************************
 * sink_unix.c
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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>

#include "player.h"
#include "jitter.h"
#include "filter.h"
typedef struct sink_s sink_t;
typedef struct sink_ctx_s sink_ctx_t;
struct sink_ctx_s
{
	player_ctx_t *player;
	const sink_t *ops;
	const char *filepath;
	pthread_t thread;
	pthread_t thread2;
	jitter_t *in;
	state_t state;
	char *out;
	size_t length;
	unsigned int samplerate;
	int samplesize;
	int nchannels;
	pthread_cond_t event;
	pthread_mutex_t mutex;
	int counter;
	int nbclients;
};
#define SINK_CTX
#include "sink.h"
#include "unix_server.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define sink_dbg(...)

static const char *jitter_name = "unix socket";
static sink_ctx_t *sink_init(player_ctx_t *player, const char *filepath)
{
	int count = 2;
	jitter_format_t format = SINK_BITSSTREAM;
	sink_ctx_t *ctx = calloc(1, sizeof(*ctx));

	ctx->ops = sink_unix;
	ctx->filepath = filepath;

	pthread_mutex_init(&ctx->mutex, NULL);
	pthread_cond_init(&ctx->event, NULL);

	unsigned int size;
	jitter_t *jitter = jitter_scattergather_init(jitter_name, 6, size);
	jitter->ctx->frequence = DEFAULT_SAMPLERATE;
	jitter->ctx->thredhold = 2;
	jitter->format = format;
	ctx->in = jitter;
	ctx->out = calloc(1, size);

	ctx->player = player;

	return ctx;
}

static jitter_t *sink_jitter(sink_ctx_t *ctx)
{
	return ctx->in;
}

static int sink_unxiclient(thread_info_t *info)
{
	sink_ctx_t *ctx = (sink_ctx_t *)info->userctx;
	int ret = 1;
	int counter = 0;
	if ( ctx->nbclients == 0)
	{
//		player_state(ctx->player, STATE_PLAY);
	}
	ctx->nbclients++;
	while (ret > 0)
	{
		pthread_mutex_lock(&ctx->mutex);
		while (ctx->counter <= counter)
		{
			pthread_cond_wait(&ctx->event, &ctx->mutex);
		}
		counter = ctx->counter;
dbg("send %d %d %d", ctx->length, ctx->counter, counter);
		if (ctx->length > 0)
			ret = send(info->sock, ctx->out, ctx->length, MSG_NOSIGNAL);
		else
			ret = 1;
		pthread_mutex_unlock(&ctx->mutex);
	}
	ctx->nbclients--;
	if ( ctx->nbclients == 0)
	{
//		player_state(ctx->player, STATE_STOP);
	}
	return ret;
}

static void *sink_thread(void *arg)
{
	sink_ctx_t *ctx = (sink_ctx_t *)arg;
	int run = 1;

	/* start decoding */
	dbg("sink: thread run");
	while (run)
	{
		unsigned char *buff = ctx->in->ops->peer(ctx->in->ctx);
		if (buff == NULL)
		{
			run = 0;
			break;
		}
		pthread_mutex_lock(&ctx->mutex);
		ctx->length = ctx->in->ops->length(ctx->in->ctx);
		memcpy(ctx->out, buff, ctx->length);
		ctx->counter++;
		pthread_mutex_unlock(&ctx->mutex);
		pthread_cond_broadcast(&ctx->event);
		ctx->in->ops->pop(ctx->in->ctx, ctx->length);
	}
	dbg("sink: thread end");
	return NULL;
}

static void *server_thread(void *arg)
{
	sink_ctx_t *ctx = (sink_ctx_t *)arg;
	unixserver_run(sink_unxiclient, ctx, ctx->filepath);
	return NULL;
}

static int sink_run(sink_ctx_t *ctx)
{
	pthread_create(&ctx->thread, NULL, sink_thread, ctx);
	pthread_create(&ctx->thread2, NULL, server_thread, ctx);
	return 0;
}

static void sink_destroy(sink_ctx_t *ctx)
{
	pthread_join(ctx->thread2, NULL);
	pthread_join(ctx->thread, NULL);
	jitter_scattergather_destroy(ctx->in);
	free(ctx->out);
	free(ctx);
}

const sink_t *sink_unix = &(sink_t)
{
	.init = sink_init,
	.jitter = sink_jitter,
	.run = sink_run,
	.destroy = sink_destroy,
};

#ifndef SINK_GET
#define SINK_GET
const sink_t *sink_get(sink_ctx_t *ctx)
{
	return ctx->ops;
}
#endif
