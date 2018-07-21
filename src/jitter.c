/*****************************************************************************
 * jitter.c
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

#include <pthread.h>

#include "jitter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

typedef struct scatter_s scatter_t;
struct scatter_s
{
	enum
	{
		SCATTER_FREE,
		SCATTER_PULL,
		SCATTER_READY,
	} state;
	unsigned char *data;
	size_t len;
	void *beat;
	scatter_t *next;
};

typedef struct jitter_private_s jitter_private_t;
struct jitter_private_s
{
	unsigned char *buffer;
	scatter_t *sg;
	scatter_t *in;
	scatter_t *out;
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

static const jitter_ops_t *jitter_scattergather;

jitter_t *jitter_scattergather_init(const char *name, unsigned int count, size_t size)
{
	jitter_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->count = count;
	ctx->size = size;
	ctx->name = name;
	jitter_private_t *private = calloc(1, sizeof(*private));
	private->buffer = malloc(count * size);
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

	// create the scatter gather
	private->sg = calloc(count, sizeof(*private->sg)); 
	if (private->sg == NULL)
	{
		err("jitter %s not enought memory", name);
		free(private->buffer);
		free(private);
		free(ctx);
		return NULL;
	}
	int i;
	scatter_t *it;
	for (i = 0; i < count; i++)
	{
		it = &private->sg[i];
		it->data = private->buffer + (i * size);
		it->next = &private->sg[i + 1];
	}
	// loop on the first element
	it->next = private->sg;
	private->in = private->out = private->sg;

	ctx->private = private;
	jitter_t *jitter = calloc(1, sizeof(*jitter));
	jitter->ctx = ctx;
	jitter->ops = jitter_scattergather;
	return jitter;
}

void jitter_scattergather_destroy(jitter_t *jitter)
{
	jitter_ctx_t *ctx = jitter->ctx;
	jitter_private_t *private = (jitter_private_t *)ctx->private;

	pthread_cond_destroy(&private->condpush);
	pthread_cond_destroy(&private->condpeer);
	pthread_mutex_destroy(&private->mutex);

	free(private->buffer);
	free(private->sg);
	free(private);
	free(ctx);
	free(jitter);
}

static void _jitter_init(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	if (jitter->thredhold == 0)
		private->state = JITTER_RUNNING;
	else
		private->state = JITTER_FILLING;
}

unsigned char *jitter_pull(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	if (private->state == JITTER_STOP)
		_jitter_init(jitter);
	pthread_mutex_unlock(&private->mutex);
	if ((private->in == private->out) &&
		(private->in->state == SCATTER_READY) &&
		(jitter->consume != NULL))
	{
		int len = 0;
		do
		{
			int ret;
			ret = jitter->consume(jitter->consumer, 
				private->out->data + len, jitter->size - len);
			if (ret > 0)
				len += ret;
			if (ret <= 0)
				break;
		} while (len < jitter->size);
		if (len >= jitter->size)
		{
			private->out->state = SCATTER_FREE;
			private->out = private->out->next;
		}
	}
	pthread_mutex_lock(&private->mutex);
	while (private->in->state != SCATTER_FREE)
	{
		dbg("jitter %s push block on %d", jitter->name, private->in->state);
		pthread_cond_wait(&private->condpush, &private->mutex);
	}
	pthread_mutex_unlock(&private->mutex);
	private->in->state = SCATTER_PULL;
	return private->in->data;
}

void jitter_push(jitter_ctx_t *jitter, size_t len, void *beat)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	private->in->len = len;
	private->in->beat = beat;
	private->in->state = SCATTER_READY;
	private->in = private->in->next;
	pthread_mutex_lock(&private->mutex);
	private->level++;
	pthread_mutex_unlock(&private->mutex);
	if (private->state == JITTER_RUNNING)
	{
		pthread_cond_broadcast(&private->condpeer);
	}
	else if (private->state == JITTER_FILLING &&
			private->level == jitter->thredhold)
	{
		private->state = JITTER_RUNNING;
	}
}

unsigned char *jitter_peer(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	if (private->state == JITTER_STOP)
		_jitter_init(jitter);
	pthread_mutex_unlock(&private->mutex);
	if ((private->in == private->out) &&
		(private->out->state == SCATTER_FREE) &&
		(jitter->produce != NULL))
	{
		int len = 0;
		do
		{
			int ret;
			ret = jitter->produce(jitter->producter, 
				private->in->data + len, jitter->size - len);
			if (ret > 0)
				len += ret;
			if (ret <= 0)
				return NULL;
		} while (len < jitter->size);
		if (len >= jitter->size)
		{
			private->in->state = SCATTER_READY;
			private->in = private->in->next;
		}
	}
	pthread_mutex_lock(&private->mutex);
	while (private->out->state != SCATTER_READY)
	{
		dbg("jitter %s peer block on %d", jitter->name, private->out->state);
		pthread_cond_wait(&private->condpeer, &private->mutex);
	}
	pthread_mutex_unlock(&private->mutex);
	private->out->state = SCATTER_PULL;
	if (private->out->beat)
		jitter->heartbeat(jitter->heart, private->out->beat);
	return private->out->data;
}

void jitter_pop(jitter_ctx_t *jitter, size_t len)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	if (private->out->len > len)
	{
		dbg("buffer not empty");
	}
	private->out->state = SCATTER_FREE;
	private->out = private->out->next;
	pthread_mutex_lock(&private->mutex);
	private->level--;
	pthread_mutex_unlock(&private->mutex);
	if (private->state == JITTER_RUNNING)
	{
		pthread_cond_broadcast(&private->condpush);
		if (private->level == 0 &&
			jitter->thredhold > 0)
			private->state = JITTER_FILLING;
	}
}

void jitter_reset(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	private->in->state = SCATTER_FREE;
	private->out->state = SCATTER_FREE;
	pthread_mutex_unlock(&private->mutex);

	pthread_cond_broadcast(&private->condpeer);
	pthread_cond_broadcast(&private->condpush);

	pthread_mutex_lock(&private->mutex);
	while (private->out->state == SCATTER_PULL)
	{
		pthread_cond_wait(&private->condpeer, &private->mutex);
	}
	while (private->in->state == SCATTER_PULL)
	{
		pthread_cond_wait(&private->condpeer, &private->mutex);
	}
	private->in = private->out = private->sg;
	pthread_mutex_unlock(&private->mutex);
dbg("jitter %s reset", jitter->name);
}

static const jitter_ops_t *jitter_scattergather = &(jitter_ops_t)
{
	.reset = jitter_reset,
	.pull = jitter_pull,
	.push = jitter_push,
	.peer = jitter_peer,
	.pop = jitter_pop,
};
