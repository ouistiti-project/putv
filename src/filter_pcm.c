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
	unsigned char channel;
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

static filter_ctx_t *filter_init(sampled_t sampled, jitter_format_t format,...);
static int filter_set(filter_ctx_t *ctx, sampled_t sampled, jitter_format_t format, unsigned int samplerate);
static void filter_destroy(filter_ctx_t *ctx);

# define FRACBITS		28
# define ONE		((sample_t)(0x10000000L))

static filter_ctx_t *filter_init(sampled_t sampled, jitter_format_t format,...)
{
	filter_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->sampled = sampled;

	filter_set(ctx, sampled, format, 44100);
	return ctx;
}

#ifdef FILTER_ONECHANNEL
static filter_ctx_t *filter_init_onechannel(sampled_t sampled, jitter_format_t format, int channel)
{
	filter_ctx_t *ctx = filter_init(sampled, format);
	if (ctx != NULL)
	{
		ctx->channel = channel;
	}
	return ctx;
}

static filter_ctx_t *filter_init_left(sampled_t sampled, jitter_format_t format, ...)
{
	filter_ctx_t *ctx = filter_init_onechannel(sampled, format, 0);
	return ctx;
}
static filter_ctx_t *filter_init_right(sampled_t sampled, jitter_format_t format, ...)
{
	filter_ctx_t *ctx = filter_init_onechannel(sampled, format, 1);
	return ctx;
}
#endif

static int filter_set(filter_ctx_t *ctx, sampled_t sampled, jitter_format_t format, unsigned int samplerate)
{
	if (sampled != NULL)
		ctx->sampled = sampled;

	unsigned char samplesize = 4;
	unsigned char shift = 24;
	unsigned char nchannels = 2;
	switch (format)
	{
	case PCM_8bits_mono:
		samplesize = 2;
		shift = 8;
		nchannels = 1;
	break;
	case PCM_16bits_LE_mono:
		samplesize = 2;
		shift = 16;
		nchannels = 1;
	break;
	case PCM_16bits_LE_stereo:
		samplesize = 2;
		shift = 16;
		nchannels = 2;
	break;
	case PCM_24bits3_LE_stereo:
		samplesize = 3;
		shift = 24;
		nchannels = 2;
	break;
	case PCM_24bits4_LE_stereo:
		samplesize = 4;
		shift = 24;
		nchannels = 2;
	break;
	case PCM_32bits_BE_stereo:
	case PCM_32bits_LE_stereo:
		samplesize = 4;
		shift = 32;
		nchannels = 2;
	break;
	default:
		err("decoder out format not supported %d", format);
		return -1;
	}
	ctx->samplesize = samplesize;
	ctx->shift = shift;
	ctx->nchannels = nchannels;
	ctx->samplerate = samplerate;
	return 0;
}

static void filter_destroy(filter_ctx_t *ctx)
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
	sample = sample >> (FRACBITS + 1 - length);
	return sample;
}

int sampled_scaling(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out)
{
	int length = ((ctx->shift) > bitspersample)?bitspersample:ctx->shift;
	sample = scale_sample(sample, length);

	return sampled_change(ctx, sample, bitspersample, out);
}


int sampled_change(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out)
{
	int i = 0, j = 0;
	for (i = 0; i < ctx->samplesize; i++)
	{
		if ((i * 8) < (ctx->shift - bitspersample))
		{
			out[i] = 0;
			j++;
		}
		else if ((i * 8) > (ctx->shift))
			out[i] = 0;
		else
			out[i] = sample >> ((i - j) * 8);
	}
	return ctx->samplesize;
}

static int filter_interleave(filter_ctx_t *ctx, filter_audio_t *audio, unsigned char *buffer, size_t size)
{
	int j;
	int i;
	int bufferlen = 0;

	for (i = 0; i < audio->nsamples; i++)
	{
		sample_t sample;
		for (j = 0; j < ctx->nchannels; j++)
		{
			if (bufferlen >= size)
				goto filter_exit;

			if (j < audio->nchannels)
				sample = audio->samples[(j % audio->nchannels)][i];
			else
				sample = audio->samples[0][i];
			if (audio->regain > 0)
					sample = sample << audio->regain;
			else if (audio->regain < 0)
				sample = sample >> -audio->regain;
			int len = ctx->sampled(ctx, sample, audio->bitspersample,
						buffer + bufferlen);
			bufferlen += len;
		}
	}
filter_exit:
	audio->nsamples -= i;
	for (j = 0; j < audio->nchannels; j++)
		audio->samples[j] += i;
	return bufferlen;
}

#ifdef FILTER_MIXED
static int filter_mixemono(filter_ctx_t *ctx, filter_audio_t *audio, unsigned char *buffer, size_t size)
{
	int j;
	int i;
	int bufferlen = 0;

	for (i = 0; i < audio->nsamples; i++)
	{
		/**
		 * this is not the good algo to mixe the channels
		 */
		long long sample = 0;
		for (j = 0; j < audio->nchannels; j++)
		{
			sample += (audio->samples[j][i] / audio->nchannels);
		}
		for (j = 0; j < ctx->nchannels; j++)
		{
			if (audio->regain)
			{
				sample = sample << audio->regain;
			}
			int len = ctx->sampled(ctx, sample, audio->bitspersample,
						buffer + bufferlen);
			bufferlen += len;
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
#endif

#ifdef FILTER_ONECHANNEL
static int filter_mono(filter_ctx_t *ctx, filter_audio_t *audio, unsigned char *buffer, size_t size)
{
	int j;
	int i;
	int bufferlen = 0;

	for (i = 0; i < audio->nsamples; i++)
	{
		sample_t sample;
		for (j = 0; j < ctx->nchannels; j++)
		{
			sample = audio->samples[ctx->channel][i];
			if (audio->regain)
			{
				sample = sample << audio->regain;
			}
			int len = ctx->sampled(ctx, sample, audio->bitspersample,
						buffer + bufferlen);
			bufferlen += len;
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

#endif

const filter_ops_t *filter_pcm_interleave = &(filter_ops_t)
{
	.name = "pcm_stereo",
	.init = filter_init,
	.set = filter_set,
	.run = filter_interleave,
	.destroy = filter_destroy,
};


#ifdef FILTER_MIXED
const filter_ops_t *filter_pcm_mixed = &(filter_ops_t)
{
	.name = "pcm_mixed",
	.init = filter_init,
	.set = filter_set,
	.run = filter_mixemono,
	.destroy = filter_destroy,
};
#endif

#ifdef FILTER_ONECHANNEL
const filter_ops_t *filter_pcm_left = &(filter_ops_t)
{
	.name = "pcm_left",
	.init = filter_init_left,
	.set = filter_set,
	.run = filter_mono,
	.destroy = filter_destroy,
};

const filter_ops_t *filter_pcm_right = &(filter_ops_t)
{
	.name = "pcm_right",
	.init = filter_init_right,
	.set = filter_set,
	.run = filter_mono,
	.destroy = filter_destroy,
};
#endif

filter_t *filter_build(const char *name, jitter_format_t format, sampled_t sampled)
{
	filter_t *filter = calloc(1, sizeof (*filter));
	if (!strcmp(name, filter_pcm_interleave->name))
		filter->ops = filter_pcm_interleave;
#ifdef FILTER_MIXED
	if (!strcmp(name, filter_pcm_mixed->name))
		filter->ops = filter_pcm_mixed;
#endif
#ifdef FILTER_ONECHANNEL
	if (!strcmp(name, filter_pcm_left->name))
		filter->ops = filter_pcm_left;
	if (!strcmp(name, filter_pcm_right->name))
		filter->ops = filter_pcm_right;
#endif
	if (filter->ops != NULL)
		filter->ctx = filter->ops->init(sampled, format);
	else
	{
		free(filter);
		filter = NULL;
	}
	return filter;
}
