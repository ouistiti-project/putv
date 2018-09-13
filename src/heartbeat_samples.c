/*****************************************************************************
 * heartbeat_samples.c
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
#include <time.h>

typedef struct heartbeat_ctx_s heartbeat_ctx_t;
struct heartbeat_ctx_s
{
	unsigned int samplerate;
	unsigned int samplesize;
	unsigned int nchannels;
	struct timespec clock;
};
#define HEARTBEAT_CTX
#include "heartbeat.h"

heartbeat_ctx_t *heartbeat_init(unsigned int samplerate, unsigned int samplesize, unsigned int nchannels)
{
	heartbeat_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->samplerate = samplerate;
	ctx->samplesize = samplesize;
	ctx->nchannels = nchannels;
	return ctx;
}

void heartbeat_destroy(heartbeat_ctx_t *ctx)
{
	free(ctx);
}

static int heartbeat_wait(heartbeat_ctx_t *ctx, void *arg)
{
	heartbeat_samples_t *beat = (heartbeat_samples_t *)arg;
	clockid_t clockid = CLOCK_REALTIME;
	struct timespec clock = {0, 0};
	memcpy(&clock, &ctx->clock, sizeof(clock));
	unsigned long msec = beat->nsamples * 1000 / ctx->samplerate;
	clock.tv_nsec += (msec % 1000) * 1000000;
	clock.tv_sec += msec / 1000;
	struct timespec now = {0, 0};
	clock_gettime(clockid, &now);
	if (now.tv_sec > clock.tv_sec ||
		(now.tv_sec == clock.tv_sec && now.tv_nsec > clock.tv_nsec))
		return -1;
	int flags = TIMER_ABSTIME;
	clock_nanosleep(clockid, flags, &clock, NULL);
	clock_gettime(clockid, &ctx->clock);
	beat->nsamples = 0;

	return 0;
}

const heartbeat_ops_t *heartbeat_samples = &(heartbeat_ops_t)
{
	.init = heartbeat_init,
	.wait = heartbeat_wait,
	.destroy = heartbeat_destroy,
};
