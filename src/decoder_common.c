/*****************************************************************************
 * decoder_mad.c
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>

#include "player.h"
#include "decoder.h"
#include "filter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define decoder_dbg(...)

static const decoder_ops_t * decoderslist [10];

decoder_t *decoder_build(player_ctx_t *player, const char *mime)
{
	decoder_t *decoder = NULL;
	const decoder_ops_t *ops = decoderslist[0];
	decoder_ctx_t *ctx = NULL;
	if (mime)
	{
		while (ops != NULL)
		{
			if (!strcmp(mime, ops->mime(NULL)))
				break;
			ops++;
		}
	}

	if (ops != NULL)
	{
		ctx = ops->init(player);
	}
	if (ctx != NULL)
	{
		warn("new decoder for %s", ops->mime(NULL));
		decoder = calloc(1, sizeof(*decoder));
		decoder->ops = ops;
		decoder->ctx = ctx;
	}
	return decoder;
}

static void _decoder_init(void) __attribute__((constructor));

static void _decoder_init(void)
{
	const decoder_ops_t *decoders[] = {
#ifdef DECODER_MAD
		decoder_mad,
#endif
#ifdef DECODER_FLAC
		decoder_flac,
#endif
#ifdef DECODER_PASSTHROUGH
		decoder_passthrough,
#endif
		NULL
	};

	const decoder_ops_t *ops = decoders[0];
	int i;
	for (i = 0; i < 10 && ops != NULL; i++)
	{
		decoderslist[i] = ops;
		ops ++;
	}
}
