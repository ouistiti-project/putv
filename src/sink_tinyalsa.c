/*****************************************************************************
 * sink_alsa.c
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
#include <tinyalsa/asoundlib.h>

#include "putv.h"
#include "jitter.h"
typedef struct sink_s sink_t;
typedef struct sink_ctx_s sink_ctx_t;
struct sink_ctx_s
{
	const sink_t *ops;
	struct pcm *playback_handle;
	pthread_t thread;
	jitter_t *in;
	state_t state;
};
#define SINK_CTX
#include "sink.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

static const char *jitter_name = "tinyalsa";
static sink_ctx_t *alsa_init(mediaplayer_ctx_t *mctx, const char *soundcard)
{
	int ret;
	jitter_format_t format = PCM_24bits_LE_stereo;
	sink_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->ops = sink_tinyalsa;

	size_t size = 128;
	struct pcm_config config =
	{
		.channels = 2,
		.rate = 44100,
		.period_size = 1024,
		.period_count = 4,
		.format = PCM_FORMAT_S24_LE,
	};
	ctx->playback_handle = pcm_open(0, 0, PCM_OUT, &config);

	jitter_t *jitter = jitter_scattergather_init(jitter_name, 10, size);
	ctx->in = jitter;
	jitter->format = format;

	return ctx;
error:
	err("sink: init error %s", strerror(errno));
	free(ctx);
	return NULL;
}

static jitter_t *alsa_jitter(sink_ctx_t *ctx)
{
	return ctx->in;
}

static void *alsa_thread(void *arg)
{
	int ret;
	sink_ctx_t *ctx = (sink_ctx_t *)arg;
	/* start decoding */
	while (ctx->state != STATE_ERROR)
	{
		unsigned char *buff = ctx->in->ops->peer(ctx->in->ctx);
		dbg("sink: play %d", ctx->in->ctx->size);
		ret = pcm_write(ctx->playback_handle, buff, ctx->in->ctx->size);
		ctx->in->ops->pop(ctx->in->ctx, ret);
		if (ret < 0)
			ctx->state = STATE_ERROR;
		else
		{
			dbg("sink: play %d", ret);
		}
	}
	return NULL;
}

static int alsa_run(sink_ctx_t *ctx)
{
	pthread_create(&ctx->thread, NULL, alsa_thread, ctx);
	return 0;
}

static void alsa_destroy(sink_ctx_t *ctx)
{
	pcm_close(ctx->playback_handle);
	jitter_scattergather_destroy(ctx->in);
	free(ctx);
}

const sink_t *sink_tinyalsa = &(sink_t)
{
	.init = alsa_init,
	.jitter = alsa_jitter,
	.run = alsa_run,
	.destroy = alsa_destroy,
};

#ifndef SINK_GET
#define SINK_GET
const sink_t *sink_get(sink_ctx_t *ctx)
{
	return ctx->ops;
}
#endif
