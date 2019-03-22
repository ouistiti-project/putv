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
typedef struct demux_s demux_t;
typedef struct demux_ops_s demux_ops_t;

typedef struct demux_out_s demux_out_t;
struct demux_out_s
{
	decoder_t *estream;
	int ssrc;
	jitter_t *jitter;
	char *data;
	const char *mime;
};

typedef struct demux_ctx_s demux_ctx_t;
struct demux_ctx_s
{
	demux_out_t out[1];
	int outn;
	int outlast;
	jitter_t *in;
	const char *mime;
	pthread_t thread;
};
#define DEMUX_CTX
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

struct rtpbits {
    int     sequence:16;     // sequence number: random
    int     pt:7;            // payload type: 14 for MPEG audio
    int     m:1;             // marker: 0
    int     cc:4;            // number of CSRC identifiers: 0
    int     x:1;             // number of extension headers: 0
    int     p:1;             // is there padding appended: 0
    int     v:2;             // version: 2
};

struct rtpheader {           // in network byte order
    struct rtpbits b;
    int     timestamp;       // start: random
    int     ssrc;            // random
    int     iAudioHeader;    // =0?!
};

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
	ctx->in = jitter_scattergather_init(jitter_name, 6, 1500);
	ctx->in->format = SINK_BITSSTREAM;
	ctx->in->ctx->thredhold = 3;
	return ctx;
}

static jitter_t *demux_jitter(demux_ctx_t *ctx)
{
	return ctx->in;
}

int demux_parseheader(demux_ctx_t *ctx, char *input, int len)
{
	struct rtpheader *header = (struct rtpheader *)input;
#ifdef DEBUG
	warn("rtp header:");
	warn("\theader:\t%d", header->iAudioHeader);
	warn("\ttimestamp:\t%d", header->timestamp);
	warn("\tseq:\t%d", header->b.sequence);
	warn("\tssrc:\t%d", header->ssrc);
	warn("\ttype:\t%d", header->b.pt);
	warn("\tnb counter:\t%d", header->b.cc);
	warn("\tpadding:\t%d", header->b.p);
#endif
	if (header->b.pt == 14)
	{
		demux_out_t *out = &ctx->out[ctx->outn];
		int ssrc = header->ssrc;
		if (out->ssrc == 0)
		{
			out->ssrc = ssrc;
			out->mime = mime_audiomp3;
		}
		if (out->ssrc != ssrc)
			return 0;
		if (out->data == NULL)
			out->data = out->jitter->ops->pull(out->jitter->ctx);
		len -= sizeof(*header);
		input += sizeof(*header);
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
		return len;
	}
	return 0;
}

static void *demux_thread(void *arg)
{
	demux_ctx_t *ctx = (demux_ctx_t *)arg;
	int run = 1;
	do
	{
		char *input;
		demux_out_t *out = &ctx->out[ctx->outn];
		input = ctx->in->ops->peer(ctx->in->ctx);
		if (input == NULL)
		{
			run = 0;
		}
		else
		{
			int len;
			len = ctx->in->ops->length(ctx->in->ctx);
			if ( demux_parseheader(ctx, input, len) < 0)
				run = 0;
		}
		ctx->in->ops->pop(ctx->in->ctx, len);
	} while (run);
	if (out->data != NULL)
		out->jitter->ops->push(out->jitter->ctx, 0, NULL);
	return NULL;
}

static int demux_run(demux_ctx_t *ctx)
{
	pthread_create(&ctx->thread, NULL, demux_thread, ctx);
	return 0;
}

static const char *demux_mime(demux_ctx_t *ctx, int index)
{
	if (index > 0)
		return NULL;
	if (ctx->mime)
		return ctx->mime;
	if (ctx->out[index].mime)
		return ctx->out[index].mime;
#ifdef DECODER_MAD
	return mime_audiomp3;
#elif defined(DECODER_FLAC)
	return mime_audioflac;
#else
	return mime_audiopcm;
#endif
}

static int demux_attach(demux_ctx_t *ctx, int index, decoder_t *decoder)
{
	if (index > 0)
		return -1;
	demux_out_t *out = &ctx->out[index];
	out->estream = decoder;
	out->jitter = out->estream->ops->jitter(out->estream->ctx);
	if (index >= ctx->outlast)
		ctx->outlast = index + 1;
}

static decoder_t *demux_estream(demux_ctx_t *ctx, int index)
{
	if (index < ctx->outlast)
		return ctx->out[index].estream;
}

static void demux_destroy(demux_ctx_t *ctx)
{
	free(ctx);
}

const demux_ops_t *demux_rtp = &(demux_ops_t)
{
	.init = demux_init,
	.jitter = demux_jitter,
	.run = demux_run,
	.attach = demux_attach,
	.estream = demux_estream,
	.mime = demux_mime,
	.destroy = demux_destroy,
};
