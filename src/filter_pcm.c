/*****************************************************************************
 * jitter_ring.c
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

# define SIZEOF_INT 4

#if SIZEOF_INT >= 4
typedef   signed int sample_t;
#else
typedef   signed long sample_t;
#endif

typedef struct filter_ctx_s filter_ctx_t;
typedef int (*sampled_t)(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out);

struct filter_ctx_s
{
	sampled_t sampled;
	unsigned int samplerate;
	unsigned char samplesize;
	unsigned char shift;
	unsigned char nchannels;
};
#define FILTER_CTX
#include "filter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

# define FRACBITS		28
# define ONE		((sample_t)(0x10000000L))

#define SCALING_GAIN 7

static int sampled_change(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out);
static int sampled_copy32(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out);
static int sampled_copy24(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out);
static int sampled_copy16(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out);

filter_ctx_t *filter_init(unsigned int samplerate, jitter_format_t format)
{
	unsigned char samplesize = 4;
	unsigned char nchannels = 2;
	unsigned char shift = 1;
	sampled_t sampled = sampled_change;
	switch (format)
	{
	case PCM_16bits_LE_mono:
		samplesize = 2;
		shift = 8;
		nchannels = 1;
		sampled = sampled_copy16;
	break;
	case PCM_16bits_LE_stereo:
		samplesize = 2;
		shift = 8;
		nchannels = 2;
		sampled = sampled_copy16;
	break;
	case PCM_24bits3_LE_stereo:
		samplesize = 3;
		shift = 16;
		nchannels = 2;
		sampled = sampled_copy24;
	break;
	case PCM_24bits4_LE_stereo:
		samplesize = 4;
		shift = 16;
		nchannels = 2;
		sampled = sampled_copy24;
	break;
	case PCM_32bits_BE_stereo:
	case PCM_32bits_LE_stereo:
		samplesize = 4;
		shift = 24;
		nchannels = 2;
		sampled = sampled_copy32;
	break;
	default:
		err("decoder out format not supported %d", format);
		return NULL;
	}
	filter_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->samplerate = samplerate;
	ctx->samplesize = samplesize;
	ctx->shift = shift;
	ctx->sampled = sampled;
	ctx->nchannels = nchannels;

	return ctx;
}

void filter_destroy(filter_ctx_t *ctx)
{
	free(ctx);
}

/**
 * @brief this function comes from mad decoder
 * 
 * @arg sample the 32bits sample
 * @arg length the length of scaling (16 or 24)
 * 
 * @return sample
 */
static
signed int scale_sample(sample_t sample, int length)
{
	/* round */
	sample += (1L << (FRACBITS - length));

	/* clip */
	if (sample >= ONE)
		sample = ONE - 1;
	else if (sample < -ONE)
		sample = -ONE;

	/* quantize */
	return sample >> (FRACBITS + 1 - length);
}

static int sampled_change(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out)
{
	int i;
	for (i = 0; i < ctx->samplesize; i++)
	{
		int shift = ctx->shift - (i * 8);
		if (shift >= 0)
			*(out + i) = (sample >> (bitspersample - shift ) ) & 0x00FF;
	}
	return i;
}

static int sampled_copy32(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out)
{
	*(sample_t *)out = sample;
	return ctx->samplesize;
}

static int sampled_copy24(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out)
{
	*(sample_t *)out = sample >> 8;
	return ctx->samplesize;
}

static int sampled_copy16(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out)
{
	*(sample_t *)out = sample >> 16;
	return ctx->samplesize;
}

static int filter_run(filter_ctx_t *ctx, filter_audio_t *audio, unsigned char *buffer, size_t size)
{
	int j;
	int i;
	int bufferlen = 0;

	for (i = 0; i < audio->nsamples; i++)
	{
		sample_t sample;
		for (j = 0; j < ctx->nchannels; j++)
		{
			if (j < audio->nchannels)
				sample = audio->samples[(j % audio->nchannels)][i];
			else
				sample = audio->samples[0][i];
#ifdef FILTER_SCALING
			sample = scale_sample(sample, audio->bitspersample);
			sample = sample << (sizeof(sample_t) - audio->bitspersample - 1) ;
#endif
			bufferlen += ctx->sampled(ctx, sample, audio->bitspersample,
						buffer + bufferlen);
			if (bufferlen >= size)
				goto filter_exit;
		}
		if (bufferlen >= size)
			goto filter_exit;
	}
filter_exit:
	audio->nsamples -= i;
	for (j = 0; j < audio->nchannels; j++)
		audio->samples[j] += i;
	return bufferlen;
}

const filter_ops_t *filter_pcm = &(filter_ops_t)
{
	.init = filter_init,
	.run = filter_run,
	.destroy = filter_destroy,
};
