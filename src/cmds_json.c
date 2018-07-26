/*****************************************************************************
 * cmds_json.c
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
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include <pthread.h>
#include <jansson.h>

#include "putv.h"
#include "jsonrpc.h"
typedef struct cmds_ctx_s cmds_ctx_t;
struct cmds_ctx_s
{
	mediaplayer_ctx_t *putv;
	media_t *media;
	const char *socketpath;
};
#define CMDS_CTX
#include "cmds.h"
#include "media.h"
#include "unix_server.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

extern struct jsonrpc_method_entry_t method_table[];

const char const *str_stop = "stop";
const char const *str_play = "play";
const char const *str_pause = "pause";

static int method_append(json_t *json_params, json_t **result, void *userdata)
{
	cmds_ctx_t *ctx = (cmds_ctx_t *)userdata;
	if (json_is_array(json_params)) {
		int ret;
		size_t index;
		json_t *value;
		json_array_foreach(json_params, index, value)
		{
			if (json_is_string(value))
			{
				const char *str = json_string_value(value);
				ret = ctx->media->ops->insert(ctx->media->ctx, str, "", NULL);
			}
			else if (json_is_object(value))
			{
				json_t * path = json_object_get(value, "url");
				json_t * info = json_object_get(value, "info");
				json_t * mime = json_object_get(value, "mime");
				ret = ctx->media->ops->insert(ctx->media->ctx, 
						json_string_value(path),
						json_string_value(info),
						json_string_value(mime));
			}
			if (ret == -1)
				*result = jsonrpc_error_object(-12345,
					"append error",
					json_string("media could not be insert into the playlist"));
			else
				*result = json_pack("{s:s,s:s}", "status", "DONE", "message", "media append");
		}
	}
	return 0;
}

static int method_play(json_t *json_params, json_t **result, void *userdata)
{
	cmds_ctx_t *ctx = (cmds_ctx_t *)userdata;
	if (ctx->media->ops->count(ctx->media->ctx) > 0)
		player_state(ctx->putv, STATE_PLAY);
	else
		player_state(ctx->putv, STATE_STOP);
	if (player_state(ctx->putv, STATE_UNKNOWN) == STATE_PLAY)
		*result = json_pack("{s:s,s:s}", "status", "DONE", "message", "media append");
	else
		*result = jsonrpc_error_object(-12345,
					"play error",
					json_string("empty playlist"));
	return 0;
}

static int method_pause(json_t *json_params, json_t **result, void *userdata)
{
	cmds_ctx_t *ctx = (cmds_ctx_t *)userdata;

	if (player_state(ctx->putv, 0) == STATE_PLAY)
		player_state(ctx->putv, STATE_PAUSE);
	return 0;
}

static int method_stop(json_t *json_params, json_t **result, void *userdata)
{
	cmds_ctx_t *ctx = (cmds_ctx_t *)userdata;
	player_state(ctx->putv, STATE_STOP);
	return 0;
}

static int method_next(json_t *json_params, json_t **result, void *userdata)
{
	cmds_ctx_t *ctx = (cmds_ctx_t *)userdata;
	ctx->media->ops->next(ctx->media->ctx);
	return 0;
}

static int method_change(json_t *json_params, json_t **result, void *userdata)
{
	cmds_ctx_t *ctx = (cmds_ctx_t *)userdata;
	char url[256];
	int urllen = 256;
	char info[1024];
	int infolen = 1024;

	if (ctx->media->ops->current(ctx->media->ctx, url, &urllen, info, &infolen) == 0)
	{
		const char *state_str = str_stop;
		state_t state = player_state(ctx->putv, STATE_UNKNOWN);

		switch (state)
		{
		case STATE_PLAY:
			state_str = str_play;
		break;
		case STATE_PAUSE:
			state_str = str_pause;
		break;
		}
		json_error_t error;
		*result = json_pack_ex(&error, 0, "{s:s,s:s%,s:s%}", 
				"state", state_str,
				"url", url, urllen,
				"info", info, infolen);
		if (*result == NULL)
			printf("error on result %s\n", error.text);
	}
	else
		*result = json_pack("{s:s}", "state", "stop");

	return 0;
}

struct jsonrpc_method_entry_t method_table[] = {
	{ 'r', "play", method_play, "" },
	{ 'r', "pause", method_pause, "" },
	{ 'r', "stop", method_stop, "" },
	{ 'r', "next", method_next, "" },
	{ 'r', "append", method_append, "[]" },
	{ 'n', "change", method_change, "o" },
	{ 0, NULL },
};

void jsonrpc_onchange(void * userctx, mediaplayer_ctx_t *ctx)
{
	thread_info_t *info = (thread_info_t *)userctx;

	char* notification = jsonrpc_request("change", sizeof("change"), method_table, (void *)ctx, NULL);
	int length = strlen(notification);
	int sock = info->sock;
	if (send(sock, notification, length, MSG_DONTWAIT | MSG_NOSIGNAL) < 0)
	{
		//TODO remove notification from mediaplayer
	}
}

int jsonrpc_command(thread_info_t *info)
{
	int ret = 0;
	int sock = info->sock;
	cmds_ctx_t *ctx = info->userctx;
	player_onchange(ctx->putv, jsonrpc_onchange, (void *)info);

	while (sock > 0)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);

		ret = select(sock + 1, &rfds, NULL, NULL, NULL);
		if (ret > 0 && FD_ISSET(sock, &rfds))
		{
			char buffer[1500];
			ret = recv(sock, buffer, 1500, MSG_NOSIGNAL);
			if (ret > 0)
			{
				char *out = jsonrpc_handler(buffer, ret, method_table, ctx);
				ret = strlen(out);
				ret = send(sock, out, ret, MSG_DONTWAIT | MSG_NOSIGNAL);
			}
		}
		if (ret == 0)
		{
			unixserver_remove(info);
			sock = -1;
		}
		if (ret < 0)
		{
			if (errno != EAGAIN)
			{
				unixserver_remove(info);
				sock = -1;
			}
		}
	}
	return ret;
}

cmds_ctx_t *cmds_json_init(mediaplayer_ctx_t *putv, media_t *media, void *arg)
{
	cmds_ctx_t *ctx = NULL;
	ctx = calloc(1, sizeof(*ctx));
	ctx->putv = putv;
	ctx->media = media;
	ctx->socketpath = (const char *)arg;
	return ctx;
}

int cmds_json_run(cmds_ctx_t *ctx)
{
	return unixserver_run(jsonrpc_command, (void *)ctx, ctx->socketpath);
}

void cmds_json_destroy(cmds_ctx_t *ctx)
{
	free(ctx);
}

cmds_t *cmds_json = &(cmds_t)
{
	.init = cmds_json_init,
	.run = cmds_json_run,
	.destroy = cmds_json_destroy,
};
