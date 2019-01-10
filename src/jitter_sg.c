/*****************************************************************************
 * jitter_sg.c
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

#define __USE_GNU
#include <pthread.h>

#include "jitter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define jitter_dbg(...)

typedef struct scatter_s scatter_t;
struct scatter_s
{
	enum
	{
		SCATTER_FREE,
		SCATTER_PULL,
		SCATTER_POP,
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
		JITTER_FLUSH,
		JITTER_COMPLETE,
	} state;
};

static unsigned char *jitter_pull(jitter_ctx_t *jitter);
static void jitter_push(jitter_ctx_t *jitter, size_t len, void *beat);
static unsigned char *jitter_peer(jitter_ctx_t *jitter);
static void jitter_pop(jitter_ctx_t *jitter, size_t len);
static void jitter_reset(jitter_ctx_t *jitter);

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
		err("jitter %s not enought memory %lu", name, count * size);
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
	dbg("jitter %s create scattergather (%d*%ld)", name, count, size);
	return jitter;
}

void jitter_scattergather_destroy(jitter_t *jitter)
{
	jitter_ctx_t *ctx = jitter->ctx;
	jitter_private_t *private = (jitter_private_t *)ctx->private;

	jitter_reset(ctx);

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

static unsigned char *jitter_pull(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	if (private->state == JITTER_STOP)
		_jitter_init(jitter);
	while (private->in->state != SCATTER_FREE)
	{
		jitter_dbg("jitter %s pull block on %p %d", jitter->name, private->in, private->in->state);
		pthread_cond_wait(&private->condpush, &private->mutex);
	}
	private->in->state = SCATTER_PULL;
	pthread_mutex_unlock(&private->mutex);
	jitter_dbg("jitter %s pull %p", jitter->name, private->in);
	return private->in->data;
}

static void jitter_push(jitter_ctx_t *jitter, size_t len, void *beat)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	jitter_dbg("jitter %s push %p", jitter->name, private->in);
	if (private->in->state != SCATTER_PULL)
	{
		pthread_cond_broadcast(&private->condpeer);
		return;
	}
	if (len == 0)
	{
		/**
		 * TODO check the ring buffer push 0 kills the jitter
		 */
		dbg("jitter sg %s push 0", jitter->name);
		pthread_mutex_lock(&private->mutex);
		private->in->state = SCATTER_FREE;
		private->state = JITTER_COMPLETE;
		pthread_mutex_unlock(&private->mutex);
	}
	else
	{
		pthread_mutex_lock(&private->mutex);
		private->in->len = len;
		private->in->beat = beat;
		private->in->state = SCATTER_READY;
		private->level++;
		private->in = private->in->next;
		pthread_mutex_unlock(&private->mutex);
		if (jitter->consume != NULL)
		{
			private->out->state = SCATTER_POP;
			int tlen = 0;
			if (private->out->beat && jitter->heart != NULL)
			{
				jitter->heart(jitter->heart_ctx, private->out->beat);
			}
			do
			{
				int ret;
				ret = jitter->consume(jitter->consumer,
					private->out->data + tlen, len - tlen);
				if (ret > 0)
					tlen += ret;
				if (ret <= 0)
				{
					tlen = ret;
					break;
				}
			} while (tlen < len);
			if (tlen > 0)
				jitter_pop(jitter, tlen);
			else
				return;
		}
	}

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

static unsigned char *jitter_peer(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	pthread_mutex_lock(&private->mutex);
	if (private->state == JITTER_STOP)
		_jitter_init(jitter);
	pthread_mutex_unlock(&private->mutex);
	if (private->out->state == SCATTER_FREE)
	{
		if ((private->in == private->out) && (jitter->produce != NULL))
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
				{
					len = ret;
					break;
				}
			} while (len < jitter->size);
			if (len > 0)
				jitter_push(jitter, len, NULL);
			else
			{
				dbg("produce nothing");
				return NULL;
			}
		}
		else if (private->state == JITTER_COMPLETE)
			return NULL;
	}
	pthread_mutex_lock(&private->mutex);
	while ((private->state == JITTER_FILLING) &&
			(private->out->state != SCATTER_READY))
	{
		jitter_dbg("jitter %s peer block on %p", jitter->name, private->out);
		pthread_cond_wait(&private->condpeer, &private->mutex);
	}
	private->out->state = SCATTER_POP;
	pthread_mutex_unlock(&private->mutex);
	if (private->out->beat && jitter->heart != NULL)
	{
		jitter->heart(jitter->heart_ctx, private->out->beat);
	}
	return private->out->data;
}

static void jitter_pop(jitter_ctx_t *jitter, size_t len)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;

	jitter_dbg("jitter %s pop", jitter->name);
	if ((private->state == JITTER_STOP) ||
		(private->out->state != SCATTER_POP))
	{
		private->out->state = SCATTER_FREE;
		pthread_cond_broadcast(&private->condpush);
		return;
	}

	if (private->out->len > len)
	{
		dbg("buffer not empty %ld %ld", private->out->len, len);
	}

	pthread_mutex_lock(&private->mutex);
	private->out->state = SCATTER_FREE;
	private->level--;
	private->out = private->out->next;
	pthread_mutex_unlock(&private->mutex);
	if (private->state == JITTER_RUNNING)
	{
		pthread_cond_broadcast(&private->condpush);
	}
	if (private->level == 0 &&
		jitter->thredhold > 0)
		private->state = JITTER_FILLING;
}

static void jitter_flush(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;
	if (private->in->state != SCATTER_FREE)
		private->in->state = SCATTER_FREE;
	private->state = JITTER_FLUSH;
}

static size_t jitter_length(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;
	if (private->out->state == SCATTER_POP)
		return private->out->len;
	return -1;
}

static void jitter_reset(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;
	jitter_dbg("jitter %s reset", jitter->name);

	if (private->out->state == SCATTER_PULL)
	{
		pthread_mutex_lock(&private->mutex);
		private->out->state = SCATTER_READY;
		pthread_mutex_unlock(&private->mutex);
		pthread_cond_broadcast(&private->condpeer);
		pthread_yield();
		pthread_mutex_lock(&private->mutex);
		while (private->out->state != SCATTER_FREE)
			pthread_cond_wait(&private->condpush, &private->mutex);
		pthread_mutex_unlock(&private->mutex);
	}
	pthread_mutex_lock(&private->mutex);
	int i = 0;
	for (i = 0; i < jitter->count; i++)
	{
		private->in->state = SCATTER_FREE;
		private->in = private->in->next;
	}
	pthread_mutex_unlock(&private->mutex);

	pthread_cond_broadcast(&private->condpush);

	pthread_mutex_lock(&private->mutex);
	private->level = 0;
	private->state = JITTER_FILLING;
	private->in = private->out = private->sg;
	pthread_mutex_unlock(&private->mutex);
}

static int jitter_empty(jitter_ctx_t *jitter)
{
	jitter_private_t *private = (jitter_private_t *)jitter->private;
	return (private->out->state != SCATTER_READY);
}

static const jitter_ops_t *jitter_scattergather = &(jitter_ops_t)
{
	.reset = jitter_reset,
	.pull = jitter_pull,
	.push = jitter_push,
	.peer = jitter_peer,
	.pop = jitter_pop,
	.flush = jitter_flush,
	.length = jitter_length,
	.empty = jitter_empty,
};
