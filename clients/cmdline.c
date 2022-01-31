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

typedef int (*method_t)(cmdline_ctx_t *ctx, const char *arg);

static int method_append(cmdline_ctx_t *ctx, const char *arg);
static int method_update(cmdline_ctx_t *ctx, const char *arg);
static int method_remove(cmdline_ctx_t *ctx, const char *arg);
static int method_list(cmdline_ctx_t *ctx, const char *arg);
static int method_filter(cmdline_ctx_t *ctx, const char *arg);
static int method_media(cmdline_ctx_t *ctx, const char *arg);
static int method_import(cmdline_ctx_t *ctx, const char *arg);
static int method_search(cmdline_ctx_t *ctx, const char *arg);
static int method_info(cmdline_ctx_t *ctx, const char *arg);
static int method_play(cmdline_ctx_t *ctx, const char *arg);
static int method_pause(cmdline_ctx_t *ctx, const char *arg);
static int method_stop(cmdline_ctx_t *ctx, const char *arg);
static int method_next(cmdline_ctx_t *ctx, const char *arg);
static int method_volume(cmdline_ctx_t *ctx, const char *arg);
static int method_repeat(cmdline_ctx_t *ctx, const char *arg);
static int method_shuffle(cmdline_ctx_t *ctx, const char *arg);
static int method_quit(cmdline_ctx_t *ctx, const char *arg);
static int method_help(cmdline_ctx_t *ctx, const char *arg);

struct cmd_s {
	const char *name;
	method_t method;
};
static const struct cmd_s cmds[] = {{
		.name = "append",
		.method = method_append,
	},{
		.name = "update",
		.method = method_update,
	},{
		.name = "remove",
		.method = method_remove,
	},{
		.name = "list",
		.method = method_list,
	},{
		.name = "filter",
		.method = method_filter,
	},{
		.name = "media",
		.method = method_media,
	},{
		.name = "import",
		.method = method_import,
	},{
		.name = "search",
		.method = method_search,
	},{
		.name = "info",
		.method = method_info,
	},{
		.name = "play",
		.method = method_play,
	},{
		.name = "pause",
		.method = method_pause,
	},{
		.name = "stop",
		.method = method_stop,
	},{
		.name = "next",
		.method = method_next,
	},{
		.name = "volume",
		.method = method_volume,
	},{
		.name = "repeat",
		.method = method_repeat,
	},{
		.name = "shuffle",
		.method = method_shuffle,
	},{
		.name = "quit",
		.method = method_quit,
	},{
		.name = "help",
		.method = method_help,
	}, {
		.name = NULL,
		.method = NULL,
	}
};

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

static int method_next(cmdline_ctx_t *ctx, const char *arg)
{
	return client_next(ctx->client, cmdline_checkstate, ctx);
}

static int method_play(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = 0;
	int id = -1;
	if (arg)
		ret = sscanf(arg, "%d", &id);
	if (ret == 0)
		return client_play(ctx->client, cmdline_checkstate, ctx);
	else
	{
		return client_setnext(ctx->client, (client_event_prototype_t)method_next, ctx, id);
	}
	return -1;
}

static int method_pause(cmdline_ctx_t *ctx, const char *arg)
{
	return client_pause(ctx->client, cmdline_checkstate, ctx);
}

static int method_stop(cmdline_ctx_t *ctx, const char *arg)
{
	return client_stop(ctx->client, cmdline_checkstate, ctx);
}

static int method_volume(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	unsigned int volume = atoi(arg);
	if (volume != -1)
	{
		ret = client_volume(ctx->client, NULL, ctx, volume);
	}
	return ret;
}

static int method_media(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	json_error_t error;
	json_t *media = json_loads(arg, 0, &error);
	ret = media_change(ctx->client, NULL, ctx, media);
	return ret;
}

static int method_repeat(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	if (! strcmp(arg, "on"))
		ret = media_options(ctx->client, NULL, ctx, -1, 1);
	else
		ret = media_options(ctx->client, NULL, ctx, -1, 0);
	return ret;
}

static int method_shuffle(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	if (! strcmp(arg, "on"))
		ret = media_options(ctx->client, NULL, ctx, 1, -1);
	else
		ret = media_options(ctx->client, NULL, ctx, 0, -1);
	return ret;
}

static int method_append(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	json_error_t error;
	json_t *media = json_loads(arg, 0, &error);
	if (media != NULL)
	{
		json_t *params;
		params = json_array();
		json_array_append(params, media);
		ret = media_insert(ctx->client, NULL, ctx, params);
	}
	return ret;
}

static int method_update(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	int id;
	char *info = NULL;
	if (arg)
		ret = sscanf(arg, "%d %1024mc", &id, &info);
	if (ret == 2 && info)
	{
		json_error_t error;
		json_t *jinfo = json_loads(info, 0, &error);
		ret = media_setinfo(ctx->client, NULL, ctx, id, jinfo);
	}
	return ret;
}

static int method_remove(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	int id;
	json_t *params = json_object();
	if (arg)
		ret = sscanf(arg, "%d", &id);
	if (ret == 1)
		json_object_set(params, "id", json_integer(id));
	ret = media_remove(ctx->client, NULL, ctx, params);
	return ret;
}

static int display_info(cmdline_ctx_t *ctx, json_t *info)
{
	const unsigned char *key = NULL;
	json_t *value;
	json_object_foreach(info, key, value)
	{
		if (json_is_string(value))
		{
			const char *string = json_string_value(value);
			if (string != NULL)
			{
				fprintf(stdout, "  %s: %s\n", key, string);
			}
		}
		if (json_is_integer(value))
		{
			fprintf(stdout, "  %s: %lld\n", key, json_integer_value(value));
		}
		if (json_is_null(value))
		{
			fprintf(stdout, "  %s: empty\n", key);
		}
	}

	return 0;
}

static int display_media(cmdline_ctx_t *ctx, json_t *media)
{
	json_t *info = json_object_get(media, "info");
	int id = json_integer_value(json_object_get(media, "id"));
	fprintf(stdout, "media: %d\n", id);
	display_info(ctx, info);
}

static int display_list(cmdline_ctx_t *ctx, json_t *params)
{
	const unsigned char *key = NULL;
	json_t *value;
	int count = json_integer_value(json_object_get(params, "count"));
	int nbitems = json_integer_value(json_object_get(params, "nbitems"));
	fprintf(stdout, "nb media: %d\n", count);
	json_t *playlist = json_object_get(params, "playlist");
	int i;
	json_t *media;
	json_array_foreach(playlist, i, media)
	{
		return display_media(ctx, media);
	}
	return 0;
}

static int method_list(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	int first = 0;
	int max = 5;
	if (arg)
		ret = sscanf(arg, "%d %d", &first, &max);
	if (ret >= 0)
		ret = media_list(ctx->client, (client_event_prototype_t)display_list, ctx, first, max);
	else
		fprintf(stdout, "error on parameter %s\n", strerror(errno));
	return ret;
}

static int method_filter(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	return ret;
}

static int method_import(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	json_error_t error;
	json_t *media = json_load_file(arg, 0, &error);
	if (media != NULL)
	{
		json_t *params;
		if (json_is_array(media))
			params = media;
		else
		{
			params = json_array();
			json_array_append(params, media);
		}
		ret = media_insert(ctx->client, NULL, ctx, params);
	}
	return ret;
}

static int method_search(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	return ret;
}

static int method_info(cmdline_ctx_t *ctx, const char *arg)
{
	int ret = -1;
	int id;
	if (arg)
		ret = sscanf(arg, "%d", &id);
	if (ret == 1)
		ret = media_info(ctx->client, (client_event_prototype_t)display_media, ctx, id);
	return ret;
}

static int method_quit(cmdline_ctx_t *ctx, const char *arg)
{
	client_disconnect(ctx->client);
	ctx->run = 0;
	return 0;
}

static int method_help(cmdline_ctx_t *ctx, const char *arg)
{
	fprintf(stdout, "putv commands:\n");
	fprintf(stdout, " play   : start the stream\n");
	fprintf(stdout, "        [media id]\n");
	fprintf(stdout, " stop   : stop the stream\n");
	fprintf(stdout, " pause  : suspend the stream\n");
	fprintf(stdout, " next   : request the next opus\n");
	fprintf(stdout, " repeat : change the repeat mode\n");
	fprintf(stdout, "        <on|off>\n");
	fprintf(stdout, " shuffle: change the shuffle mode\n");
	fprintf(stdout, "        <on|off>\n");
	fprintf(stdout, " volume : request to change the level of volume on putv server\n");
	fprintf(stdout, "        <0..100>\n");
	fprintf(stdout, " media  : request to change the media\n");
	fprintf(stdout, "        <media url>\n");
	fprintf(stdout, " list   : display the opus from the media\n");
	fprintf(stdout, "        <first opus id> <max number of opus>\n");
	fprintf(stdout, " info   : display an opus from the media\n");
	fprintf(stdout, "        <opus id>\n");
	fprintf(stdout, " append : add an opus into the media\n");
	fprintf(stdout, "        <json media> {\"url\":\"https://example.com/stream.mp3\",\"info\":{\"title\": \"test\",\"artist\":\"John Doe\",\"album\":\"white\"}}\n");
	fprintf(stdout, " import : import opus from a file into the media\n");
	fprintf(stdout, "        <file path>\n");
	fprintf(stdout, " export : export the opus from the media into a file\n");
	fprintf(stdout, "        <file path>\n");
	fprintf(stdout, " quit   : quit the command line application\n");
	return 0;
}

int printevent(cmdline_ctx_t *ctx, json_t *json_params)
{
	char *state;
	int id;
	json_t *info = json_object();

	json_unpack(json_params, "{ss,si,so}", "state", &state,"id", &id, "info", &info);
	fprintf(stdout, "\n%s\n", state);
	display_info(ctx, info);
	fprintf(stdout, "> ");
	fflush(stdout);
}

int run_client(void *arg)
{
	cmdline_ctx_t *ctx = (cmdline_ctx_t *)arg;

	client_data_t data = {0};
	client_unix(ctx->socketpath, &data);
	client_async(&data, 1);
	ctx->client = &data;

	pthread_t thread;
	client_eventlistener(&data, "onchange", (client_event_prototype_t)printevent, ctx);
	pthread_create(&thread, NULL, (__start_routine_t)client_loop, (void *)&data);

	int fd = 0;
	while (ctx->run)
	{
		int ret;
		fd_set rfds;
		struct timeval timeout = {1, 0};
		struct timeval *ptimeout = NULL;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		int maxfd = fd;
		fprintf (stdout, "> ");
		fflush(stdout);
		ret = select(maxfd + 1, &rfds, NULL, NULL, ptimeout);
		if (ret > 0 && FD_ISSET(fd, &rfds))
		{
			int length;
			ret = ioctl(fd, FIONREAD, &length);
			char *buffer = malloc(length + 1);
			ret = read(fd, buffer, length);
			int i;
			method_t method = NULL;
			const char *arg = NULL;
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
				for (int j = 0; cmds[j].name != NULL; j++)
				{
					int length = strlen(cmds[j].name);
					if (!strncmp(buffer + i, cmds[j].name, length))
					{
						method = cmds[j].method;
						i += length;
						break;
					}
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
			else
				fprintf(stdout, " command not found\n");
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
					sleep(1);
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
		opt = getopt(argc, argv, "R:n:m:h");
		switch (opt)
		{
			case 'R':
				cmdline_data.root = optarg;
			break;
			case 'n':
				cmdline_data.name = optarg;
			break;
			case 'm':
				media_path = optarg;
			break;
			case 'h':
				fprintf(stderr, "cmdline -R <dir> -n <socketname> [-m <jsonfile>]");
				fprintf(stderr, "cmdline for putv applications\n");
				fprintf(stderr, " -R <DIR>   change the socket directory directory");
				fprintf(stderr, " -n <NAME>  change the socket name");
				fprintf(stderr, " -m <FILE>  load Json file to mqnqge media");
				return -1;
			break;
		}
	} while(opt != -1);

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
	cmdline_data.socketpath = malloc(strlen(cmdline_data.root) + 1 + strlen(cmdline_data.name) + 1);
	sprintf(cmdline_data.socketpath, "%s/%s", cmdline_data.root, cmdline_data.name);
	cmdline_data.run = 1;
#ifdef USE_INOTIFY
	cmdline_data.inotifyfd = inotify_init();
	int dirfd = inotify_add_watch(cmdline_data.inotifyfd, cmdline_data.root,
					IN_MODIFY | IN_CREATE | IN_DELETE);
	_check_socket((void *)&cmdline_data);
#else

	run_client((void *)&cmdline_data);
#endif
	free(cmdline_data.socketpath);
	json_decref(cmdline_data.media);
	return 0;
}
