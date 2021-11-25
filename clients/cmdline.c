/*****************************************************************************
 * input.c
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
#include <string.h>
#include <fcntl.h>

#include <pthread.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef USE_INOTIFY
#include <sys/inotify.h>
#endif

#include <libgen.h>
#include <jansson.h>
#include <linux/input.h>
#ifdef USE_LIBINPUT
#include <libinput.h>
#endif

#include "client_json.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

typedef void *(*__start_routine_t) (void *);

typedef struct cmdline_ctx_s cmdline_ctx_t;
struct cmdline_ctx_s
{
	int inotifyfd;
	int dirfd;
	const char *root;
	const char *name;
	const char *cmdline_path;
	json_t *media;
	int media_id;
	int inputfd;
	char *socketpath;
	client_data_t *client;
	char run;
	enum
	{
		STATE_UNKNOWN,
		STATE_PLAY,
		STATE_PAUSE,
		STATE_STOP,
	} state;
};

typedef int (*method_t)(cmdline_ctx_t *ctx, char *arg);

#ifdef USE_LIBINPUT
static int open_restricted(const char *path, int flags, void *user_data)
{
        int fd = open(path, flags);
        return fd < 0 ? -errno : fd;
}
static void close_restricted(int fd, void *user_data)
{
        close(fd);
}
const static struct libcmdline_interface interface = {
        .open_restricted = open_restricted,
        .close_restricted = close_restricted,
};
#endif

int cmdline_checkstate(void *data, json_t *params)
{
	cmdline_ctx_t *ctx = (cmdline_ctx_t *)data;
	const char *state;
	json_unpack(params, "{ss}", "state", &state);
	if (!strcmp(state, "play"))
		ctx->state = STATE_PLAY;
	else if (!strcmp(state, "pause"))
		ctx->state = STATE_PAUSE;
	else if (!strcmp(state, "stop"))
		ctx->state = STATE_STOP;
	else
		ctx->state = STATE_UNKNOWN;
	return 0;
}

static int method_next(cmdline_ctx_t *ctx, char *arg)
{
	return client_next(ctx->client, cmdline_checkstate, ctx);
}

static int method_play(cmdline_ctx_t *ctx, char *arg)
{
	return client_play(ctx->client, cmdline_checkstate, ctx);
}

static int method_pause(cmdline_ctx_t *ctx, char *arg)
{
	return client_pause(ctx->client, cmdline_checkstate, ctx);
}

static int method_stop(cmdline_ctx_t *ctx, char *arg)
{
	return client_stop(ctx->client, cmdline_checkstate, ctx);
}

static int method_volume(cmdline_ctx_t *ctx, char *arg)
{
	int ret = -1;
	unsigned int volume = atoi(arg);
	if (volume != -1)
	{
		ret = client_volume(ctx->client, NULL, ctx, json_integer(volume));
	}
	return ret;
}

static int method_media(cmdline_ctx_t *ctx, char *arg)
{
	int ret = -1;
	json_error_t error;
	json_t *media = json_loads(arg, 0, &error);
	ret = media_change(ctx->client, NULL, ctx, media);
	return ret;
}

static int method_loop(cmdline_ctx_t *ctx, char *arg)
{
	int ret = -1;
	return ret;
}

static int method_random(cmdline_ctx_t *ctx, char *arg)
{
	int ret = -1;
	return ret;
}

static int method_quit(cmdline_ctx_t *ctx, char *arg)
{
	client_disconnect(ctx->client);
	ctx->run = 0;
	return 0;
}

static int method_help(cmdline_ctx_t *ctx, char *arg)
{
	fprintf(stdout, "putv commands:\n");
	fprintf(stdout, " play   : start or resume the stream on putv server\n");
	fprintf(stdout, " stop   : stop the stream on putv server\n");
	fprintf(stdout, " pause  : suspend the stream on putv server\n");
	fprintf(stdout, " next   : request the next source on putv server\n");
	fprintf(stdout, " volume : request to change the level of volume on putv server\n");
	fprintf(stdout, "        <0..100>\n");
	fprintf(stdout, " media  : request to change the media\n");
	fprintf(stdout, "        <media url>\n");
	return 0;
}

int run_client(void *arg)
{
	cmdline_ctx_t *ctx = (cmdline_ctx_t *)arg;

	client_data_t data = {0};
	client_unix(ctx->socketpath, &data);
	client_async(&data, 1);
	ctx->client = &data;

	pthread_t thread;
	pthread_create(&thread, NULL, (__start_routine_t)client_loop, (void *)&data);

	int fd = 0;
	while (ctx->run)
	{
		int ret;
		fd_set rfds;
		struct timeval timeout = {1, 0};

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		int maxfd = fd;
		ret = select(maxfd + 1, &rfds, NULL, NULL, &timeout);
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
				if (!strncmp(buffer + i, "volume",6))
				{
					method = method_volume;
					i += 6;
				}
				if (!strncmp(buffer + i, "loop",4))
				{
					method = method_loop;
					i += 4;
				}
				if (!strncmp(buffer + i, "random",6))
				{
					method = method_random;
					i += 6;
				}
				if (!strncmp(buffer + i, "quit",4))
				{
					method = method_quit;
					i += 4;
				}
				if (!strncmp(buffer + i, "help", 4))
				{
					method = method_help;
					i += 4;
				}
				if (!strncmp(buffer + i, "append", 6))
				{
					i += 6;
				}
				if (!strncmp(buffer + i, "remove", 6))
				{
					i += 6;
				}
				if (!strncmp(buffer + i, "list",4))
				{
					i += 4;
				}
				if (!strncmp(buffer + i, "media",5))
				{
					method = method_media;
					i += 5;
				}
				if (!strncmp(buffer + i, "import",6))
				{
					i += 6;
				}
				if (!strncmp(buffer + i, "search",6))
				{
					i += 6;
				}
				if (!strncmp(buffer + i, "info",4))
				{
					i += 4;
				}
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

	pthread_join(thread, NULL);
	return 0;
}

#ifdef USE_INOTIFY
#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))

static void *_check_socket(void *arg)
{
	cmdline_ctx_t *ctx = (cmdline_ctx_t *)arg;
	ctx->socketpath = malloc(strlen(ctx->root) + 1 + strlen(ctx->name) + 1);
	sprintf(ctx->socketpath, "%s/%s", ctx->root, ctx->name);
	if (!access(ctx->socketpath, R_OK | W_OK))
	{
		run_client((void *)ctx);
	}
	while (ctx->run)
	{
		char buffer[BUF_LEN];
		int i = 0;
		int length = read(ctx->inotifyfd, buffer, BUF_LEN);

		if (length < 0)
		{
			err("read");
		}

		while (i < length)
		{
			struct inotify_event *event =
				(struct inotify_event *) &buffer[i];
			if (event->len)
			{
				if (event->mask & IN_CREATE)
				{
					if (!access(ctx->socketpath, R_OK | W_OK))
					{
						run_client((void *)ctx);
					}
				}
#if 0
				else if (event->mask & IN_DELETE)
				{
				}
				else if (event->mask & IN_MODIFY)
				{
					dbg("The file %s was modified.", event->name);
				}
#endif
			}
			i += EVENT_SIZE + event->len;
		}
	}
	free(ctx->socketpath);
}
#endif

#define DAEMONIZE 0x01
int main(int argc, char **argv)
{
	int mode = 0;
	cmdline_ctx_t cmdline_data = {
		.root = "/tmp",
		.name = "putv",
	};
	const char *media_path;

	int opt;
	do
	{
		opt = getopt(argc, argv, "R:n:i:m:hD");
		switch (opt)
		{
			case 'R':
				cmdline_data.root = optarg;
			break;
			case 'n':
				cmdline_data.name = optarg;
			break;
			case 'i':
				cmdline_data.cmdline_path = optarg;
			break;
			case 'm':
				media_path = optarg;
			break;
			case 'h':
				fprintf(stderr, "input -R <dir> -n <socketname> -D -m <jsonfile>");
				fprintf(stderr, "send events from input to putv applications\n");
				fprintf(stderr, " -D         daemonize");
				fprintf(stderr, " -R <DIR>   change the socket directory directory");
				fprintf(stderr, " -n <NAME>  change the socket name");
				fprintf(stderr, " -m <FILE>  load Json file to mqnqge media");
				return -1;
			break;
			case 'D':
				mode |= DAEMONIZE;
			break;
		}
	} while(opt != -1);

	if ((mode & DAEMONIZE) && fork() != 0)
	{
		return 0;
	}

	json_error_t error;
	json_t *media;
	media = json_load_file(media_path, 0, &error);
	if (json_is_object(media))
	{
		media = json_object_get(media, "media");
	}
	if (! json_is_array(cmdline_data.media))
	{
		json_decref(cmdline_data.media);
		cmdline_data.media = NULL;
	}
	cmdline_data.media = media;
#ifdef USE_INOTIFY
	cmdline_data.inotifyfd = inotify_init();
	int dirfd = inotify_add_watch(cmdline_data.inotifyfd, cmdline_data.root,
					IN_MODIFY | IN_CREATE | IN_DELETE);
	cmdline_data.run = 1;
	_check_socket((void *)&cmdline_data);
#else
	cmdline_data.socketpath = malloc(strlen(cmdline_data.root) + 1 + strlen(cmdline_data.name) + 1);
	sprintf(cmdline_data.socketpath, "%s/%s", cmdline_data.root, cmdline_data.name);

	run_client((void *)&cmdline_data);
	free(cmdline_data.socketpath);
#endif
	json_decref(cmdline_data.media);
	return 0;
}
