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

typedef struct player_decoder_s player_decoder_t;

struct player_decoder_s
{
	const src_t *src;
	int mediaid;
};

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
	player_decoder_t *current;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	jitter_t *audioout;
};

player_ctx_t *player_init(const char *filtername)
{
	player_ctx_t *ctx = calloc(1, sizeof(*ctx));
	pthread_mutex_init(&ctx->mutex, NULL);
	pthread_cond_init(&ctx->cond, NULL);
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
		pthread_mutex_lock(&ctx->mutex);
		ctx->state = state;
		pthread_mutex_unlock(&ctx->mutex);
		pthread_cond_broadcast(&ctx->cond);
	}
	state = ctx->state;
	return state;
}

int player_mediaid(player_ctx_t *ctx)
{
	if (ctx->current != NULL)
	{
		return ctx->current->mediaid;
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

struct _player_play_s
{
	player_ctx_t *ctx;
	player_decoder_t *dec;
};

static void _player_listener(void *arg, event_t event, void *eventarg)
{
	player_ctx_t *player = (player_ctx_t *)arg;
	if (event == SRC_EVENT_NEW_ES)
	{
		event_new_es_t *event_data = (event_new_es_t *)eventarg;
		if (player->current == NULL)
		{
			err("player: source is null");
			return;
		}
		const src_t *src = player_source(player);
		decoder_t *decoder = NULL;

		decoder = decoder_build(player, event_data->mime);
		if (decoder != NULL)
		{
			src->ops->attach(src->ctx, event_data->pid, decoder);
			decoder->ops->run(decoder->ctx, player->audioout);
		}
		else
			err("player: decoder not found for %s", event_data->mime);
	}
}

static int _player_play(void* arg, int id, const char *url, const char *info, const char *mime)
{
	struct _player_play_s *data = (struct _player_play_s *)arg;
	player_ctx_t *player = data->ctx;
	const src_t *src = NULL;

	dbg("player: prepare %d %s %s", id, url, mime);
	src = src_build(player, url, mime);
	if (src != NULL)
	{
		data->dec = calloc(1, sizeof(*data->dec));
		data->dec->mediaid = id;
		data->dec->src = src;

		if (src->ops->eventlistener)
		{
			src->ops->eventlistener(src->ctx, _player_listener, player);
		}
		else
		{
			const event_new_es_t event = {.pid = 0, .mime = mime, .jitte = JITTE_LOW};
			_player_listener(player, SRC_EVENT_NEW_ES, (void *)&event);
		}

		return 0;
	}
	else
	{
		dbg("player: src not found for %s", url);
		data->dec = NULL;
	}
	return -1;
}

int player_subscribe(player_ctx_t *ctx, estream_t type, jitter_t *encoder_jitter)
{
	if (type == ES_AUDIO)
	{
		ctx->audioout = encoder_jitter;
	}
	return 0;
}

int player_run(player_ctx_t *ctx)
{
	if (ctx->audioout == NULL)
		return -1;

	struct _player_play_s player =
	{
		.ctx = ctx,
		.dec = NULL,
	};

	int last_state = STATE_STOP;
	while (last_state != STATE_ERROR)
	{
		pthread_mutex_lock(&ctx->mutex);
		while (last_state == ctx->state)
		{
			pthread_cond_wait(&ctx->cond, &ctx->mutex);
		}
		last_state = ctx->state;
		pthread_mutex_unlock(&ctx->mutex);

		switch (ctx->state)
		{
			case STATE_STOP:
				dbg("player: stop");
				ctx->audioout->ops->flush(ctx->audioout->ctx);
				if (ctx->current != NULL)
				{
					ctx->current->src->ops->destroy(ctx->current->src->ctx);
					free(ctx->current);
					ctx->current = NULL;
				}
				if (player.dec != NULL)
				{
					player.dec->src->ops->destroy(player.dec->src->ctx);
					free(player.dec);
					player.dec = NULL;
				}

				ctx->audioout->ops->reset(ctx->audioout->ctx);
				ctx->media->ops->end(ctx->media->ctx);
			break;
			case STATE_CHANGE:
				if (ctx->current != NULL)
				{
					dbg("player: wait");
					ctx->current->src->ops->destroy(ctx->current->src->ctx);
					free(ctx->current);
					ctx->current = NULL;
				}
				ctx->current = player.dec;
				player.dec = NULL;

				if (ctx->current != NULL)
				{
					dbg("player: play");
					/**
					 * the src needs to be ready before the decoder
					 * to set a producer if it's needed
					 */
					const src_t *src = ctx->current->src;
					src->ops->run(src->ctx);
					player_state(ctx, STATE_PLAY);
				}
				else
				{
					player_state(ctx, STATE_STOP);
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
					player_state(ctx, STATE_STOP);
					break;
				}

				if (ctx->media->ops->next)
				{
					ctx->media->ops->next(ctx->media->ctx);
				}
				else if (ctx->media->ops->end)
					ctx->media->ops->end(ctx->media->ctx);

				if (player.dec != NULL)
				{
					player.dec->src->ops->destroy(player.dec->src->ctx);
					free(player.dec);
					player.dec = NULL;
				}
				ctx->media->ops->play(ctx->media->ctx, _player_play, &player);
				if (ctx->current == NULL)
					player_state(ctx, STATE_CHANGE);
			break;
			case STATE_PAUSE:
			break;
		}
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
	return ctx->current->src;
}
