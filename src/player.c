/*****************************************************************************
 * player.c
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define __USE_GNU
#include <pthread.h>

#include "player.h"
#include "media.h"
#include "jitter.h"
#include "src.h"
#include "decoder.h"
#include "encoder.h"
#include "sink.h"
#include "filter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

typedef struct player_event_s player_event_t;
struct player_event_s
{
	int id;
	player_event_type_t type;
	player_event_cb_t cb;
	void *ctx;
	player_event_t *next;
};

struct player_ctx_s
{
	const char *filtername;
	filter_t *filter;

	media_t *media;
	media_t *nextmedia;
	state_t state;
	player_event_t *events;

	src_t *src;
	src_t *nextsrc;

	pthread_cond_t cond;
	pthread_cond_t cond_int;
	pthread_mutex_t mutex;

	jitter_t *outstream[MAX_ESTREAM];
	int noutstreams;
};

player_ctx_t *player_init(const char *filtername)
{
	player_ctx_t *ctx = calloc(1, sizeof(*ctx));
	pthread_mutex_init(&ctx->mutex, NULL);
	pthread_cond_init(&ctx->cond, NULL);
	pthread_cond_init(&ctx->cond_int, NULL);
	ctx->state = STATE_STOP;
	ctx->filtername = filtername;
	return ctx;
}

int player_change(player_ctx_t *ctx, const char *mediapath, int random, int loop, int now)
{
	media_t *media = media_build(ctx, mediapath);
	if (media)
	{
		ctx->nextmedia = media;
		if (now || ctx->media == NULL)
		{
			ctx->state = STATE_STOP;
			pthread_cond_broadcast(&ctx->cond);
		}

		if (ctx->media == NULL)
			ctx->media = media;
		if (media->ops->loop && loop)
		{
			media->ops->loop(media->ctx, OPTION_ENABLE);
		}
		if (media->ops->random && random)
		{
			media->ops->random(media->ctx, OPTION_ENABLE);
		}
	}
	return 0;
}

void player_next(player_ctx_t *ctx)
{
	if (ctx->media != NULL)
	{
		/**
		 * next command just request the main loop to complete
		 * the current entry to jump to the next one.
		 * The main loop will set the next one long time before
		 * than somebody request the jump to the next one.
		 */
		player_state(ctx, STATE_CHANGE);
	}
}

media_t *player_media(player_ctx_t *ctx)
{
	return ctx->media;
}

void player_destroy(player_ctx_t *ctx)
{
	player_state(ctx, STATE_ERROR);
	pthread_yield();
	pthread_cond_destroy(&ctx->cond);
	pthread_cond_destroy(&ctx->cond_int);
	pthread_mutex_destroy(&ctx->mutex);
	free(ctx);
}

void player_removeevent(player_ctx_t *ctx, int id)
{
	player_event_t *event = ctx->events;
	if (event->id == id)
	{
		ctx->events = event->next;
		free(event);
		return;
	}
	while (event != NULL)
	{
		player_event_t *next = event->next;
		if (next != NULL && next->id == id)
		{
			event->next = next->next;
			free(next);
			return;
		}
		event = event->next;
	}
}

int player_onchange(player_ctx_t *ctx, player_event_cb_t callback, void *cbctx, char *name)
{
	player_event_t *event = calloc(1, sizeof(*event));
	if (ctx->events != NULL)
		event->id = ctx->events->id + 1;
	else
		event->id = 0;
	event->cb = callback;
	event->ctx = cbctx;
	event->type = EVENT_ONCHANGE;
	event->next = ctx->events;
	ctx->events = event;
	return event->id;
}

state_t player_state(player_ctx_t *ctx, state_t state)
{
	if ((state != STATE_UNKNOWN) && ctx->state != state)
	{
		if (pthread_mutex_trylock(&ctx->mutex) != 0)
			return ctx->state;
		ctx->state = state;
		pthread_mutex_unlock(&ctx->mutex);
		pthread_cond_broadcast(&ctx->cond_int);
	}
	state = ctx->state;
	return state;
}

int player_mediaid(player_ctx_t *ctx)
{
	if (ctx->src != NULL)
	{
		return ctx->src->mediaid;
	}
	return -1;
}

int player_waiton(player_ctx_t *ctx, int state)
{
	if (ctx->state == STATE_CHANGE ||
//		ctx->state == STATE_STOP ||
		ctx->state == STATE_ERROR)
		return -1;
	pthread_mutex_lock(&ctx->mutex);
	while (ctx->state == state)
	{
		pthread_cond_wait(&ctx->cond, &ctx->mutex);
	}
	pthread_mutex_unlock(&ctx->mutex);
	return 0;
}

static void _player_new_es(player_ctx_t *ctx, const src_t *src, void *eventarg)
{
	event_new_es_t *event_data = (event_new_es_t *)eventarg;
	if (ctx->src == NULL)
	{
		err("player: source is null");
		return;
	}

	warn("player decoder build");
	event_data->decoder = decoder_build(ctx, event_data->mime);
	if (event_data->decoder == NULL)
		err("player: decoder not found for %s", event_data->mime);
	else
		src->ops->attach(src->ctx, event_data->pid, event_data->decoder);
}

static void _player_decode_es(player_ctx_t *ctx, const src_t *src, void *eventarg)
{
	event_decode_es_t *event_data = (event_decode_es_t *)eventarg;
	if (event_data->decoder != NULL && ctx->noutstreams < MAX_ESTREAM)
	{
		int i;
		for ( i = 0; i < ctx->noutstreams; i++)
		{
			jitter_t *outstream = ctx->outstream[i];
			if (event_data->decoder->ops->run(event_data->decoder->ctx, outstream) == 0)
				break;
		}

	}
}

static void _player_listener(void *arg, const src_t *src, event_t event, void *eventarg)
{
	player_ctx_t *ctx = (player_ctx_t *)arg;
	switch (event)
	{
		case SRC_EVENT_NEW_ES:
			_player_new_es(ctx, src, eventarg);
		break;
		case SRC_EVENT_DECODE_ES:
			_player_decode_es(ctx, src, eventarg);
		break;
	}
}

static int _player_play(void* arg, int id, const char *url, const char *info, const char *mime)
{
	player_ctx_t *ctx = (player_ctx_t *)arg;
	src_t *src = NULL;

	dbg("player: prepare %d %s %s", id, url, mime);
	src = src_build(ctx, url, mime, id);
	if (src != NULL)
	{
		ctx->nextsrc = src;

		if (src->ops->eventlistener)
		{
			src->ops->eventlistener(src->ctx, _player_listener, ctx);
		}
		else
		{
			const event_new_es_t event_new = {.pid = 0, .mime = mime, .jitte = JITTE_LOW};
			_player_listener(ctx, src, SRC_EVENT_NEW_ES, (void *)&event_new);
			const event_decode_es_t event_decode = {.pid = 0, .decoder = event_new.decoder};
			_player_listener(ctx, src, SRC_EVENT_DECODE_ES, (void *)&event_decode);
		}

		return 0;
	}
	else
	{
		dbg("player: src not found for %s", url);
		ctx->nextsrc = NULL;
	}
	return -1;
}

int player_subscribe(player_ctx_t *ctx, estream_t type, jitter_t *encoder_jitter)
{
	if (type == ES_AUDIO)
	{
		ctx->outstream[ctx->noutstreams] = encoder_jitter;
		ctx->noutstreams++;
	}
	return 0;
}

int player_run(player_ctx_t *ctx)
{
	if (ctx->noutstreams == 0)
		return -1;

	int last_state = STATE_STOP;
	int i;
	while (last_state != STATE_ERROR)
	{
		pthread_mutex_lock(&ctx->mutex);
		while (last_state == ctx->state)
		{
			pthread_cond_wait(&ctx->cond_int, &ctx->mutex);
			if (last_state == STATE_PAUSE && ctx->state == STATE_PLAY)
			{
				last_state = ctx->state;
				pthread_cond_broadcast(&ctx->cond);
			}
		}
		last_state = ctx->state;

		switch (ctx->state)
		{
			case STATE_STOP:
				dbg("player: stoping");
				for (i = 0; i < ctx->noutstreams; i++)
					ctx->outstream[i]->ops->flush(ctx->outstream[i]->ctx);
				if (ctx->src != NULL)
				{
					ctx->src->ops->destroy(ctx->src->ctx);
					free(ctx->src);
					ctx->src = NULL;
				}
				if (ctx->nextsrc != NULL)
				{
					ctx->nextsrc->ops->destroy(ctx->nextsrc->ctx);
					free(ctx->nextsrc);
					ctx->nextsrc = NULL;
				}

				ctx->media->ops->end(ctx->media->ctx);
				for (i = 0; i < ctx->noutstreams; i++)
					ctx->outstream[i]->ops->reset(ctx->outstream[i]->ctx);
				dbg("player: stop");
			break;
			case STATE_CHANGE:
				if (ctx->src != NULL)
				{
					dbg("player: wait");
					ctx->src->ops->destroy(ctx->src->ctx);
					free(ctx->src);
					ctx->src = NULL;
				}
				ctx->src = ctx->nextsrc;
				ctx->nextsrc = NULL;

				if (ctx->src != NULL)
				{
					dbg("player: play");
					/**
					 * the src needs to be ready before the decoder
					 * to set a producer if it's needed
					 */
					ctx->src->ops->run(ctx->src->ctx);
					ctx->state = STATE_PLAY;
				}
				else
				{
					ctx->state = STATE_STOP;
				}
			break;
			case STATE_PLAY:
				/**
				 * first check if a new media is requested
				 */
				if (ctx->media != ctx->nextmedia)
				{
					if (ctx->media)
					{
						ctx->media->ops->destroy(ctx->media->ctx);
						free(ctx->media);
					}
					ctx->media = ctx->nextmedia;
				}
				if (ctx->media == NULL)
				{
					err("media not available");
					ctx->state = STATE_STOP;
					break;
				}

				if (ctx->media->ops->next)
				{
					ctx->media->ops->next(ctx->media->ctx);
				}
				else if (ctx->media->ops->end)
					ctx->media->ops->end(ctx->media->ctx);

				if (ctx->nextsrc != NULL)
				{
					ctx->nextsrc->ops->destroy(ctx->nextsrc->ctx);
					free(ctx->nextsrc);
					ctx->nextsrc = NULL;
				}
				ctx->media->ops->play(ctx->media->ctx, _player_play, ctx);
				if (ctx->src == NULL)
					ctx->state = STATE_CHANGE;
			break;
			case STATE_PAUSE:
			break;
		}
		pthread_mutex_unlock(&ctx->mutex);
		if (last_state != ctx->state)
			pthread_cond_broadcast(&ctx->cond);

		/******************
		 * event manager  *
		 ******************/
		player_event_t *it = ctx->events;
		while (it != NULL)
		{
			it->cb(it->ctx, ctx, ctx->state);
			it = it->next;
		}
	}
	return 0;
}

const char *player_filtername(player_ctx_t *ctx)
{
	return ctx->filtername;
}

const src_t *player_source(player_ctx_t *ctx)
{
	return ctx->src;
}
