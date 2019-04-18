/*****************************************************************************
 * media.c
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

#include <libgen.h>
#include <jansson.h>

#include "client_json.h"
#include "../version.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

typedef void *(*__start_routine_t) (void *);

typedef enum
{
	NONE,
	INSERT,
	REMOVE,
	LIST,
} method_t;

typedef struct media_s media_t;
struct media_s
{
	int id;
	const char *url;
	const char *title;
	const char *artist;
	const char *genre;
	const char *album;
};

typedef struct data_s data_t;
struct data_s
{
	client_data_t client;
	long long nbitems;
	media_t media;
	list_t list;
	method_t method;
	int wait;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

int method_insert(void *arg, json_t *params)
{
	data_t *data = (data_t *)arg;
	data->wait = 0;
	pthread_cond_signal(&data->cond);
	return 0;
}

int method_remove(void *arg, json_t *params)
{
	data_t *data = (data_t *)arg;
	data->wait = 0;
	pthread_cond_signal(&data->cond);
	return 0;
}

int _display(void *data, json_t *entry)
{
	int id = -1;
	json_t *jsid = json_object_get(entry, "id");
	if (json_is_integer(jsid))
	{
		id = json_integer_value(jsid);
		printf("Media %d:\n", id);
	}
	else
		printf("Media unkonwn:\n");
	
	json_t *info = json_object_get(entry, "info");
	if (json_is_object(info))
	{
		json_t *title = json_object_get(info, "Title");
		json_t *artist = json_object_get(info, "Artist");
		json_t *album = json_object_get(info, "Album");
		json_t *genre = json_object_get(info, "Genre");
		if (json_is_string(title))
			printf("\tTitle: %s\n", json_string_value(title));
		if (json_is_string(artist))
			printf("\tArtist: %s\n", json_string_value(artist));
		if (json_is_string(album))
			printf("\tAlbum: %s\n", json_string_value(album));
		if (json_is_string(genre))
			printf("\tGenre: %s\n", json_string_value(genre));
	}

	json_t *sources = json_object_get(entry, "sources");
	if (json_is_array(sources))
	{
		int i;
		for (i = 0; i < json_array_size(sources); i++)
		{
			json_t *source = json_array_get(sources, i);
			if (json_is_object(source))
			{
				json_t *url = json_object_get(source, "url");
				json_t *mime = json_object_get(source, "mime");
				if (json_is_string(url))
					printf("\tURL: %s\n", json_string_value(url));
			}
		}
	}

	return id;
}

int method_list(void *arg, json_t *params)
{
	data_t *data = (data_t *)arg;
	int id = -1;
	if (json_is_object(params))
	{
		json_t *playlist = json_object_get(params, "playlist");
		if (json_is_array(playlist))
		{
			int i;
			for (i = 0; i < json_array_size(playlist); i++)
			{
				json_t *entry = json_array_get(playlist, i);
				if (json_is_object(entry))
					id = _display(data, entry);
			}
		}
		else if (json_is_object(playlist))
			id = _display(data, playlist);
		data->nbitems += json_integer_value(json_object_get(params, "nbitems"));
		long long count = json_integer_value(json_object_get(params, "count"));
		printf("Display : %lld/%lld\n", data->nbitems, count);
		if (data->nbitems < count)
		{
			data->list.first = id;
			data->method = LIST;
		}
		else
			data->method = NONE;
	}
	data->wait = 0;
	pthread_cond_signal(&data->cond);
	return 0;
}

#define DAEMONIZE 0x01
int main(int argc, char **argv)
{
	int mode = 0;
	const char *root = DATADIR;
	const char *socketname = basename(argv[0]);
	data_t data = {0};
	method_t method;
	pthread_t thread;

	data.media.id = -1;
	int opt;
	do
	{
		opt = getopt(argc, argv, "R:n:irlT:A:B:G:U:I:hD");
		switch (opt)
		{
			case 'R':
				root = optarg;
			break;
			case 'n':
				socketname = optarg;
			break;
			case 'i':
				method = INSERT;
			break;
			case 'r':
				method = REMOVE;
			break;
			case 'l':
				method = LIST;
			break;
			case 'T':
				data.media.title = optarg;
			break;
			case 'A':
				data.media.artist = optarg;
			break;
			case 'B':
				data.media.album = optarg;
			break;
			case 'G':
				data.media.genre = optarg;
			break;
			case 'U':
				data.media.url = optarg;
			break;
			case 'I':
				data.media.id = atoi(optarg);
				data.list.first = data.media.id;
			break;
			case 'h':
				return -1;
			break;
			case 'D':
				mode |= DAEMONIZE;
			break;
		}
	} while(opt != -1);

	pthread_cond_init(&data.cond, NULL);
	pthread_mutex_init(&data.mutex, NULL);
	char *socketpath;
	socketpath = malloc(strlen(root) + 1 + strlen(socketname) + 1);
	sprintf(socketpath, "%s/%s", root, socketname);
	if (client_unix(socketpath, &data.client) > 0)
	{
		pthread_create(&thread, NULL, (__start_routine_t)client_loop, (void *)&data);
	}
	json_t *params;
	int nbitems = 0;
	do {
		data.method = NONE;
		data.wait = 1;
		switch (method)
		{
		case INSERT:
			params = json_array();
			json_t *entry = json_object();
			if (data.media.url != NULL)
				json_object_set(entry, "url", json_string(data.media.url));
			json_t *info = json_object();
			if (data.media.artist != NULL)
				json_object_set(info, "Title", json_string(data.media.title));
			if (data.media.artist != NULL)
				json_object_set(info, "Artist", json_string(data.media.artist));
			if (data.media.artist != NULL)
				json_object_set(info, "Album", json_string(data.media.album));
			if (data.media.artist != NULL)
				json_object_set(info, "Genre", json_string(data.media.genre));
			json_object_set(entry, "info", info);
			json_array_append(params, entry);
			media_insert(&data.client, method_insert, &data, params);
		break;
		case REMOVE:
			params = json_object();
			if (data.media.id != -1)
				json_object_set(params, "id", json_integer(data.media.id));
			if (data.media.url != NULL)
				json_object_set(params, "url", json_string(data.media.url));
			media_remove(&data.client, method_remove, &data, params);
		break;
		case LIST:
			media_list(&data.client, method_list, &data, &data.list);
		break;
		}
		pthread_mutex_lock(&data.mutex);
		while (data.wait)
		{
			pthread_cond_wait(&data.cond, &data.mutex);
		}
		pthread_mutex_unlock(&data.mutex);
	} while (data.method != NONE);
	free(socketpath);
	pthread_mutex_destroy(&data.mutex);
	pthread_cond_destroy(&data.cond);
	return 0;
}
