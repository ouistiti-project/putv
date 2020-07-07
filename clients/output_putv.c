/*****************************************************************************
 * gmrenderer.c
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

#include "client_json.h"
#include "output_module.h"

extern void Log_info(const char *category, const char *format, ...);
extern void Log_error(const char *category, const char *format, ...);

#define err(format, ...) Log_error("output_putv", format, ##__VA_ARGS__)
#define warn(format, ...) Log_info("output_putv", format, ##__VA_ARGS__)
#define dbg(format, ...) Log_info("output_putv", format, ##__VA_ARGS__)

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

#ifndef PUTV
#define PUTV   PACKAGE_NAME
#endif
#ifndef WEBSOCKETDIR
#define WEBSOCKETDIR /var/run/websocket
#endif

#define SOCKETNAME EXPAND_AND_QUOTE(PUTV)
#define ROOTDIR EXPAND_AND_QUOTE(WEBSOCKETDIR)


struct SongMetaData {
	const char *title;
	const char *artist;
	const char *album;
	const char *genre;
	const char *composer;
};

typedef struct gmrenderer_ctx_s gmrenderer_ctx_t;
struct gmrenderer_ctx_s
{
	const char *root;
	const char *name;
	char *socketpath;
	int volume;
	int current_id;
	client_data_t client;
	json_t *media;
	output_transition_cb_t transition_cb;
	output_update_meta_cb_t meta_cb;
	struct SongMetaData metadata;
	enum
	{
		STATE_UNKNOWN,
		STATE_PLAY,
		STATE_PAUSE,
		STATE_STOP,
	} state;
};

static int gmrenderer_checkstate(void *data, json_t *params)
{
	dbg("putv check params %s", json_dumps(params, 0));
	gmrenderer_ctx_t *ctx = (gmrenderer_ctx_t *)data;
	if (json_is_object(params))
	{
		json_t *jstate = json_object_get(params, "state");
		if (json_is_string(jstate))
		{
			int state;
			state = ctx->state;
			const char *sstate;
			sstate = json_string_value(jstate);
			if (ctx->state != STATE_PLAY && !strcmp(sstate, "play"))
				state = STATE_PLAY;
			else if (ctx->state != STATE_PAUSE && !strcmp(sstate, "pause"))
				state = STATE_PAUSE;
			else if (ctx->state != STATE_STOP && !strcmp(sstate, "stop"))
				state = STATE_STOP;
			if (state != ctx->state)
			{
				ctx->state = state;
				if (ctx->state == STATE_STOP && ctx->transition_cb)
				{
					ctx->transition_cb(PLAY_STOPPED);
				}
			}
		}
		json_t *jid = json_object_get(params, "id");
		if (json_is_integer(jid))
		{
			int id;
			id = json_integer_value(jid);
			if (ctx->current_id == -1)
				ctx->current_id = id;
			if (id != ctx->current_id)
			{
				ctx->current_id = id;
				if (ctx->media != NULL)
				{
					media_change(&ctx->client, NULL, ctx, ctx->media);
					json_decref(ctx->media);
					ctx->media = NULL;
					if (ctx->transition_cb)
					{
						ctx->transition_cb(PLAY_STARTED_NEXT_STREAM);
					}
				}
			}
		}
		json_t *jinfo = json_object_get(params, "info");
		if (json_is_object(jinfo))
		{
			json_t *jartist = json_object_get(jinfo, "Artist");
			if (json_is_string(jartist))
				ctx->metadata.artist = json_string_value(jartist);
			json_t *jalbum = json_object_get(jinfo, "Album");
			if (json_is_string(jalbum))
				ctx->metadata.album = json_string_value(jalbum);
			json_t *jtitle = json_object_get(jinfo, "Title");
			if (json_is_string(jtitle))
				ctx->metadata.title = json_string_value(jtitle);
			json_t *jgenre = json_object_get(jinfo, "Genre");
			if (json_is_string(jgenre))
				ctx->metadata.genre = json_string_value(jgenre);

			//if (ctx->meta_cb)
			//	ctx->meta_cb(&ctx->metadata);
		}
	}
	dbg("putv check end");

	return 0;
}

static int gmrenderer_checkvolume(void *data, json_t *params)
{
	dbg("putv check volume %s", json_dumps(params, 0));
	gmrenderer_ctx_t *ctx = (gmrenderer_ctx_t *)data;
	if (json_is_object(params))
	{
		json_t *jlevel = json_object_get(params, "level");
		if (json_is_integer(jlevel))
		{
			ctx->volume =  json_integer_value(jlevel);
		}
	}
	dbg("putv volume end");

	return 0;
}

static gmrenderer_ctx_t *gmrenderer_ctx = &(gmrenderer_ctx_t)
{
	.root = ROOTDIR,
	.name = SOCKETNAME,
};

#ifdef USE_INOTIFY
#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))

int inotifyfd;

static void *_check_socket(void *arg)
{
	gmrenderer_ctx_t *ctx = (gmrenderer_ctx_t *)arg;

	if (!access(ctx->socketpath, R_OK | W_OK))
	{
		client_unix(gmrenderer_ctx->socketpath, &gmrenderer_ctx->client);
		client_async(&gmrenderer_ctx->client, 1);
		client_eventlistener(&gmrenderer_ctx->client, "onchange", gmrenderer_checkstate, gmrenderer_ctx);
	}
	while (1)
	{
		char buffer[BUF_LEN];
		int i = 0;
		int length = read(inotifyfd, buffer, BUF_LEN);

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
						client_unix(gmrenderer_ctx->socketpath, &gmrenderer_ctx->client);
						client_async(&gmrenderer_ctx->client, 1);
						client_eventlistener(&gmrenderer_ctx->client, "onchange", gmrenderer_checkstate, gmrenderer_ctx);
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

static int
output_putv_init(void)
{
	const char *websocketdir = getenv("WEBSOCKETDIR");
	if (websocketdir != NULL)
		gmrenderer_ctx->root = websocketdir;
	const char *putv = getenv("PUTV");
	if (putv != NULL)
		gmrenderer_ctx->name = putv;
	gmrenderer_ctx->current_id = -1;

	int len = strlen(gmrenderer_ctx->root) + 1;
	len += strlen(gmrenderer_ctx->name) + 1;
	gmrenderer_ctx->socketpath = malloc(len);
	sprintf(gmrenderer_ctx->socketpath, "%s/%s", gmrenderer_ctx->root, gmrenderer_ctx->name);
	dbg("socket path %s", gmrenderer_ctx->socketpath);

#if 0
//#ifdef USE_INOTIFY
	inotifyfd = inotify_init();
	int dirfd = inotify_add_watch(inotifyfd, gmrenderer_ctx->root,
					IN_MODIFY | IN_CREATE | IN_DELETE);
	pthread_t thread;
	pthread_create(&thread, NULL, (__start_routine_t)_check_socket, (void *)&gmrenderer_ctx);
#else
	if (access(gmrenderer_ctx->socketpath, R_OK | W_OK))
		return -1;
	client_unix(gmrenderer_ctx->socketpath, &gmrenderer_ctx->client);
	client_async(&gmrenderer_ctx->client, 1);
	client_eventlistener(&gmrenderer_ctx->client, "onchange", gmrenderer_checkstate, gmrenderer_ctx);
#endif

	return 0;
}

static int
output_putv_loop()
{
	client_loop(&gmrenderer_ctx->client);

	free(gmrenderer_ctx->socketpath);
	return 0;
}

static void
output_putv_set_uri(const char *uri,
				     output_update_meta_cb_t meta_cb)
{
	json_t *media = json_object();
	json_object_set_new(media, "media", json_string(uri));
	dbg("UPnP: set uri %s", uri);
	media_change(&gmrenderer_ctx->client, NULL, gmrenderer_ctx, media);
	json_decref(media);
}


static void
output_putv_set_next_uri(const char *uri)
{
	json_t *media = json_object();
	json_object_set_new(media, "media", json_string(uri));
	dbg("UPnP: set next uri %s", uri);
	if (media_insert(&gmrenderer_ctx->client, NULL, NULL, media) < 0)
	{
		if (gmrenderer_ctx->media)
			json_decref(gmrenderer_ctx->media);
		gmrenderer_ctx->media = media;
	}
	else
		json_decref(media);
}

static int
output_putv_play(output_transition_cb_t callback)
{
	gmrenderer_ctx->transition_cb = callback;
	dbg("UPnP: play");
	client_play(&gmrenderer_ctx->client, gmrenderer_checkstate, gmrenderer_ctx);
	return 0;
}

static int
output_putv_stop(void)
{
	dbg("UPnP: stop");
	client_stop(&gmrenderer_ctx->client, gmrenderer_checkstate, gmrenderer_ctx);
	gmrenderer_ctx->current_id = -1;
	return 0;
}

static int
output_putv_pause(void)
{
	dbg("UPnP: pause");
	client_pause(&gmrenderer_ctx->client, gmrenderer_checkstate, gmrenderer_ctx);
	return 0;
}

static int
output_putv_seek(int64_t position_nanos)
{
	dbg("UPnP: seek");
	client_next(&gmrenderer_ctx->client, gmrenderer_checkstate, gmrenderer_ctx);
	return 0;
}

static int
output_putv_get_position(int64_t *track_duration,
					 int64_t *track_pos)
{
	return 0;
}

static int
output_putv_getvolume(float *value)
{
	dbg("UPnP: get volume");
	client_volume(&gmrenderer_ctx->client, gmrenderer_checkvolume, gmrenderer_ctx, json_null());
	return gmrenderer_ctx->volume;
}
static int
output_putv_setvolume(float value)
{
	dbg("UPnP: set volume");
	client_volume(&gmrenderer_ctx->client, NULL, gmrenderer_ctx, json_integer((int) value));
	return 0;
}
static int
output_putv_getmute(int *value)
{
	return 0;
}
static int
output_putv_setmute(int value)
{
	client_volume(&gmrenderer_ctx->client, NULL, gmrenderer_ctx, json_integer(0));
	return 0;
}

static int output_putv_add_options(int *argc, char **argv[])
{
	if (*argc < 2)
	return 0;
	int opt;
	do
	{
		opt = getopt(*argc, *argv, "R:n:");
		switch (opt)
		{
		case 'R':
			gmrenderer_ctx->root = optarg;
		break;
		case 'n':
			gmrenderer_ctx->name = optarg;
		break;
		default:
		break;
		}
	} while(opt != -1);

	return 0;
}

struct output_module putv_output = {
    .shortname = "putv",
	.description = "putv framework",
	.add_options = output_putv_add_options,
	.init        = output_putv_init,
	.loop        = output_putv_loop,
	.set_uri     = output_putv_set_uri,
	.set_next_uri= output_putv_set_next_uri,
	.play        = output_putv_play,
	.stop        = output_putv_stop,
	.pause       = output_putv_pause,
	.seek        = output_putv_seek,
	.get_position = output_putv_get_position,
	.get_volume  = output_putv_getvolume,
	.set_volume  = output_putv_setvolume,
	.get_mute  = output_putv_getmute,
	.set_mute  = output_putv_setmute,
};

const struct output_module *get_module()
{
	return &putv_output;
}
