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

#include <pthread.h>

#include "jitter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define VARIATIC_OUTPUT 1

static unsigned char *jitter_pull(jitter_ctx_t *jitter);
static void jitter_push(jitter_ctx_t *jitter, size_t len, void *beat);
static unsigned char *jitter_peer(jitter_ctx_t *jitter);
static void jitter_pop(jitter_ctx_t *jitter, size_t len);

typedef struct jitter_private_s jitter_private_t;
struct jitter_private_s
{
	unsigned char *buffer;
	unsigned char *bufferstart;
	unsigned char *bufferend;
	unsigned char *in;
	unsigned char *out;
	pthread_mutex_t mutex;
	pthread_cond_t condpush;
	pthread_cond_t condpeer;
	unsigned int level; 
	enum
	{
		JITTER_STOP,
		JITTER_FILLING,
		JITTER_RUNNING,
		JITTER_OVERFLOW,
	} state;
};

static const jitter_ops_t *jitter_ringbuffer;

jitter_t *jitter_ringbuffer_init(const char *name, unsigned int count, size_t size)
{
	jitter_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->count = count;
	ctx->size = size;
	ctx->name = name;
	count += VARIATIC_OUTPUT;
	jitter_private_t *private = calloc(1, sizeof(*private));
	private->buffer = malloc(count * size);
	private->bufferstart = private->buffer + (VARIATIC_OUTPUT * size);
	private->bufferend = private->buffer + (count * size);
	if (private->buffer == NULL)
	{
		err("jitter %s not enought memory %u", name, count * size);
		free(private);
		free(ctx);
		return NULL;
	}
	pthread_mutex_init(&private->mutex, NULL);
	pthread_cond_init(&private->condpush, NULL);
	pthread_cond_init(&private->condpeer, NULL);
	private->in = private->out = private->bufferstart;

	ctx->private = private;
	jitter_t *jitter = calloc(1, sizeof(*jitter));
	jitter->ctx = ctx;
	jitter->ops = jitter_ringbuffer;
	dbg("create ring buffer %d", count * size);
	return jitter;
}

void jitter_ringbuffer_destroy(jitter_t *jitter)
{
	jitter_ctx_t *ctx = jitter->ctx;
	jitter_private_t *private = (jitter_private_t *)ctx->private;

	pthread_cond_destroy(&private->condpush);
	pthread_cond_destroy(&private->condpeer);
	pthread_mutex_destroy(&private->mutex);

	free(private->buffer);
	free(private);
	free(ctx);
	free(jitter);
}

static void _jitter_init(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	if (jitter->thredhold < 1)
		jitter->thredhold = 1;
	private->state = JITTER_FILLING;
}

static unsigned char *jitter_pull(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	if (private->state == JITTER_STOP)
		_jitter_init(jitter);
	pthread_mutex_unlock(&private->mutex);
	if ((private->in == private->out) &&
		(jitter->consume != NULL))
	{
		int len;
		len = jitter->consume(jitter->consumer, 
			private->out, jitter->size);
		if (len > 0)
			jitter_pop(jitter, len);
		else
			return NULL;
	}
	if (private->in == NULL)
		return NULL;
	pthread_mutex_lock(&private->mutex);
	while ((private->in + jitter->size) < private->out)
	{
		dbg("jitter %s push block on %p", jitter->name, private->in);
		pthread_cond_wait(&private->condpush, &private->mutex);
	}
	pthread_mutex_unlock(&private->mutex);
	return private->in;
}

static void jitter_push(jitter_ctx_t *jitter, size_t len, void *beat)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	private->level += len;
	private->in += len;
	pthread_mutex_unlock(&private->mutex);
	if (private->in == private->bufferend)
		private->in = private->bufferstart;
	if ((len != jitter->size) ||
		((private->in + jitter->size) > private->bufferend))
	{
		memset(private->in, 0, private->bufferend - private->in);
		dbg("jitter variatic input not implemented");
		private->in = NULL;
	}
	if (private->state == JITTER_RUNNING)
	{
		pthread_cond_broadcast(&private->condpeer);
	}
	else if (private->state == JITTER_FILLING &&
			private->level == (jitter->thredhold * jitter->size))
	{
		dbg("running ring buffer ");
		private->state = JITTER_RUNNING;
	}
	else
	{
		dbg("filling ring buffer ");
	}
}

static unsigned char *jitter_peer(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	if (private->state == JITTER_STOP)
		_jitter_init(jitter);
	pthread_mutex_unlock(&private->mutex);
	if ((private->in < (private->out + jitter->size)) &&
		(jitter->produce != NULL))
	{
		int len;
		len = jitter->produce(jitter->producter, 
			private->in, jitter->size);
		if (len > 0)
			jitter_push(jitter, len, NULL);
		else
			return NULL;
	}
	pthread_mutex_lock(&private->mutex);
	while((private->in > private->out) &&
		(private->out + jitter->size) > private->in)
	{
		pthread_cond_wait(&private->condpeer, &private->mutex);
	}
	pthread_mutex_unlock(&private->mutex);
	return private->out;
}

static void jitter_pop(jitter_ctx_t *jitter, size_t len)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	if (private->state == JITTER_STOP)
		return;

	pthread_mutex_lock(&private->mutex);
	private->out += len;
	if ((private->out + jitter->size) > private->bufferend)
	{
		int len = VARIATIC_OUTPUT * (private->bufferend - private->out);
		if (len > jitter->size)
			len = jitter->size;
		memcpy(private->bufferstart - len, private->out, len);
		private->out = private->bufferstart - len;
	}
	private->level-=len;
	pthread_mutex_unlock(&private->mutex);
	if (private->state == JITTER_RUNNING)
	{
		pthread_cond_broadcast(&private->condpush);
		if (private->level <= 0)
			private->state = JITTER_FILLING;
	}
}

static void jitter_reset(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	private->in = private->bufferstart;
	private->out = private->bufferstart;
	pthread_mutex_unlock(&private->mutex);

	pthread_cond_broadcast(&private->condpeer);
	pthread_cond_broadcast(&private->condpush);
}

static const jitter_ops_t *jitter_ringbuffer = &(jitter_ops_t)
{
	.reset = jitter_reset,
	.pull = jitter_pull,
	.push = jitter_push,
	.peer = jitter_peer,
	.pop = jitter_pop,
};
