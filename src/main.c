/*****************************************************************************
 * main.c
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
#include <unistd.h>

#include <pthread.h>

#include "putv.h"
#include "cmds_line.h"
#include "cmds_json.h"
#include "../version.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

void *player_thread(void *arg)
{
	int ret;
	ret = player_run((mediaplayer_ctx_t*)arg);
	player_destroy((mediaplayer_ctx_t*)arg);
	return (void *)ret;
}

#define DAEMONIZE 0x01
#define SRC_STDIN 0x02
int main(int argc, char **argv)
{
	char socketpath[256];
	const char *dbpath = SYSCONFDIR"/putv.db";
	mediaplayer_ctx_t *ctx;
	pthread_t thread;
	const char *root = "/tmp";
	int mode = 0;
	const char *name = "putv";
	
	int opt;
	do
	{
		opt = getopt(argc, argv, "R:d:hDVx");
		switch (opt)
		{
			case 'R':
				root = optarg;
			break;
			case 'd':
				dbpath = optarg;
			break;
			case 'h':
				return -1;
			break;
			case 'x':
				mode = SRC_STDIN;
			break;
			case 'D':
				mode = DAEMONIZE;
			break;
		}
	} while(opt != -1);

	dbg("mode %x", mode);

	if ((mode & DAEMONIZE) && fork() != 0)
	{
		return 0;
	}

	ctx = player_init(dbpath);

	if (mode & SRC_STDIN)
	{
		dbg("insert stdin");
		media_insert(ctx, "-", NULL, "audio/mp3");
	}
	pthread_create(&thread, NULL, player_thread, (void *)ctx);

	snprintf(socketpath, sizeof(socketpath) - 1, "%s/%s", root, name);
#ifdef CMDLINE
	cmds_line_run(ctx);
#endif
#ifdef JSONRPC
	cmds_json_run(ctx, socketpath);
#endif
	dbg("application ended");
	return 0;
}
