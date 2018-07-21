/*****************************************************************************
 * cmds_line.c
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
#include <sys/types.h>
#include <sys/ioctl.h>

#include "putv.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

typedef int (*method_t)(mediaplayer_ctx_t *ctx, char *arg);

int method_append(mediaplayer_ctx_t *ctx, char *arg)
{
	dbg("cmdline: insert");
	return media_insert(ctx, arg, NULL, NULL);
}

int method_play(mediaplayer_ctx_t *ctx, char *arg)
{
	dbg("cmdline: play");
	return (player_state(ctx, STATE_PLAY) == STATE_PLAY);
}

int method_pause(mediaplayer_ctx_t *ctx, char *arg)
{
	dbg("cmdline: pause");
	return (player_state(ctx, STATE_PAUSE) == STATE_PAUSE);
}

int method_stop(mediaplayer_ctx_t *ctx, char *arg)
{
	dbg("cmdline: stop");
	return (player_state(ctx, STATE_STOP) == STATE_STOP);
}

int method_next(mediaplayer_ctx_t *ctx, char *arg)
{
	dbg("cmdline: next");
	return media_next(ctx);
}

void cmds_line_onchange(void *info, mediaplayer_ctx_t *ctx)
{
	state_t state = player_state(ctx, STATE_UNKNOWN);
	switch (state)
	{
	case STATE_PLAY:
		printf("player: play\n");
	break;
	case STATE_PAUSE:
		printf("player: pause\n");
	break;
	case STATE_STOP:
		printf("player: stop\n");
	break;
	}
}

int cmds_line_run(mediaplayer_ctx_t *ctx)
{
	int fd = 0;
	int run = 1;

	player_onchange(ctx, cmds_line_onchange, NULL);
	while (run)
	{
		int ret;
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		int maxfd = fd;
		ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (ret > 0 && FD_ISSET(fd, &rfds))
		{
			int length;
			ret = ioctl(fd, FIONREAD, &length);
			char *buffer = malloc(length + 1);
			ret = read(fd, buffer, length);
			int i;
			method_t method = NULL;
			char *arg = NULL;
			for (i = 0; i < length; i++)
			{
				if (buffer[i] == ' ' || buffer[i] == '\t')
					continue;
				if (buffer[i] == '\n')
					break;
				if (method != NULL)
				{
					arg = buffer + i;
					break;
				}
				if (!strncmp(buffer + i, "append", 6))
				{
					method = method_append;
					i += 6;
				}
				if (!strncmp(buffer + i, "play",4))
				{
					method = method_play;
					i += 4;
				}
				if (!strncmp(buffer + i, "pause",5))
				{
					method = method_pause;
					i += 5;
				}
				if (!strncmp(buffer + i, "stop",4))
				{
					method = method_stop;
					i += 4;
				}
				if (!strncmp(buffer + i, "next",4))
				{
					method = method_next;
					i += 4;
				}
				if (!strncmp(buffer + i, "quit",4))
					run = 0;
			}
			if (method)
			{
				char *end = NULL;
				if (arg)
					end = strchr(arg, '\n');
				if (end != NULL)
					*end = '\0';
				method(ctx, arg);
			}
		}
	}
	return 0;
}
