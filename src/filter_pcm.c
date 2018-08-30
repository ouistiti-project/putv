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

typedef struct filter_ctx_s filter_ctx_t;
struct filter_ctx_s
{
	unsigned int samplerate;
	unsigned int samplesize;
	unsigned int nchannels;
};
#define FILTER_CTX
#include "filter.h"

filter_ctx_t *filter_init(unsigned int samplerate, unsigned int samplesize, unsigned int nchannels)
{
	filter_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->samplerate = samplerate;
	ctx->samplesize = samplesize;
	ctx->nchannels = nchannels;
	return ctx;
}

void filter_destroy(filter_ctx_t *ctx)
{
	free(ctx);
}

static int sampled(filter_ctx_t *ctx, unsigned char *in, int inlength, unsigned char *out, int outlength)
{
	int length = ctx->samplesize;
	if (length <= inlength)
	{
		in += (inlength - length);
	}
	memcpy(out, in, length);
	return length;
}

static int filter_run(filter_ctx_t *ctx, filter_audio_t *audio, unsigned char *buffer, size_t size)
{
	int bufferlen = 0;
	while (audio->nsamples && bufferlen < size)
	{
		int i;
		for (i = 0; i < audio->nchannels && i < ctx->nchannels; i++)
		{
			bufferlen += sampled(ctx, (unsigned char *)audio->samples[i]++, sizeof(*audio->samples[i]),
					buffer + bufferlen, size - bufferlen);
		}
		audio->nsamples--;
	}
	return bufferlen;
}

const filter_ops_t *filter_pcm = &(filter_ops_t)
{
	.init = filter_init,
	.run = filter_run,
	.destroy = filter_destroy,
};
