/*****************************************************************************
 * media_dir.c
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
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define __USE_GNU
#include <pthread.h>

#ifdef USE_INOTIFY
#include <sys/inotify.h>
#endif

#include <jansson.h>

#include "player.h"
#include "media.h"

#define MINCOUNT 50

typedef struct media_dirlist_s media_dirlist_t;
struct media_ctx_s
{
	const char *url;
	char *root;
	player_ctx_t *player;
	int mediaid;
	int firstmediaid;
	int count;
	unsigned int options;
	int inotifyfd;
	int dirfd;
	pthread_t thread;
	media_dirlist_t *first;
	media_dirlist_t *current;
};

struct media_dirlist_s
{
	char *path;
	int level;
	/**
	 * items into the directory
	 */
	struct dirent **items;
	int nitems;
	/**
	 * current item into the directory
	 */
	int index;
	int mediaid;
	media_dirlist_t *next;
	media_dirlist_t *prev;
};

#define OPTION_LOOP 0x0001
#define OPTION_RANDOM 0x0002
#define OPTION_INOTIFY 0x1000

#define PROTOCOLNAME "file://"
#define PROTOCOLNAME_LENGTH 7

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

static int media_count(media_ctx_t *ctx);
static int media_insert(media_ctx_t *ctx, const char *path, const char *info, const char *mime);
static int media_find(media_ctx_t *ctx, int id, media_parse_t cb, void *data);
static int media_play(media_ctx_t *ctx, media_parse_t play, void *data);
static int media_next(media_ctx_t *ctx);
static option_state_t media_loop(media_ctx_t *ctx, option_state_t enable);
static option_state_t media_random(media_ctx_t *ctx, option_state_t enable);
static int media_end(media_ctx_t *ctx);

/**
 * directory browsing functions
 **/
typedef int (*_findcb_t)(void *arg, media_ctx_t *ctx, int mediaid, const char *path, const char *mime);

typedef struct _find_mediaid_s _find_mediaid_t;
struct _find_mediaid_s
{
	int id;
	media_parse_t cb;
	void *arg;
};

static int _run_cb(_find_mediaid_t *mdata, int id, const char *path, const char *mime)
{
	int ret = 0;
	if (mdata->cb != NULL)
	{
		char *info = NULL;
		json_t *object = NULL;

		object = json_object();

#ifdef USE_ID3TAG
		if (mime && !strcmp(mime, mime_audiomp3))
		{
			media_parseid3tag(path, object);
		}
#endif
#ifdef USE_OGGMETADDATA
		if (mime && !strcmp(mime, mime_audioflac))
		{
			media_parseoggmetadata(path, object);
		}
#endif
		char coverpath[PATH_MAX];
		strcpy(coverpath, path);
		char *dname = strrchr(coverpath, '/');
		if (strlen(dname) >= 8)
		{
			if (dname == NULL)
				dname = coverpath;
			else
				dname++;
			strcpy(dname, "cover.jpg");
			if (!access(coverpath, R_OK))
			{
				json_t *value;
				value = json_string(coverpath);
				json_object_set(object, str_cover, value);
			}
			strcpy(dname, "cover.png");
			if (!access(coverpath, R_OK))
			{
				json_t *value;
				value = json_string(coverpath);
				json_object_set(object, str_cover, value);
			}
		}

		info = json_dumps(object, JSON_INDENT(2));
		json_decref(object);
		ret = mdata->cb(mdata->arg, id, path, info, mime);
		if (info != NULL)
			free(info);
	}
	return ret;
}

static media_dirlist_t *_free_medialist(media_dirlist_t *it, int one)
{
	while (it != NULL)
	{
		if (it->path)
			free(it->path);
		it->path == NULL;
		if (it->items)
			free(it->items);
		media_dirlist_t *prev = it->prev;
		free(it);
		it = prev;
		if (one)
			break;
	}
	return it;
}

static int _find_mediaid(void *arg, media_ctx_t *ctx, int mediaid, const char *path, const char *mime)
{
	int ret = 1;
	_find_mediaid_t *mdata = (_find_mediaid_t *)arg;
	if (mdata->id >= 0 && mdata->id == mediaid)
	{
		_run_cb(mdata, mediaid, path, mime);
		ret = 0;
	}
	else if (mdata->id == -1 && mdata->cb != NULL)
	{
		_run_cb(mdata, mediaid, path, mime);
	}
	return ret;
}

static int _find_display(void *arg, media_ctx_t *ctx, int mediaid, const char *path, const char *mime)
{
	printf("Media: %d, %s\n", mediaid, path);
	return 1;
}

static int _find_count(void *arg, media_ctx_t *ctx, int mediaid, const char *path, const char *mime)
{
	return 1;
}

static int _find(media_ctx_t *ctx, int level, media_dirlist_t **pit, int *pmediaid, _findcb_t cb, void *arg)
{
	int ret = -1;
	media_dirlist_t *it = *pit;
	if (it == NULL)
	{
		it = calloc(1, sizeof(*it));
		it->path = malloc(strlen(ctx->root) + 1);
		strcpy(it->path, ctx->root);
		it->nitems = scandir(it->path, &it->items, NULL, alphasort);
		*pmediaid = 0;
		ctx->first = it;
	}
	else if (it->level > level)
	{
		ret =_find(ctx, level + 1, pit, pmediaid, cb, arg);
		it = *pit;
		/**
		 * return 0 means that something found.
		 * in this case the index has not to be change,
		 * to begin for this point with the next search.
		 */
		if (ret != 0 && it)
			it->index++;
	}

	while (ret != 0 && it->nitems > 0 && it->index < it->nitems)
	{
		if (it->items[it->index]->d_name[0] == '.')
		{
			it->index++;
			continue;
		}
		switch (it->items[it->index]->d_type)
		{
			case DT_DIR:
			{
				media_dirlist_t *new = calloc(1, sizeof(*new));

				if (new)
				{
					new->level = level + 1;
					new->path = malloc(strlen(it->path) + 1 + strlen(it->items[it->index]->d_name) + 1);
					if (new->path)
					{
						sprintf(new->path,"%s/%s", it->path, it->items[it->index]->d_name);
						new->nitems = scandir(new->path, &new->items, NULL, alphasort);
					}
					if (new->nitems > 0)
					{
						new->prev = it;
						new->mediaid = *pmediaid;
						/**
						 * recursive call of _find
						 */
						ret = _find(ctx, level + 1, &new, pmediaid, cb, arg);
						/**
						 * ret == 0 if all files of the directory are checked
						 * ret > 0 if at least one file is checked
						 * ret == -1 if no file are availlable in the directory
						 */
						if (ret >= 0)
							/**
							 * keep new to push the new structure for the next call of the function
							 */
							it = new;
					}
					else
					{
						_free_medialist(new, 1);
					}
				}
			}
			break;
			case DT_REG:
			{
				char *path = malloc(strlen(it->path) + 1 + strlen(it->items[it->index]->d_name) + 1);
				if (path)
				{
					sprintf(path, "%s/%s", it->path, it->items[it->index]->d_name);
					const char *mime = utils_getmime(path);
					ret = -1;
					if (strcmp(mime, mime_octetstream) != 0)
					{
						ret = cb(arg, ctx, *pmediaid, path, mime);
					}
					free(path);
					if (ret > 0)
						(*pmediaid)++;
				}
			}
			break;
			default:
			break;
		}
		if (ret == 0)
		{
			break;
		}
		it->index++;
	}
	if (it->index == it->nitems)
	{
		it = _free_medialist(it, 1);
	}

	*pit = it;
	return ret;
}

/**
 * media API
 **/
static int media_count(media_ctx_t *ctx)
{
	return ctx->count;
}

static int media_insert(media_ctx_t *ctx, const char *path, const char *info, const char *mime)
{
	return -1;
}

static int media_remove(media_ctx_t *ctx, int id, const char *path)
{
	return -1;
}

static int media_find(media_ctx_t *ctx, int id, media_parse_t cb, void *arg)
{
	int ret;
	int mediaid = 0;
	media_dirlist_t *dir = NULL;
	_find_mediaid_t mdata = {id, cb, arg};
	ret = _find(ctx, 0, &dir, &mediaid, _find_mediaid, &mdata);
	return ret? 0:1;
}

static int media_list(media_ctx_t *ctx, media_parse_t cb, void *arg)
{
	int ret;
	int mediaid = 0;
	media_dirlist_t *dir = NULL;
	_find_mediaid_t mdata = {-1, cb, arg};
	ret = _find(ctx, 0, &dir, &mediaid, _find_mediaid, &mdata);
	return ret;
}

static int media_play(media_ctx_t *ctx, media_parse_t cb, void *arg)
{
	if (ctx->mediaid >= 0)
	{
		int ret = -1;
		ret = media_find(ctx, ctx->mediaid, cb, arg);
		if (ret == 0)
			ctx->mediaid = -1;
	}
	return ctx->mediaid;
}

static int media_next(media_ctx_t *ctx)
{
	int ret;

	if (ctx->options & OPTION_RANDOM)
	{
		ctx->mediaid = (random() % (ctx->count - 1));
		_find_mediaid_t data = {ctx->mediaid, NULL, NULL};
		ret = _find(ctx, 0, &ctx->current, &ctx->mediaid, _find_mediaid, &data);
		if (ctx->mediaid > ctx->count)
			ctx->mediaid = 0;
		if (ctx->mediaid == 0)
			ctx->mediaid = ctx->count;
		ctx->mediaid--;
	}
	else
	{
		_find_mediaid_t data = {ctx->mediaid + 1, NULL, NULL};
		ret = _find(ctx, 0, &ctx->current, &ctx->mediaid, _find_mediaid, &data);
	}
	if (ctx->firstmediaid == -1)
		ctx->firstmediaid = ctx->mediaid;
	if (ret != 0)
	{
		if (ctx->count < ctx->mediaid)
			ctx->count = ctx->mediaid;
		ctx->mediaid = -1;
		if (ctx->current)
		{
			ctx->current = _free_medialist(ctx->current, 0);
		}
		ctx->current = NULL;
		if (ctx->count > 0 &&
			((ctx->firstmediaid != ctx->mediaid) ||
				(ctx->options & OPTION_LOOP)))
		{
			media_next(ctx);
		}
	}
	return ctx->mediaid;
}

static int media_end(media_ctx_t *ctx)
{
	ctx->current = _free_medialist(ctx->current, 0);
	ctx->mediaid = -1;
	return 0;
}

/**
 * the loop requires to restart the player.
 */
static option_state_t media_loop(media_ctx_t *ctx, option_state_t enable)
{
	if (enable == OPTION_ENABLE)
		ctx->options |= OPTION_LOOP;
	else if (enable == OPTION_DISABLE)
		ctx->options &= ~OPTION_LOOP;
	return (ctx->options & OPTION_LOOP)? OPTION_ENABLE: OPTION_DISABLE;
}

static option_state_t media_random(media_ctx_t *ctx, option_state_t enable)
{
	if (enable == OPTION_ENABLE)
		ctx->options |= OPTION_RANDOM;
	else if (enable == OPTION_DISABLE)
		ctx->options &= ~OPTION_RANDOM;
	return (ctx->options & OPTION_RANDOM)? OPTION_ENABLE: OPTION_DISABLE;
}

static int _count_media(media_ctx_t *ctx)
{
	int ret;
	_find_mediaid_t data = {-1, NULL, NULL};
	_free_medialist(ctx->current, 0);
	ctx->current = NULL;
	ctx->count = 0;

	ret = _find(ctx, 0, &ctx->current, &ctx->count, _find_count, &data);
	return ctx->count;
}

#ifdef USE_INOTIFY
#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))

static void *_check_dir(void *arg)
{
	media_ctx_t *ctx = (media_ctx_t *)arg;
	while (ctx->options & OPTION_INOTIFY)
	{
		char buffer[BUF_LEN];
		int i = 0;
		int length = read(ctx->inotifyfd, buffer, BUF_LEN);

		if (length < 0)
		{
			err("inotify read");
		}

		while (i < length)
		{
			struct inotify_event *event =
				(struct inotify_event *) &buffer[i];
			if (event->len)
			{
				if ((event->mask & (IN_CREATE | IN_ISDIR)) == (IN_CREATE | IN_ISDIR))
				{
					/*
					 * dirty patch but between the event and the directory is ready
					 * it may take some time
					 */
					sleep(1);
					int count = _count_media(ctx);
					if (count > 0)
					{
						//media_next(ctx);
						player_state(ctx->player, STATE_CHANGE);
					}
				}
				else if (event->mask & IN_DELETE)
				{
					int count = _count_media(ctx);
					if (count == 0)
					{
						media_end(ctx);
						player_state(ctx->player, STATE_STOP);
					}
				}
#if 0
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

static media_ctx_t *media_init(player_ctx_t *player, const char *url,...)
{
	media_ctx_t *ctx = NULL;
	if (url)
	{
		int ret;
		struct stat pathstat;
		char *query = NULL;
		char *path = utils_getpath(url, "file://", &query);
		if (path == NULL)
		{
			err("media dir: error on path %s", url);
			return NULL;
		}
		ret = stat(path, &pathstat);
		if (ret != 0)
		{
			err("media dir: error %s %s", path, strerror(errno));
			return NULL;
		}
		if (! S_ISDIR(pathstat.st_mode))
		{
			err("media dir: error %s in not a directory", path);
			return NULL;
		}

		ctx = calloc(1, sizeof(*ctx));
		ctx->player = player;
		ctx->url = url;
		ctx->root = path;
		ctx->mediaid = -1;
		ctx->firstmediaid = 0;
		_count_media(ctx);

#ifdef USE_INOTIFY
		ctx->inotifyfd = inotify_init();
		ctx->dirfd = inotify_add_watch(ctx->inotifyfd, path,
						IN_MODIFY | IN_CREATE | IN_DELETE);
		pthread_create(&ctx->thread, NULL, _check_dir, (void *)ctx);
		ctx->options |= OPTION_INOTIFY;
#endif
	}
	else
			err("media dir: error on url");
	return ctx;
}

static void media_destroy(media_ctx_t *ctx)
{
#ifdef USE_INOTIFY
	ctx->options &= ~OPTION_INOTIFY;
	pthread_cancel(ctx->thread);
	pthread_join(ctx->thread, NULL);
	inotify_rm_watch(ctx->inotifyfd, ctx->dirfd);
	close(ctx->inotifyfd);
#endif
	if (ctx->root != NULL)
		free(ctx->root);
	free(ctx);
}

const media_ops_t *media_dir = &(const media_ops_t)
{
	.init = media_init,
	.destroy = media_destroy,
	.next = media_next,
	.play = media_play,
	.list = media_list,
	.find = media_find,
	.remove = media_remove,
	.insert = media_insert,
	.count = media_count,
	.end = media_end,
	.random = media_random,
	.loop = media_loop,
};
