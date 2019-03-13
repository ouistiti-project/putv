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
typedef struct demux_ctx_s demux_ctx_t;
struct demux_ctx_s
{
	decoder_t *estream;
	jitter_t *out;
	char *output;
	jitter_t *in;
	char *mime;
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
	return ctx;
}

static jitter_t *demux_jitter(demux_ctx_t *ctx)
{
	return ctx->in;
}

int demux_parseheader(demux_ctx_t *ctx, char *input)
{
	struct rtpheader *header = (struct rtpheader *)input;
	if (header->b.pt == 14)
		return sizeof(*header);
	return 0;
}

static void *demux_thread(void *arg)
{
	demux_ctx_t *ctx = (demux_ctx_t *)arg;
	int run = 1;
	do
	{
		char *input;
		input = ctx->in->ops->peer(ctx->in->ctx);
		if (ctx->output == NULL)
			ctx->output = ctx->out->ops->pull(ctx->out->ctx);
		if (input == NULL)
		{
			run = 0;
			ctx->out->ops->push(ctx->out->ctx, 0, NULL);
			break;
		}

		int len;
		len = ctx->in->ops->length(ctx->in->ctx);
		int headerlen;
		headerlen = demux_parseheader(ctx, input);
		if (headerlen > 0)
		{
			memcpy(ctx->output, input + headerlen, len - headerlen);
			ctx->out->ops->push(ctx->out->ctx, len - headerlen, NULL);
			ctx->output = NULL;
		}
		ctx->in->ops->pop(ctx->in->ctx, len);
	} while (run);
	return NULL;
}

static int demux_run(demux_ctx_t *ctx)
{
	ctx->out = ctx->estream->ops->jitter(ctx->estream->ctx);
	pthread_create(&ctx->thread, NULL, demux_thread, ctx);
	return 0;
}

static const char *demux_mime(demux_ctx_t *ctx, int index)
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

static int demux_attach(demux_ctx_t *ctx, int index, decoder_t *decoder)
{
	if (index > 0)
		return -1;
	ctx->estream = decoder;
}

static decoder_t *demux_estream(demux_ctx_t *ctx, int index)
{
	return ctx->estream;
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
