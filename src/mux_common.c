/*****************************************************************************
 * mux_mad.c
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
#include "mux.h"
#include "filter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define mux_dbg(...)

mux_t *mux_build(player_ctx_t *player, const char *protocol)
{
	mux_t *mux = NULL;
	const mux_ops_t *ops = NULL;
	mux_ctx_t *ctx = NULL;
#ifdef MUX_RTP
	if (mime && !strcmp(mime, mux_rtp->protocol(NULL)))
	{
		ops = mux_rtp;
	}
	else
#endif
#ifdef MUX_DVB
	if (mime && !strcmp(mime, mux_dvb->protocol(NULL)))
	{
		ops = mux_dvb;
	}
	else
#endif
		ops = mux_passthrough;

	if (ops != NULL)
	{
		ctx = ops->init(player, "");
	}
	if (ctx != NULL)
	{
		mux = calloc(1, sizeof(*mux));
		mux->ops = ops;
		mux->ctx = ctx;
	}
	return mux;
}
