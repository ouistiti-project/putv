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

#include <libgen.h>
#include <sys/socket.h>
#include <sys/un.h>
# include <sys/ioctl.h>

#include <jansson.h>

#include "jsonrpc.h"
#include "client_json.h"
#include "../version.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

struct client_event_s
{
	const char *event;
	client_event_prototype_t proto;
	void *data;
	client_event_t *next;
};

static int method_subscribe(json_t *json_params, json_t **result, void *userdata)
{
	*result = json_pack("{ss}", "event", "onchange");
	return 0;
}

static int answer_subscribe(json_t *json_params, json_t **result, void *userdata)
{
	int state = -1;
	json_unpack(json_params, "{sb}", "subscribed", &state);
	return !state;
}

static int method_next(json_t *json_params, json_t **result, void *userdata)
{
	*result = json_null();
	return 0;
}

static int answer_next(json_t *json_params, json_t **result, void *userdata)
{
	const char *state;
	int id;
	json_unpack(json_params, "{si,ss}", "id", &id, "state", &state);
	return 0;
}

static int method_state(json_t *json_params, json_t **result, void *userdata)
{
	*result = json_null();
	return 0;
}

static int answer_state(json_t *json_params, json_t **result, void *userdata)
{
	const char *state;
	json_unpack(json_params, "{ss}", "state", &state);
	return 0;
}

static int notification_onchange(json_t *json_params, json_t **result, void *userdata)
{
	client_data_t *data = userdata;
	client_event_t *event = data->events;
	while (event)
	{
		if (!strcmp(event->event, "onchange"))
			break;
		event = event->next;
	}
	if (event)
	{
		event->proto(event->data, json_params);
	}
	return 0;
}

struct jsonrpc_method_entry_t table[] =
{
	{'r',"subscribe", method_subscribe, "o", 0, NULL},
	{'a',"subscribe", answer_subscribe, "o", 0, NULL},
	{'r',"next", method_next, "", 0, NULL},
	{'a',"next", answer_next, "o", 0, NULL},
	{'r',"play", method_state, "", 0, NULL},
	{'a',"play", answer_state, "o", 0, NULL},
	{'r',"pause", method_state, "", 0, NULL},
	{'a',"pause", answer_state, "o", 0, NULL},
	{'r',"stop", method_state, "", 0, NULL},
	{'a',"stop", answer_state, "o", 0, NULL},
	{'n',"onchange", notification_onchange, "o", 0, NULL},
	{0, NULL},
};

int client_unix(const char *socketpath, client_data_t *data)
{
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock > 0)
	{
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(struct sockaddr_un));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, socketpath, sizeof(addr.sun_path));
		int ret = connect(sock, (const struct sockaddr *)&addr, sizeof(addr.sun_path));
		if (ret != 0)
			sock = 0;
		data->sock = sock;
	}

	return sock;
}

int client_eventlistener(client_data_t *data, const char *name, client_event_prototype_t proto, void *protodata)
{
	client_event_t *event = calloc(1, sizeof(*event));
	event->event = strdup(name);
	event->proto = proto;
	event->data = protodata;
	event->next = data->events;
	data->events = event;
	return 0;
}

int client_loop(client_data_t *data)
{
	int run = 1;
	client_event_t *event = data->events;
	while (event)
	{
//		jsonrpc_request("subscribe");
		event = event->next;
	}
	while (data->sock > 0  && run)
	{
		fd_set rfds;
		int maxfd = data->sock;
		FD_ZERO(&rfds);
		FD_SET(data->sock, &rfds);
		int ret;
		ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (ret > 0 && FD_ISSET(data->sock, &rfds))
		{
			int len;
			ret = ioctl(data->sock, FIONREAD, &len);
			char *buffer = malloc(len);
			ret = recv(data->sock, buffer, len, MSG_NOSIGNAL);
			if (ret > 0)
				jsonrpc_handler(buffer, len, table, data);
			if (ret == 0)
				run = 0;
		}
		if (ret < 0)
			run = 0;
	}
	return 0;
}
