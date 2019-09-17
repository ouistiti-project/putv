/*****************************************************************************
 * mux_passthrough.c
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

#include "../config.h"
#include "player.h"
#include "decoder.h"
#include "rtp.h"
typedef struct mux_s mux_t;
typedef struct mux_ops_s mux_ops_t;
typedef struct mux_ctx_s mux_ctx_t;
struct mux_ctx_s
{
	player_ctx_t *ctx;
	jitter_t *in;
	jitter_t *out;
	rtpheader_t header;
	pthread_t thread;
	const char *mime;
};
#define MUX_CTX
#include "mux.h"
#include "media.h"
#include "jitter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define mux_dbg(...)

static const char *jitter_name = "rtp muxer";
static mux_ctx_t *mux_init(player_ctx_t *player, const char *mime)
{
	mux_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->mime = mime;

	ctx->header.b.v = 2;
	ctx->header.b.p = 0;
	ctx->header.b.x = 0;
	ctx->header.b.cc = 0;
	ctx->header.b.m = 1;
	if (mime == mime_audiomp3)
		ctx->header.b.pt = 14;
	if (mime == mime_audiopcm)
		ctx->header.b.pt = 11;
	if (mime == mime_audioalac)
		ctx->header.b.pt = 46;
	ctx->header.b.seqnum = random();
	ctx->header.timestamp = random();
	ctx->header.ssrc = random();

	return ctx;
}

static jitter_t *mux_jitter(mux_ctx_t *ctx, int index)
{
	return ctx->in;
}

static void *mux_thread(void *arg)
{
	int result = 0;
	int run = 1;
	mux_ctx_t *ctx = (mux_ctx_t *)arg;
	heartbeat_t *heart = ctx->in->ops->heartbeat(ctx->in->ctx, NULL);
	if (heart != NULL)
		ctx->out->ops->heartbeat(ctx->out->ctx, heart);
	while (run)
	{
		void *beat = NULL;
		char *inbuffer;
		if (heart)
			inbuffer = ctx->in->ops->peer(ctx->in->ctx, &beat);
		else
			inbuffer = ctx->in->ops->peer(ctx->in->ctx, NULL);
		unsigned long inlength = ctx->in->ops->length(ctx->in->ctx);
		if (inbuffer != NULL)
		{
			int len = sizeof(ctx->header);
			char *outbuffer = ctx->out->ops->pull(ctx->out->ctx);

			mux_dbg("mux: rtp seqnum %d", ctx->header.b.seqnum);
			memcpy(outbuffer, &ctx->header, len);
			ctx->header.b.m = 0;
			ctx->header.b.seqnum++;
			if (ctx->header.b.seqnum == UINT16_MAX)
			{
				ctx->header.b.seqnum = 0;
				ctx->header.b.m = 1;
			}
#ifdef DEBUG_0
			int i;
			fprintf(stderr, "header: ");
			for (i = 0; i < len; i++)
				fprintf(stderr, "%.2x ", outbuffer[i]);
			fprintf(stderr, "\n");
#endif
			memcpy(outbuffer + len, inbuffer, inlength);
			len += inlength;

			ctx->out->ops->push(ctx->out->ctx, len, beat);
			ctx->in->ops->pop(ctx->in->ctx, inlength);
		}
	}
	return (void *)(intptr_t)result;
}

static int mux_run(mux_ctx_t *ctx, jitter_t *sink_jitter)
{
	int size = sink_jitter->ctx->size - sizeof(rtpheader_t) - sizeof(uint32_t);
	jitter_t *jitter = jitter_scattergather_init(jitter_name, 6, size);
	jitter->ctx->frequence = 0;
	jitter->ctx->thredhold = 3;
#if defined(MUX_RTP_MP3)
	jitter->format = MPEG2_3_MP3;
#elif defined(MUX_RTP_PCM)
	jitter->format = PCM_16bits_LE_mono;
#else
	jitter->format = SINK_BITSSTREAM;
#endif
	ctx->in = jitter;

	ctx->out = sink_jitter;
	pthread_create(&ctx->thread, NULL, mux_thread, ctx);
	return 0;
}

static int mux_attach(mux_ctx_t *ctx, const char *mime)
{
	ctx->mime = mime;
	if (mime == mime_audiomp3)
		ctx->header.b.pt = 14;
	if (mime == mime_audiopcm)
		ctx->header.b.pt = 11;
	if (mime == mime_audioalac)
		ctx->header.b.pt = 46;
	return 0;
}

static const char *mux_mime(mux_ctx_t *ctx, int index)
{
	if (index > 0)
		return NULL;
	return ctx->mime;
}

static void mux_destroy(mux_ctx_t *ctx)
{
	if (ctx->thread)
		pthread_join(ctx->thread, NULL);
	jitter_scattergather_destroy(ctx->in);
	free(ctx);
}

const mux_ops_t *mux_rtp = &(mux_ops_t)
{
	.init = mux_init,
	.jitter = mux_jitter,
	.run = mux_run,
	.attach = mux_attach,
	.mime = mux_mime,
	.protocol = "rtp",
	.destroy = mux_destroy,
};
