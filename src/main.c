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

#include "player.h"
#include "encoder.h"
#include "sink.h"
#include "media.h"
#include "cmds.h"
#include "../version.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

static int run_player(player_ctx_t *player, jitter_t *sink_jitter)
{
	int ret;
	const encoder_t *encoder;
	encoder_ctx_t *encoder_ctx;
	jitter_t *encoder_jitter = NULL;

	encoder = ENCODER;
	encoder_ctx = encoder->init(player);
	encoder->run(encoder_ctx, sink_jitter);
	encoder_jitter = encoder->jitter(encoder_ctx);

	if (encoder_jitter != NULL)
		ret = player_run(player, encoder_jitter);
	encoder->destroy(encoder_ctx);
	return ret;
}

#define DAEMONIZE 0x01
#define SRC_STDIN 0x02
#define AUTOSTART 0x04
int main(int argc, char **argv)
{
	const char *mediapath = SYSCONFDIR"/putv.db";
	const char *outarg = "default";
	media_ctx_t *media_ctx;
	pthread_t thread;
	const char *root = "/tmp";
	int mode = 0;
	const char *name = "putv";
	
	int opt;
	do
	{
		opt = getopt(argc, argv, "R:m:o:hDVxa");
		switch (opt)
		{
			case 'R':
				root = optarg;
			break;
			case 'm':
				mediapath = optarg;
			break;
			case 'o':
				outarg = optarg;
			break;
			case 'h':
				return -1;
			break;
			case 'x':
				mode |= SRC_STDIN;
			break;
			case 'D':
				mode |= DAEMONIZE;
			break;
			case 'a':
				mode |= AUTOSTART;
			break;
		}
	} while(opt != -1);

	if ((mode & DAEMONIZE) && fork() != 0)
	{
		return 0;
	}

	media_ctx = MEDIA->init(mediapath);
	if (media_ctx == NULL)
	{
		err("media not found %s", mediapath);
		return -1;
	}
	media_t *media = &(media_t)
	{
		.ops = MEDIA,
		.ctx = media_ctx,
	};

	if (mode & AUTOSTART)
		media->ops->options(media->ctx, MEDIA_AUTOSTART, 1);
	if (mode & SRC_STDIN)
	{
		dbg("insert stdin");
		media->ops->insert(media_ctx, "-", NULL, "audio/mp3");
	}

	player_ctx_t *player = player_init(media);

	cmds_t cmds[3];
	int nbcmds = 0;
#ifdef CMDLINE
	cmds[nbcmds].ops = cmds_line;
	cmds[nbcmds].ctx = cmds[nbcmds].ops->init(player, media, NULL);
	nbcmds++;
#endif
#ifdef JSONRPC
	char socketpath[256];
	snprintf(socketpath, sizeof(socketpath) - 1, "%s/%s", root, name);
	cmds[nbcmds].ops = cmds_json;
	cmds[nbcmds].ctx = cmds[nbcmds].ops->init(player, media, (void *)socketpath);
	nbcmds++;
#endif

	int i;
	for (i = 0; i < nbcmds; i++)
		cmds[i].ops->run(cmds[i].ctx);

	const sink_t *sink;
	sink_ctx_t *sink_ctx;
	jitter_t *sink_jitter = NULL;

	sink = SINK;
	sink_ctx = sink->init(player, outarg);
	sink->run(sink_ctx);
	sink_jitter = sink->jitter(sink_ctx);

	run_player(player, sink_jitter);

	sink->destroy(sink_ctx);
	player_destroy(player);

	for (i = 0; i < nbcmds; i++)
	{
		cmds[i].ops->destroy(cmds[i].ctx);
	}
	return 0;
}
