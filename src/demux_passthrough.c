/*****************************************************************************
 * demux_passthrough.c
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "player.h"
#include "decoder.h"
#include "event.h"
typedef struct src_s demux_t;
typedef struct src_s src_t;
typedef struct src_ops_s demux_ops_t;
typedef struct src_ops_s src_ops_t;
typedef struct src_ctx_s demux_ctx_t;
typedef struct src_ctx_s src_ctx_t;
struct src_ctx_s
{
	player_ctx_t *ctx;
	decoder_t *estream;
	const char *mime;
	event_listener_t *listener;
};
#define SRC_CTX
#define DEMUX_CTX
#include "src.h"
#include "demux.h"
#include "media.h"
#include "jitter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

static src_ctx_t *demux_init(player_ctx_t *player, const char *protocol, const char *mime)
{
	src_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->mime = utils_mime2mime(mime);
	return ctx;
}

static jitter_t *demux_jitter(src_ctx_t *ctx, jitte_t jitte)
{
	if (ctx->estream == NULL)
		return NULL;
	return ctx->estream->ops->jitter(ctx->estream->ctx, jitte);
}

static int demux_run(src_ctx_t *ctx)
{
	const src_t src = { .ops = demux_passthrough, .ctx = ctx};
	event_new_es_t event = {.pid = 0, .src = &src, .mime = ctx->mime, .jitte = JITTE_HIGH};
	event_decode_es_t event_decode = {.src = &src};
	event_listener_t *listener = ctx->listener;
	while (listener)
	{
		listener->cb(listener->arg, SRC_EVENT_NEW_ES, (void *)&event);
		event_decode.pid = event.pid;
		event_decode.decoder = event.decoder;
		listener->cb(listener->arg, SRC_EVENT_DECODE_ES, (void *)&event_decode);
		listener = listener->next;
	}
	return 0;
}

static const char *demux_mime(src_ctx_t *ctx, int index)
{
	if (index > 0)
		return NULL;
	if (ctx->mime)
		return ctx->mime;
#ifdef DECODER_MAD
	return mime_audiomp3;
#elif defined(DECODER_FLAC)
	return mime_audioflac;
#else
	return mime_audiopcm;
#endif
}

static void demux_eventlistener(src_ctx_t *ctx, event_listener_cb_t cb, void *arg)
{
	event_listener_t *listener = calloc(1, sizeof(*listener));
	listener->cb = cb;
	listener->arg = arg;
	if (ctx->listener == NULL)
		ctx->listener = listener;
	else
	{
		/**
		 * add listener to the end of the list. this allow to call
		 * a new listener with the current event when the function is
		 * called from a callback
		 */
		event_listener_t *previous = ctx->listener;
		while (previous->next != NULL) previous = previous->next;
		previous->next = listener;
	}
}

static int demux_attach(src_ctx_t *ctx, long index, decoder_t *decoder)
{
	if (index > 0)
		return -1;
	dbg("demux: attach");
	ctx->estream = decoder;
	return 0;
}

static decoder_t *demux_estream(src_ctx_t *ctx, long index)
{
	return ctx->estream;
}

static void demux_destroy(src_ctx_t *ctx)
{
	if (ctx->estream != NULL)
	{
		const src_t src = { .ops = demux_passthrough, .ctx = ctx};
		event_end_es_t event = {.pid = 0, .src = &src, .decoder = ctx->estream};
		event_listener_t *listener = ctx->listener;
		while (listener)
		{
			listener->cb(listener->arg, SRC_EVENT_END_ES, (void *)&event);
			listener = listener->next;
		}
		ctx->estream->ops->destroy(ctx->estream->ctx);
	}
	event_listener_t *listener = ctx->listener;
	while (listener)
	{
		event_listener_t *next = listener->next;
		free(listener);
		listener = next;
	}
	free(ctx);
}

const src_ops_t *demux_passthrough = &(src_ops_t)
{
	.name = "demux_passthrough",
	.protocol = "udp",
	.init = demux_init,
	.jitter = demux_jitter,
	.run = demux_run,
	.eventlistener = demux_eventlistener,
	.attach = demux_attach,
	.estream = demux_estream,
	.mime = demux_mime,
	.destroy = demux_destroy,
};
