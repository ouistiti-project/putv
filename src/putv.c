/*****************************************************************************
 * putv.c
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
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "putv.h"
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

struct mediaplayer_ctx_s
{
	media_t *media;
	state_t state;
	player_event_t *events;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
};

const char const *mime_mp3 = "audio/mp3";
const char const *mime_octetstream = "octet/stream";

mediaplayer_ctx_t *player_init(media_t *media)
{
	mediaplayer_ctx_t *ctx = calloc(1, sizeof(*ctx));
	pthread_mutex_init(&ctx->mutex, NULL);
	pthread_cond_init(&ctx->cond, NULL);
	ctx->media = media;
	return ctx;
}

void player_destroy(mediaplayer_ctx_t *ctx)
{
	pthread_cond_destroy(&ctx->cond);
	pthread_mutex_destroy(&ctx->mutex);
	free(ctx);
}

void player_onchange(mediaplayer_ctx_t *ctx, player_event_cb_t callback, void *cbctx)
{
	player_event_t *event = calloc(1, sizeof(*event));
	event->cb = callback;
	event->ctx = cbctx;
	event->type = EVENT_ONCHANGE;
	event->next = ctx->events;
	ctx->events = event; 
}

state_t player_state(mediaplayer_ctx_t *ctx, state_t state)
{
	if ((state != STATE_UNKNOWN) && ctx->state != state)
	{
		ctx->state = state;
		pthread_cond_broadcast(&ctx->cond);
		player_event_t *it = ctx->events;
		while (it != NULL)
		{
			it->cb(it->ctx, ctx);
			it = it->next;
		}
	}
	
	return ctx->state;
}

int player_waiton(mediaplayer_ctx_t *ctx, int state)
{
	if (ctx->state == STATE_STOP)
		return -1;
	pthread_mutex_lock(&ctx->mutex);
	while (ctx->state == state)
	{
		pthread_cond_wait(&ctx->cond, &ctx->mutex);
	}
	pthread_mutex_unlock(&ctx->mutex);
	return 0;
}

struct _player_play_t
{
	mediaplayer_ctx_t *ctx;
	jitter_t *jitter_encoder;
};

static int _player_play(void* arg, const char *url, const char *info, const char *mime)
{
	struct _player_play_t *player = (struct _player_play_t *)arg;
	mediaplayer_ctx_t *ctx = player->ctx;
	const decoder_t *decoder = NULL;
	const src_t *src = SRC;
	src_ctx_t *src_ctx = NULL;
	decoder_ctx_t *decoder_ctx = NULL;

	dbg("putv: play %s", url);

#ifdef DECODER_MAD
	if (mime && !strcmp(mime, mime_mp3))
		decoder = decoder_mad;
#endif
	if (decoder == NULL)
		decoder = DECODER;

	if (decoder != NULL)
	{
		decoder_ctx = decoder->init(ctx);
		src_ctx = src->init(ctx, url);
	}
	if (src_ctx != NULL)
	{
		decoder->run(decoder_ctx, player->jitter_encoder);
		src->run(src_ctx, decoder->jitter(decoder_ctx));
		decoder->destroy(decoder_ctx);
		src->destroy(src_ctx);
	}
	return 0;
}

int player_run(mediaplayer_ctx_t *ctx)
{
	const sink_t *sink;
	sink_ctx_t *sink_ctx;
	const encoder_t *encoder;
	encoder_ctx_t *encoder_ctx;

	jitter_t *jitter[3];

	sink = SINK;
	sink_ctx = sink->init(ctx, "default");
	sink->run(sink_ctx);
	jitter[0] = sink->jitter(sink_ctx);

	encoder = ENCODER;
	encoder_ctx = encoder->init(ctx);
	encoder->run(encoder_ctx,jitter[0]);
	jitter[1] = encoder->jitter(encoder_ctx);

	while (ctx->state != STATE_ERROR)
	{
		pthread_mutex_lock(&ctx->mutex);
		while (ctx->state == STATE_STOP)
		{
			dbg("putv: stop");
			pthread_cond_wait(&ctx->cond, &ctx->mutex);
		}
		pthread_mutex_unlock(&ctx->mutex);

		jitter[0]->ops->reset(jitter[0]->ctx);
		jitter[1]->ops->reset(jitter[1]->ctx);
		struct _player_play_t player =
		{
			.ctx = ctx,
			.jitter_encoder = jitter[1],
		};
		if (ctx->media->ops->next(ctx->media->ctx) == -1)
			ctx->state = STATE_STOP;
		else
			ctx->media->ops->play(ctx->media->ctx, _player_play, &player);
	}
	encoder->destroy(encoder_ctx);
	sink->destroy(sink_ctx);
	pthread_exit(0);
	return 0;
}
