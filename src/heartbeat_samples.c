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
#include <pthread.h>
#include <errno.h>

#include "jitter.h"
typedef struct heartbeat_ctx_s heartbeat_ctx_t;
struct heartbeat_ctx_s
{
	unsigned int samplerate;
	unsigned int samplesize;
	unsigned int nchannels;
	struct timespec clock;
	pthread_mutex_t mutex;
};
#define HEARTBEAT_CTX
#include "heartbeat.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define heartbeat_dbg(...)

heartbeat_ctx_t *heartbeat_init(unsigned int samplerate, jitter_format_t format, unsigned int nchannels)
{
	heartbeat_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->samplerate = samplerate;
	switch (format)
	{
	case PCM_16bits_LE_mono:
	case PCM_16bits_LE_stereo:
		ctx->samplesize = 2;
	break;
	case PCM_24bits3_LE_stereo:
		ctx->samplesize = 2;
	break;
	case PCM_24bits4_LE_stereo:
	case PCM_32bits_LE_stereo:
	case PCM_32bits_BE_stereo:
		ctx->samplesize = 2;
	break;
	default:
		ctx->samplesize = 4;
	break;
	}
	ctx->nchannels = nchannels;
	pthread_mutex_init(&ctx->mutex, NULL);
	return ctx;
}

void heartbeat_destroy(heartbeat_ctx_t *ctx)
{
	pthread_mutex_destroy(&ctx->mutex);
	free(ctx);
}

void heartbeat_start(heartbeat_ctx_t *ctx)
{
	pthread_mutex_lock(&ctx->mutex);
	clock_gettime(clockid, &ctx->clock);
	pthread_mutex_unlock(&ctx->mutex);
}

static int heartbeat_wait(heartbeat_ctx_t *ctx, void *arg)
{
	heartbeat_samples_t *beat = (heartbeat_samples_t *)arg;
	clockid_t clockid = CLOCK_REALTIME;
	if (ctx->samplerate == 0)
		return -1;

	pthread_mutex_lock(&ctx->mutex);

	unsigned long usec = beat->nsamples * 1000;
	int divider = ctx->samplerate / 100;
	usec /= divider;
	usec *= 10;
	if (ctx->clock.tv_sec == 0 && ctx->clock.tv_nsec == 0)
		clock_gettime(clockid, &ctx->clock);
	ctx->clock.tv_nsec += (usec % 1000000) * 1000;
	ctx->clock.tv_sec += usec / 1000000;
	if (ctx->clock.tv_nsec > 1000000000)
	{
		ctx->clock.tv_nsec -= 1000000000;
		ctx->clock.tv_sec += 1;
	}
	struct timespec now = {0, 0};
	clock_gettime(clockid, &now);
	if (now.tv_sec > ctx->clock.tv_sec ||
		(now.tv_sec == ctx->clock.tv_sec && now.tv_nsec > ctx->clock.tv_nsec))
	{
		now.tv_sec -= ctx->clock.tv_sec;
		now.tv_nsec -= ctx->clock.tv_nsec;
		if (now.tv_nsec < 0)
		{
			now.tv_nsec += 1000000000;
			now.tv_sec -= 1;
		}
		heartbeat_dbg("heartbeat to late %lu.%09lu", now.tv_sec, now.tv_nsec);
		clock_gettime(clockid, &ctx->clock);
		pthread_mutex_unlock(&ctx->mutex);
		return -1;
	}
	int flags = TIMER_ABSTIME;
	struct timespec rest = {0};
	while (clock_nanosleep(clockid, flags, &ctx->clock, &rest) != 0)
	{
		err("heartbeat hook");
	}
	heartbeat_dbg("heartbeat: boom %ld.%06ld", usec / 1000000, usec % 1000000);
	clock_gettime(clockid, &ctx->clock);
	//beat->nsamples = 0;

	pthread_mutex_unlock(&ctx->mutex);
	return 0;
}

static int heartbeat_lock(heartbeat_ctx_t *ctx)
{
	return pthread_mutex_lock(&ctx->mutex);
}

static int heartbeat_unlock(heartbeat_ctx_t *ctx)
{
	return pthread_mutex_unlock(&ctx->mutex);
}

const heartbeat_ops_t *heartbeat_samples = &(heartbeat_ops_t)
{
	.init = heartbeat_init,
	.start = heartbeat_start,
	.wait = heartbeat_wait,
	.lock = heartbeat_lock,
	.unlock = heartbeat_unlock,
	.destroy = heartbeat_destroy,
};
