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
	player_event_type_t type;
	player_event_cb_t cb;
	void *ctx;
	player_event_t *next;
};

struct player_ctx_s
{
	media_t *media;
	int mediaid;
	state_t state;
	player_event_t *events;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
};

const char const *mime_octetstream = "octet/stream";

player_ctx_t *player_init(media_t *media)
{
	player_ctx_t *ctx = calloc(1, sizeof(*ctx));
	pthread_mutex_init(&ctx->mutex, NULL);
	pthread_cond_init(&ctx->cond, NULL);
	ctx->media = media;
	ctx->state = STATE_STOP;
	return ctx;
}

void player_destroy(player_ctx_t *ctx)
{
	player_state(ctx, STATE_ERROR);
	pthread_yield();
	pthread_cond_destroy(&ctx->cond);
	pthread_mutex_destroy(&ctx->mutex);
	free(ctx);
}

void player_onchange(player_ctx_t *ctx, player_event_cb_t callback, void *cbctx)
{
	player_event_t *event = calloc(1, sizeof(*event));
	event->cb = callback;
	event->ctx = cbctx;
	event->type = EVENT_ONCHANGE;
	event->next = ctx->events;
	ctx->events = event; 
}

state_t player_state(player_ctx_t *ctx, state_t state)
{
	if ((state != STATE_UNKNOWN) && ctx->state != state)
	{
		ctx->state = state;
		pthread_cond_broadcast(&ctx->cond);
		if (state == STATE_PAUSE)
		{
			player_event_t *it = ctx->events;
			while (it != NULL)
			{
				it->cb(it->ctx, ctx, ctx->state);
				it = it->next;
			}
		}
	}
	
	return ctx->state;
}

int player_mediaid(player_ctx_t *ctx)
{
	return ctx->mediaid;
}

int player_waiton(player_ctx_t *ctx, int state)
{
	if (ctx->state == STATE_STOP ||
		ctx->state == STATE_CHANGE ||
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

struct _player_decoder_s
{
	const decoder_t *decoder;
	decoder_ctx_t *decoder_ctx;
	const src_t *src;
	src_ctx_t *src_ctx;
};

struct _player_play_s
{
	player_ctx_t *ctx;
	struct _player_decoder_s *dec;
};

static int _player_play(void* arg, int id, const char *url, const char *info, const char *mime)
{
	struct _player_play_s *player = (struct _player_play_s *)arg;
	player_ctx_t *ctx = player->ctx;
	struct _player_decoder_s *dec;

	dec = calloc(1, sizeof(*dec));
	dec->src = SRC;
#ifdef DECODER_MAD
	if (mime && !strcmp(mime, decoder_mad->mime))
		dec->decoder = decoder_mad;
#endif
#ifdef DECODER_FLAC
	if (mime && !strcmp(mime, decoder_flac->mime))
		dec->decoder = decoder_flac;
#endif
	if (dec->decoder == NULL)
		dec->decoder = DECODER;

	if (dec->decoder != NULL)
	{
		dec->decoder_ctx = dec->decoder->init(ctx);
		dbg("player: prepare %s", url);
		dec->src_ctx = dec->src->init(ctx, url);
	}
	if (dec->src_ctx != NULL)
	{
		player->dec = dec;
		return 0;
	}
	else
	{
		player->dec = NULL;
		free(dec);
	}
	return -1;
}

int player_run(player_ctx_t *ctx, jitter_t *encoder_jitter)
{
	struct _player_play_s player =
	{
		.ctx = ctx,
		.dec = NULL,
	};
	struct _player_decoder_s *current_dec = NULL;
	while (ctx->state != STATE_ERROR)
	{
		pthread_mutex_lock(&ctx->mutex);
		while (ctx->state == STATE_STOP)
		{
			dbg("player: stop");
			encoder_jitter->ops->reset(encoder_jitter->ctx);
			pthread_cond_wait(&ctx->cond, &ctx->mutex);
		}
		pthread_mutex_unlock(&ctx->mutex);
		ctx->media->ops->next(ctx->media->ctx);

		do
		{
			player.dec = NULL;
			int nextid = ctx->media->ops->play(ctx->media->ctx, _player_play, &player);
			if (current_dec != NULL)
			{
				dbg("player: wait");
				current_dec->decoder->destroy(current_dec->decoder_ctx);
				current_dec->src->destroy(current_dec->src_ctx);
				free(current_dec);
				current_dec = NULL;
			}
			ctx->mediaid = nextid;
			if (ctx->state != STATE_STOP && player.dec != NULL)
			{
				current_dec = player.dec;
				dbg("player: play");
				ctx->state = STATE_PLAY;
				current_dec->decoder->run(current_dec->decoder_ctx, encoder_jitter);
				current_dec->src->run(current_dec->src_ctx, current_dec->decoder->jitter(current_dec->decoder_ctx));
				ctx->media->ops->next(ctx->media->ctx);
			}
			else
				player_state(ctx, STATE_STOP);
			player_event_t *it = ctx->events;
			while (it != NULL)
			{
				it->cb(it->ctx, ctx, ctx->state);
				it = it->next;
			}
		} while (ctx->state != STATE_STOP);
		ctx->media->ops->end(ctx->media->ctx);
	}
	return 0;
}
