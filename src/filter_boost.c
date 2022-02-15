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

#include "filter.h"

static sample_t boost_increase(boost_t *ctx, sample_t sample, int bitspersample);
static sample_t boost_decrease(boost_t *ctx, sample_t sample, int bitspersample);
static sample_t boost_multi(boost_t *ctx, sample_t sample, int bitspersample);

boost_t *boost_init(boost_t *input, int db)
{
	if (input == NULL)
		input = calloc(1, sizeof(*input));
	input->replaygain = db;
	input->rgshift = db / 3;
	input->coef = db / 3.0;
	input->cb = boost_multi;
	return input;
}

sample_t boost_cb(void *arg, sample_t sample, int bitspersample)
{
	boost_t *ctx = (boost_t *)arg;
	return ctx->cb(ctx, sample, bitspersample);
}

static sample_t boost_increase(boost_t *ctx, sample_t sample, int bitspersample)
{
	sample_t mask = (((sample_t)1) << (bitspersample - 2));
	sample_t lead = sample & mask;
	sample = (sample << ctx->rgshift) & ~mask;
	sample |= lead;
	return sample;
}

static sample_t boost_decrease(boost_t *ctx, sample_t sample, int bitspersample)
{
	sample_t mask = 1 << (bitspersample - 2);
	sample_t lead = sample & mask;
	sample = (sample >> ctx->rgshift) & ~mask;
	sample |= lead;
	return sample;
}

static sample_t boost_multi(boost_t *ctx, sample_t sample, int bitspersample)
{
	sample += (sample * ctx->coef);
	return sample;
}
