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
#include <alsa/asoundlib.h>

#include "putv.h"
#include "jitter.h"
typedef struct sink_s sink_t;
typedef struct sink_ctx_s sink_ctx_t;
struct sink_ctx_s
{
	mediaplayer_ctx_t *ctx;
	const sink_t *ops;
	snd_pcm_t *playback_handle;
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

#define sink_dbg(...)

static const char *jitter_name = "alsa";
static sink_ctx_t *alsa_init(mediaplayer_ctx_t *mctx, const char *soundcard)
{
	int ret;
	jitter_format_t format = SINK_ALSA_FORMAT;
	int nchannels = 2;
	int count = 2;
	sink_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->ops = sink_alsa;
	ret = snd_pcm_open(&ctx->playback_handle, soundcard, SND_PCM_STREAM_PLAYBACK, 0);

	snd_pcm_hw_params_t *hw_params;
	ret = snd_pcm_hw_params_malloc(&hw_params);
	if (ret < 0) {
		err("sink: malloc");
		goto error;
	}

	ret = snd_pcm_hw_params_any(ctx->playback_handle, hw_params);
	if (ret < 0) {
		err("sink: get params");
		goto error;
	}
	ret = snd_pcm_hw_params_set_access(ctx->playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0) {
		err("sink: access");
		goto error;
	}
	snd_pcm_format_t pcm_format;
	switch (format)
	{
		case PCM_32bits_LE_stereo:
			pcm_format = SND_PCM_FORMAT_S32_LE;
			count = 4;
		break;
		case PCM_24bits_LE_stereo:
			pcm_format = SND_PCM_FORMAT_S24_LE;
			count = 3;
		break;
		case PCM_16bits_LE_stereo:
			pcm_format = SND_PCM_FORMAT_S16_LE;
			count = 2;
		break;
		case PCM_16bits_LE_mono:
			pcm_format = SND_PCM_FORMAT_S16_LE;
			nchannels = 1;
			count = 2;
		break;
	}
	ret = snd_pcm_hw_params_set_format(ctx->playback_handle, hw_params, pcm_format);
	if (ret < 0) {
		err("sink: format");
		goto error;
	}
	unsigned int rate = 44100;
	ret = snd_pcm_hw_params_set_rate_near(ctx->playback_handle, hw_params, &rate, NULL);
	if (ret < 0) {
		err("sink: rate");
		goto error;
	}
	ret = snd_pcm_hw_params_set_channels(ctx->playback_handle, hw_params, nchannels);
	if (ret < 0) {
		err("sink: channels");
		goto error;
	}

	ret = snd_pcm_hw_params(ctx->playback_handle, hw_params);
	if (ret < 0) {
		err("sink: set params");
		goto error;
	}

	snd_pcm_uframes_t size;
	snd_pcm_hw_params_get_period_size(hw_params, &size, 0);
	snd_pcm_hw_params_free(hw_params);

	ret = snd_pcm_prepare(ctx->playback_handle);
	if (ret < 0) {
		err("sink: prepare");
		goto error;
	}

	jitter_t *jitter = jitter_scattergather_init(jitter_name, 10, size * count);
	ctx->in = jitter;
	jitter->format = format;
	jitter->ctx->thredhold = 5;

	ctx->ctx = mctx;

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
	int divider;
	sink_ctx_t *ctx = (sink_ctx_t *)arg;
	switch (ctx->in->format)
	{
		case PCM_32bits_LE_stereo:
			divider = 8;
		break;
		case PCM_16bits_LE_stereo:
			divider = 4;
		break;
		case PCM_16bits_LE_mono:
			divider = 2;
		break;
	}
	/* start decoding */
	while (ctx->state != STATE_ERROR)
	{
		if (player_waiton(ctx->ctx, STATE_PAUSE) < 0)
		{
			snd_pcm_prepare(ctx->playback_handle);
		}

		unsigned char *buff = ctx->in->ops->peer(ctx->in->ctx);
		ret = snd_pcm_writei(ctx->playback_handle, buff, ctx->in->ctx->size / divider);
		ctx->in->ops->pop(ctx->in->ctx, ret * divider);
		if (ret == -EPIPE)
		{
			warn("pcm recover");
			ret = snd_pcm_recover(ctx->playback_handle, ret, 0);
		}
		if (ret < 0)
		{
			ctx->state = STATE_ERROR;
			err("sink: error write pcm");
		}
		else
		{
			sink_dbg("sink: play %d", ret);
		}
	}
	dbg("sink: thread end");
	return NULL;
}

static int alsa_run(sink_ctx_t *ctx)
{
	pthread_create(&ctx->thread, NULL, alsa_thread, ctx);
	return 0;
}

static void alsa_destroy(sink_ctx_t *ctx)
{
	pthread_join(ctx->thread, NULL);
	snd_pcm_close(ctx->playback_handle);
	jitter_scattergather_destroy(ctx->in);
	free(ctx);
}

const sink_t *sink_alsa = &(sink_t)
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
