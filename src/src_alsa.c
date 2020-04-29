/*****************************************************************************
 * src_alsa.c
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

#include "player.h"
#include "jitter.h"
#include "filter.h"
#include "event.h"
typedef struct src_s src_t;
typedef struct src_ops_s src_ops_t;
typedef struct src_ctx_s src_ctx_t;
struct src_ctx_s
{
	player_ctx_t *player;
	const char *soundcard;
	snd_pcm_t *handle;
	pthread_t thread;
	jitter_t *out;
	state_t state;
	unsigned int samplerate;
	int samplesize;
	int nchannels;
	filter_t filter;
	decoder_t *estream;
	event_listener_t *listener;
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

static int _pcm_open(src_ctx_t *ctx, snd_pcm_format_t pcm_format, unsigned int rate, unsigned long *size)
{
	int ret;
	int dir;

	snd_pcm_hw_params_t *hw_params;
	ret = snd_pcm_hw_params_malloc(&hw_params);
	if (ret < 0)
	{
		err("src: malloc");
		goto error;
	}

	ret = snd_pcm_hw_params_any(ctx->handle, hw_params);
	if (ret < 0)
	{
		err("src: get params");
		goto error;
	}
	//int resample = 1;
	//ret = snd_pcm_hw_params_set_rate_resample(handle, params, resample);
	ret = snd_pcm_hw_params_set_access(ctx->handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0)
	{
		err("src: access");
		goto error;
	}

	ret = snd_pcm_hw_params_set_format(ctx->handle, hw_params, pcm_format);
	if (ret < 0)
	{
		err("src: format");
		goto error;
	}
	dir=0;
	ret = snd_pcm_hw_params_set_rate_near(ctx->handle, hw_params, &rate, &dir);
	if (ret < 0)
	{
		err("src: rate");
		goto error;
	}
	ret = snd_pcm_hw_params_set_channels(ctx->handle, hw_params, ctx->nchannels);
	if (ret < 0)
	{
		err("src: channels %d", ctx->nchannels);
		goto error;
	}

	if (size && *size > 0)
	{
		dir = 0;
		//snd_pcm_hw_params_set_buffer_size_near(ctx->handle, hw_params, size);
		snd_pcm_hw_params_set_period_size_near(ctx->handle, hw_params, size, &dir);
	}

	ret = snd_pcm_hw_params(ctx->handle, hw_params);
	if (ret < 0)
	{
		err("src: set params");
		goto error;
	}

	snd_pcm_uframes_t buffer_size;
	snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
	snd_pcm_uframes_t periodsize;
	snd_pcm_hw_params_get_period_size(hw_params, &periodsize, 0);
	src_dbg("src alsa config :");
	src_dbg("\tbuffer size %lu", buffer_size);
	src_dbg("\tperiod size %lu", periodsize);
	src_dbg("\tsample rate %u", rate);
	src_dbg("\tsample size %d", ctx->samplesize);
	src_dbg("\tnchannels %u", ctx->nchannels);
	if (size)
		*size = periodsize;

	ret = snd_pcm_prepare(ctx->handle);
	if (ret < 0) {
		err("src: prepare");
		goto error;
	}
	ctx->samplerate = rate;

error:
	snd_pcm_hw_params_free(hw_params);
	return ret;
}

static int _pcm_close(src_ctx_t *ctx)
{
	snd_pcm_drain(ctx->handle);
	snd_pcm_close(ctx->handle);
	return 0;
}

static const char *jitter_name = "alsa";
static src_ctx_t *src_init(player_ctx_t *player, const char *url, const char *mime)
{
	int count = 2;
	const char *soundcard;
	src_ctx_t *ctx = NULL;

	if (strstr(url, "://") != NULL)
	{
		soundcard = strstr(url, "pcm://");
		if (soundcard == NULL)
			return NULL;
		soundcard += 6;
	}
	else
	{
		soundcard = url;
	}

	int ret;
	snd_pcm_t *handle;
	ret = snd_pcm_open(&handle, soundcard, SND_PCM_STREAM_CAPTURE, 0);

	if (ret == 0)
	{
		ctx = calloc(1, sizeof(*ctx));
		ctx->soundcard = soundcard;
		ctx->player = player;
		ctx->handle = handle;
		dbg("src: %s", src_alsa->name);
	}
	return ctx;
}

static void *src_thread(void *arg)
{
	int ret;
	src_ctx_t *ctx = (src_ctx_t *)arg;
	snd_pcm_format_t pcm_format;
	switch (ctx->out->format)
	{
		case PCM_32bits_LE_stereo:
			pcm_format = SND_PCM_FORMAT_S32_LE;
			ctx->samplesize = 4;
			ctx->nchannels = 2;
		break;
		case PCM_24bits4_LE_stereo:
			pcm_format = SND_PCM_FORMAT_S24_LE;
			ctx->samplesize = 4;
			ctx->nchannels = 2;
		break;
		case PCM_24bits3_LE_stereo:
			pcm_format = SND_PCM_FORMAT_S24_LE;
			ctx->samplesize = 3;
			ctx->nchannels = 2;
		break;
		case PCM_16bits_LE_stereo:
			pcm_format = SND_PCM_FORMAT_S16_LE;
			ctx->samplesize = 2;
			ctx->nchannels = 2;
		break;
		case PCM_16bits_LE_mono:
			pcm_format = SND_PCM_FORMAT_S16_LE;
			ctx->samplesize = 2;
			ctx->nchannels = 1;
		break;
		default:
			dbg("src alsa: format error %d",  ctx->out->format);
	}
	if (ctx->out->ctx->frequence == 0)
		ctx->out->ctx->frequence = 48000;

	int divider = ctx->samplesize * ctx->nchannels;

	unsigned long size = ctx->out->ctx->size / divider;
	if (_pcm_open(ctx, pcm_format, ctx->out->ctx->frequence, &size) < 0)
	{
		err("src: pcm error %s", strerror(errno));
		return NULL;
	}

	snd_pcm_start(ctx->handle);
	/* start decoding */
	unsigned char *buff = NULL;
	while (ctx->state != STATE_ERROR)
	{
		if (player_waiton(ctx->player, STATE_PAUSE) < 0)
		{
			if (player_state(ctx->player, STATE_UNKNOWN) == STATE_ERROR)
			{
				snd_pcm_drain(ctx->handle);
				ctx->state = STATE_ERROR;
				continue;
			}
		}

		ret = 0;
		if (buff == NULL)
		{
			buff = ctx->out->ops->pull(ctx->out->ctx);
			/**
			 * the pipe is broken. close the src and the decoder
			 */
			if (buff == NULL)
				break;
		}
		unsigned char *buff2 = NULL;

		while ((ret = snd_pcm_avail_update (ctx->handle)) < size)
		{
			if (ret >= 0 && snd_pcm_state(ctx->handle) == SND_PCM_STATE_XRUN)
				ret=-EPIPE;

			if (ret < 0)
				break;
			ret = snd_pcm_wait (ctx->handle, 1000);
		}
		if (ret > 0)
		{
			if (ret > size)
				ret = size;
			buff2 = malloc(ret * divider);
#ifdef LBENDIAN
			ret = snd_pcm_readi(ctx->handle, buff2, ret);
#else
			src_dbg("buff %lu %u", ctx->out->ctx->size, ret * 4);
			ret = snd_pcm_readi(ctx->handle, buff, ret);
#endif
		}
		if (ret == -EPIPE)
		{
			warn("pcm recover");
			ret = snd_pcm_recover(ctx->handle, ret, 0);
		}
		else if (ret < 0)
		{
			ctx->state = STATE_ERROR;
			err("src: error write pcm %d", ret);
		}
		else if (ret > 0)
		{
#ifdef LBENDIAN
			int i;
			for (i = 0; i < ret; i++)
			{
				buff[i*2] = buff2[(i*2) + 1];
				buff[(i*2) + 1] = buff2[i*2];
			}
#endif
			ctx->out->ops->push(ctx->out->ctx, ret * divider, NULL);
			buff = NULL;
		}
		free(buff2);
	}
	dbg("src: thread end");
	player_next(ctx->player);
	return NULL;
}

static int src_run(src_ctx_t *ctx)
{
	const event_new_es_t event = {.pid = 0, .mime = mime_audiopcm, .jitte = JITTE_LOW};
	event_listener_t *listener = ctx->listener;
	while (listener)
	{
		listener->cb(listener->arg, SRC_EVENT_NEW_ES, (void *)&event);
		listener = listener->next;
	}
	pthread_create(&ctx->thread, NULL, src_thread, ctx);
	return 0;
}

static const char *src_mime(src_ctx_t *ctx, int index)
{
	if (index > 0)
		return NULL;
	return mime_audiopcm;
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
	ctx->out = ctx->estream->ops->jitter(ctx->estream->ctx, JITTE_LOW);
}

static decoder_t *src_estream(src_ctx_t *ctx, int index)
{
	return ctx->estream;
}

static void src_destroy(src_ctx_t *ctx)
{
	if (ctx->estream != NULL)
		ctx->estream->ops->destroy(ctx->estream->ctx);
	pthread_join(ctx->thread, NULL);
	ctx->filter.ops->destroy(ctx->filter.ctx);
	event_listener_t *listener = ctx->listener;
	while (listener)
	{
		event_listener_t *next = listener->next;
		free(listener);
		listener = next;
	}
	_pcm_close(ctx);
	free(ctx);
}

const src_ops_t *src_alsa = &(src_ops_t)
{
	.name = "alsa",
	.protocol = "pcm://",
	.init = src_init,
	.run = src_run,
	.eventlistener = src_eventlistener,
	.attach = src_attach,
	.estream = src_estream,
	.destroy = src_destroy,
	.mime = src_mime,
};
