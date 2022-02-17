/*****************************************************************************
 * filter_boost.c
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
#include <math.h>

#include "filter.h"

stats_t *stats_init(stats_t *input)
{
	if (input == NULL)
		input = calloc(1, sizeof(*input));
	return input;
}

sample_t stats_cb(void *arg, sample_t sample, int bitspersample, int channel)
{
	stats_t *ctx = (stats_t *)arg;

	sample_t asample = 0;
	asample = labs(sample);
	if (sample == INT32_MIN)
	{
		uint64_t max = (1L << (ctx->bitspersample - 1));
		fprintf(stdout, "peak %u / %ld\t", ctx->peak, max);
		fprintf(stdout, "rms %Lf\t", ctx->rms);
		fprintf(stdout, "boost %ld\t", ((max * 3) / ctx->peak) - 3);
		fprintf(stdout, "\n");
	}
	else
	{
		ctx->nbs++;
		if (ctx->peak < asample)
			ctx->peak = asample;
		long double sqrms = ((long double)ctx->rms * (long double)ctx->rms);
		if (ctx->rms < ctx->nbs)
		{
			sqrms *= (ctx->nbs - 1);
			sqrms /= ctx->nbs;
		}
		else
		{
			sqrms /= ctx->nbs;
			sqrms *= (ctx->nbs - 1);
		}
		long double sqsample = ((long double)sample * (long double)sample);
		sqsample /= ctx->nbs;
		ctx->rms = sqrtl(sqrms + sqsample);
/*
		if (ctx->mean < UINT64_MAX)
		{
			uint64_t mean = ctx->mean;
			mean *= (ctx->nbs - 1);
			mean += asample + 1;
			ctx->mean = mean / ctx->nbs;
		}
*/
		if (ctx->bitspersample == 0)
			ctx->bitspersample = bitspersample;
	}
	return sample;
}
