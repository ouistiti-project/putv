/*****************************************************************************
 * demux_rtp.c
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

#include <pthread.h>

#include "player.h"
#include "decoder.h"
#include "event.h"
typedef struct demux_s demux_t;
typedef struct demux_ops_s demux_ops_t;

#define NB_LOOPS 5
#define NB_BUFFERS 4
#define BUFFERSIZE 1500

typedef struct demux_reorder_s demux_reorder_t;
struct demux_reorder_s
{
	char buffer[BUFFERSIZE];
	size_t len;
	char id;
	char ready;
};

typedef struct demux_out_s demux_out_t;
struct demux_out_s
{
	decoder_t *estream;
	uint32_t ssrc;
	jitter_t *jitter;
	char *data;
	const char *mime;
	short cc;
	demux_out_t *next;
};

typedef struct demux_ctx_s demux_ctx_t;
struct demux_ctx_s
{
	demux_out_t *out;
	jitter_t *in;
	demux_reorder_t reorder[NB_BUFFERS];
	const char *mime;
	pthread_t thread;
	struct
	{
		event_listener_t cb;
		void *arg;
	} listener;
};
#define DEMUX_CTX
#include "demux.h"
#include "media.h"
#include "jitter.h"
#include "rtp.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

static const char *jitter_name = "rtp demux";

static demux_ctx_t *demux_init(player_ctx_t *player, const char *search)
{
	char *mime = NULL;
	if (search != NULL)
	{
		mime = strstr(search, "mime=");
		if (mime != NULL)
		{
			mime += 5;
		}
	}

	demux_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->mime = utils_mime2mime(mime);
	ctx->in = jitter_scattergather_init(jitter_name, 6, BUFFERSIZE);
	ctx->in->format = SINK_BITSSTREAM;
	ctx->in->ctx->thredhold = 3;
	return ctx;
}

static jitter_t *demux_jitter(demux_ctx_t *ctx)
{
	return ctx->in;
}

int demux_parseheader(demux_ctx_t *ctx, char *input, size_t len)
{
	rtpheader_t *header = (rtpheader_t *)input;
#ifdef DEBUG_0
	warn("rtp header:");
	warn("\ttimestamp:\t%d", header->timestamp);
	warn("\tseq:\t%d", header->b.seqnum);
	warn("\tssrc:\t%d", header->ssrc);
	warn("\ttype:\t%d", header->b.pt);
	warn("\tnb csrc:\t%d", header->b.cc);
	warn("\tpadding:\t%d", header->b.p);
#endif
	demux_out_t *out = ctx->out;
	while (out != NULL && out->ssrc != header->ssrc)
		out = out->next;
	if (out == NULL)
	{
		out = calloc(1, sizeof(*out));
		out->ssrc = header->ssrc;
		out->cc = header->b.cc;
		out->mime = mime_octetstream;
		if (header->b.pt == 14)
		{
			out->mime = mime_audiomp3;
		}
		if (header->b.pt == 11)
		{
			out->mime = mime_audiopcm;
		}
		out->next = ctx->out;
		ctx->out = out;
		const event_new_es_t event = {.pid = out->ssrc, .mime = out->mime};
		ctx->listener.cb(ctx->listener.arg, SRC_EVENT_NEW_ES, (void *)&event);
	}
#ifdef DEMUX_RTC_REORDER
	demux_reorder_t *reorder = &ctx->reorder[header->b.seqnum % NB_BUFFERS];
	int id = header->b.seqnum % (NB_BUFFERS * NB_LOOPS);

	if (out->jitter != NULL && reorder->ready && reorder->id != id)
	{
		if (out->data == NULL)
			out->data = out->jitter->ops->pull(out->jitter->ctx);
		char *buffer = reorder->buffer;
		size_t len = reorder->len;
		while (len > out->jitter->ctx->size)
		{
			memcpy(out->data, buffer, out->jitter->ctx->size);
			out->jitter->ops->push(out->jitter->ctx, out->jitter->ctx->size, NULL);
			len -= out->jitter->ctx->size;
			buffer += out->jitter->ctx->size;
		}
		memcpy(out->data, buffer, len);
		out->jitter->ops->push(out->jitter->ctx, len, NULL);
		out->data = NULL;
		reorder->ready = 0;
	}
	if (!reorder->ready)
	{
		input += sizeof(*header);
		len -= sizeof(*header);
		if (header->b.cc)
		{
			warn("CSCR:");
			input += header->b.cc * sizeof(uint32_t);
			len -= header->b.cc * sizeof(uint32_t);
		}
		if (header->b.x)
		{
			warn("rtp extension:");
			uint16_t *extid = (uint16_t *)input;
			input += sizeof(uint16_t);
			len -= sizeof(uint16_t);
			uint16_t *extlength = (uint16_t *)input;
			input += sizeof(uint16_t);
			len -= sizeof(uint16_t);

			input += *extlength;
			len -= *extlength;
		}
		if (len > 0)
		{
			memcpy(reorder->buffer, input, len);
			reorder->ready = 1;
			reorder->id = id;
		}
	}
#else
	if (out->jitter != NULL)
	{
		if (out->data == NULL)
			out->data = out->jitter->ops->pull(out->jitter->ctx);
		while (len > out->jitter->ctx->size)
		{
			memcpy(out->data, input, out->jitter->ctx->size);
			out->jitter->ops->push(out->jitter->ctx, out->jitter->ctx->size, NULL);
			len -= out->jitter->ctx->size;
			input += out->jitter->ctx->size;
		}
		memcpy(out->data, input, len);
		out->jitter->ops->push(out->jitter->ctx, len, NULL);
		out->data = NULL;
	}
	else
		exit(0);
#endif
	return len;
}

static void *demux_thread(void *arg)
{
	demux_ctx_t *ctx = (demux_ctx_t *)arg;
	int run = 1;
	do
	{
		char *input;
		size_t len = 0;
		input = ctx->in->ops->peer(ctx->in->ctx, NULL);
		if (input == NULL)
		{
			run = 0;
		}
		else
		{
			len = ctx->in->ops->length(ctx->in->ctx);
			if ( demux_parseheader(ctx, input, len) < 0)
				run = 0;
		}
		ctx->in->ops->pop(ctx->in->ctx, len);
	} while (run);
	demux_out_t *out = ctx->out;
	while (out != NULL)
	{
		if (out->data != NULL)
			out->jitter->ops->push(out->jitter->ctx, 0, NULL);
		out = out->next;
	}
	return NULL;
}

static int demux_run(demux_ctx_t *ctx)
{
	pthread_create(&ctx->thread, NULL, demux_thread, ctx);
	return 0;
}

static const char *demux_mime(demux_ctx_t *ctx, int index)
{
	if (ctx->mime)
		return ctx->mime;
	demux_out_t *out = ctx->out;
	while (out != NULL && index > 0)
	{
		out = out->next;
		index--;
	}
	if (ctx->out != NULL)
		return ctx->out->mime;
#ifdef DECODER_MAD
	return mime_audiomp3;
#elif defined(DECODER_FLAC)
	return mime_audioflac;
#else
	return mime_audiopcm;
#endif
}

static void demux_eventlistener(demux_ctx_t *ctx, event_listener_t listener, void *arg)
{
	ctx->listener.cb = listener;
	ctx->listener.arg = arg;
}

static int demux_attach(demux_ctx_t *ctx, long index, decoder_t *decoder)
{
	demux_out_t *out = ctx->out;
	while (out != NULL && out->ssrc != (uint32_t)index)
	{
		out = out->next;
	}
	if (out != NULL)
	{
		out->estream = decoder;
		out->jitter = out->estream->ops->jitter(out->estream->ctx);
	}
}

static decoder_t *demux_estream(demux_ctx_t *ctx, long index)
{
	demux_out_t *out = ctx->out;
	while (out != NULL)
	{
		if (index == out->ssrc)
			return out->estream;
		out = out->next;
	}
	return NULL;
}

static void demux_destroy(demux_ctx_t *ctx)
{
	demux_out_t *out = ctx->out;
	while (out != NULL)
	{
		demux_out_t *old = out;
		out = out->next;
		free(old);
	}
	free(ctx);
}

const demux_ops_t *demux_rtp = &(demux_ops_t)
{
	.init = demux_init,
	.jitter = demux_jitter,
	.run = demux_run,
	.eventlistener = demux_eventlistener,
	.attach = demux_attach,
	.estream = demux_estream,
	.mime = demux_mime,
	.destroy = demux_destroy,
};
