/*****************************************************************************
 * decoder_opus.c
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
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>

#include <opus/opus.h>

#include "player.h"
typedef struct decoder_s decoder_t;
typedef struct decoder_ctx_s decoder_ctx_t;
struct decoder_ctx_s
{
	const decoder_t *ops;
	OpusDecoder *decoder;
	int nchannels;
	int samplerate;
	pthread_t thread;
	jitter_t *in;
	unsigned char *inbuffer;
	jitter_t *out;
	unsigned char *outbuffer;
};
#define DECODER_CTX
#include "decoder.h"
#include "jitter.h"
#include "filter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define decoder_dbg(...)

#define BUFFERSIZE 1500

#define NBUFFER 3

static const char *jitter_name = "opus decoder";
static decoder_ctx_t *decoder_init(player_ctx_t *player)
{
	decoder_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->ops = decoder_opus;
	ctx->nchannels = 2;
	ctx->samplerate = DEFAULT_SAMPLERATE;

	int err;
	ctx->decoder = opus_decoder_create(ctx->samplerate, ctx->nchannels, &err);
	if (err != OPUS_OK)
	{
		err("opus decoder: open error %s", opus_strerror(err));
		err("opus decoder: open samplerate: %d nchannels %d",ctx->samplerate, ctx->nchannels);
		free(ctx);
		return NULL;
	}

	jitter_t *jitter = jitter_ringbuffer_init(jitter_name, NBUFFER, BUFFERSIZE);
	ctx->in = jitter;
	jitter->format = OPUS;

	return ctx;
}

static jitter_t *decoder_jitter(decoder_ctx_t *decoder)
{
	return decoder->in;
}

static void *opus_thread(void *arg)
{
	int result = 0;
	decoder_ctx_t *ctx = (decoder_ctx_t *)arg;
	int run = 1;
	opus_decoder_ctl(ctx->decoder, OPUS_RESET_STATE);
	while (run)
	{
dbg("decoder opus: %s 1", __FUNCTION__);
		unsigned char *inbuffer = ctx->in->ops->peer(ctx->in->ctx);
		if (inbuffer == NULL)
		{
			dbg("opus decoder: end of stream");
			run = 0;
			continue;
		}
dbg("decoder opus: %s 2", __FUNCTION__);
		if (ctx->outbuffer = NULL)
			ctx->outbuffer = ctx->out->ops->pull(ctx->out->ctx);
dbg("decoder opus: %s 3", __FUNCTION__);
		int inlength = ctx->in->ops->length(ctx->in->ctx);
		int outlength = ctx->out->ctx->size / (sizeof(opus_int16) * ctx->nchannels);
		//opus_decoder_ctl(ctx->decoder, OPUS_GET_LAST_PACKET_DURATION(&outlength));
		dbg("opus decoder in length %d out length %d", inlength, outlength);
		int ret = opus_decode(ctx->decoder, inbuffer, inlength, (opus_int16 *)ctx->outbuffer, outlength, 0);
dbg("decoder opus: %s 4", __FUNCTION__);
		if (ret > 0)
		{
			ctx->out->ops->push(ctx->out->ctx, ret, NULL);
			ctx->outbuffer = NULL;
dbg("decoder opus: %s 5", __FUNCTION__);
		}
		else
		{
			err("opus decoder : decode error (%d) %s", ret, opus_strerror(ret));
			run = 0;
		}
dbg("decoder opus: %s 6", __FUNCTION__);
		ctx->in->ops->pop(ctx->in->ctx, inlength);
dbg("decoder opus: %s 7", __FUNCTION__);
	}

	return (void *)result;
}

static int decoder_run(decoder_ctx_t *decoder, jitter_t *jitter)
{
	decoder->out = jitter;
	if (decoder->out->format > PCM_32bits_BE_stereo)
	{
		err("decoder out format not supported %d", decoder->out->format);
		return -1;
	}
	pthread_create(&decoder->thread, NULL, opus_thread, decoder);
	return 0;
}

static void decoder_destroy(decoder_ctx_t *ctx)
{
	pthread_join(ctx->thread, NULL);
	/* release the decoder */
	opus_decoder_destroy(ctx->decoder);
	jitter_ringbuffer_destroy(ctx->in);
	free(ctx);
}

const decoder_t *decoder_opus = &(decoder_t)
{
	.init = decoder_init,
	.jitter = decoder_jitter,
	.run = decoder_run,
	.destroy = decoder_destroy,
};

#ifndef DECODER_GET
#define DECODER_GET
const decoder_t *decoder_get(decoder_ctx_t *ctx)
{
	return ctx->ops;
}
#endif
