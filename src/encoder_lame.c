/*****************************************************************************
 * encoder_lame.c
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <lame/lame.h>

#include "player.h"
typedef struct encoder_s encoder_t;
typedef struct encoder_ctx_s encoder_ctx_t;
struct encoder_ctx_s
{
	const encoder_t *ops;
	lame_global_flags *encoder;
	int nsamples;
	int dumpfd;
	pthread_t thread;
	player_ctx_t *ctx;
	jitter_t *in;
	unsigned char *inbuffer;
	jitter_t *out;
	unsigned char *outbuffer;
};
#define ENCODER_CTX
#include "encoder.h"
#include "jitter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif
#define encoder_dbg(...)

static const char *jitter_name = "lame encoder";
void error_report(const char *format, va_list ap)
{
	fprintf(stderr, format, ap);
}

static encoder_ctx_t *encoder_init(player_ctx_t *ctx)
{
	encoder_ctx_t *encoder = calloc(1, sizeof(*encoder));
	encoder->ops = encoder_lame;
	encoder->ctx = ctx;
	encoder->encoder = lame_init();

	lame_set_out_samplerate(encoder->encoder, 44100);
	lame_set_in_samplerate(encoder->encoder, 44100);
	lame_set_num_channels(encoder->encoder, 2);
	lame_set_quality(encoder->encoder, 5);
	//lame_set_mode(encoder->encoder, STEREO);
	//lame_set_mode(encoder->encoder, JOINT_STEREO);
	//lame_set_errorf(encoder->encoder, error_report);
	lame_set_VBR(encoder->encoder, vbr_off);
	//lame_set_VBR(encoder->encoder, vbr_default);
	lame_set_disable_reservoir(encoder->encoder, 1);
	lame_init_params(encoder->encoder);

#ifdef LAME_DUMP
	encoder->dumpfd = open("lame_dump.mp3", O_RDWR | O_CREAT);
#endif
	int nchannels = 2;
	encoder->nsamples = lame_get_framesize(encoder->encoder);
	jitter_t *jitter = jitter_scattergather_init(jitter_name, 3,
				encoder->nsamples * sizeof(signed short) * nchannels);
	encoder->in = jitter;
	jitter->format = PCM_16bits_LE_stereo;

	return encoder;
}

static jitter_t *encoder_jitter(encoder_ctx_t *encoder)
{
	return encoder->in;
}

static void *lame_thread(void *arg)
{
	int result = 0;
	int run = 1;
	encoder_ctx_t *encoder = (encoder_ctx_t *)arg;
	/* start decoding */
	while (run)
	{
		int ret = 0;

		encoder->inbuffer = encoder->in->ops->peer(encoder->in->ctx);
		if (encoder->outbuffer == NULL)
		{
			encoder->outbuffer = encoder->out->ops->pull(encoder->out->ctx);
		}
		if (encoder->inbuffer)
		{
			ret = lame_encode_buffer_interleaved(encoder->encoder, (short int *)encoder->inbuffer, encoder->nsamples,
					encoder->outbuffer, encoder->out->ctx->size);
#ifdef LAME_DUMP
			if (encoder->dumpfd > 0 && ret > 0)
			{
				dbg("encoder lame dump %d", ret);
				write(encoder->dumpfd, encoder->outbuffer, ret);
			}
#endif
			encoder->in->ops->pop(encoder->in->ctx, encoder->in->ctx->size);
		}
		else
		{
			ret = lame_encode_flush(encoder->encoder, encoder->outbuffer, encoder->out->ctx->size);
		}
		if (ret > 0)
		{
			encoder_dbg("encoder lame %d", ret);
			encoder->out->ops->push(encoder->out->ctx, ret, NULL);
			encoder->outbuffer = NULL;
		}
		if (ret < 0)
		{
			if (ret == -1)
				err("lame error %d, not enought memory %d", ret, encoder->out->ctx->size);
			else
				err("lame error %d", ret);
			run = 0;
		}
	}
	return (void *)result;
}

static int encoder_run(encoder_ctx_t *encoder, jitter_t *jitter)
{
	encoder->out = jitter;
	pthread_create(&encoder->thread, NULL, lame_thread, encoder);
	return 0;
}

static void encoder_destroy(encoder_ctx_t *encoder)
{
#ifdef LAME_DUMP
	if (encoder->dumpfd > 0)
		close(encoder->dumpfd);
#endif
	pthread_join(encoder->thread, NULL);
	/* release the decoder */
	jitter_scattergather_destroy(encoder->in);
	free(encoder);
}

const encoder_t *encoder_lame = &(encoder_t)
{
	.init = encoder_init,
	.jitter = encoder_jitter,
	.run = encoder_run,
	.destroy = encoder_destroy,
};

#ifndef ENCODER_GET
#define ENCODER_GET
const encoder_t *encoder_get(encoder_ctx_t *ctx)
{
	return ctx->ops;
}
#endif
