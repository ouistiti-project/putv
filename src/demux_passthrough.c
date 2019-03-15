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
typedef struct demux_s demux_t;
typedef struct demux_ops_s demux_ops_t;
typedef struct demux_ctx_s demux_ctx_t;
struct demux_ctx_s
{
	player_ctx_t *ctx;
	jitter_t *inout;
	decoder_t *estream;
	const char *mime;
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
	return ctx;
}

static jitter_t *demux_jitter(demux_ctx_t *ctx)
{
	return ctx->estream->ops->jitter(ctx->estream->ctx);
}

static int demux_run(demux_ctx_t *ctx)
{
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

const demux_ops_t *demux_passthrough = &(demux_ops_t)
{
	.init = demux_init,
	.jitter = demux_jitter,
	.run = demux_run,
	.attach = demux_attach,
	.estream = demux_estream,
	.mime = demux_mime,
	.destroy = demux_destroy,
};
