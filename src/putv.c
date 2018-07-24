/*****************************************************************************
 * putv.c
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
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <sqlite3.h>

#include "putv.h"
#include "jitter.h"
#include "src.h"
#include "decoder.h"
#include "encoder.h"
#include "sink.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

typedef struct player_event_s player_event_t;
struct player_event_s
{
	player_event_type_t type;
	player_event_cb_t cb;
	void *ctx;
	player_event_t *next;
};

struct mediaplayer_ctx_s
{
	sqlite3 *db;
	int mediaid;
	state_t state;
	player_event_t *events;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
};

const char const *mime_mp3 = "audio/mp3";
const char const *mime_octetstream = "octet/stream";

static const char *utils_getmime(const char *path)
{
	char *ext = strrchr(path, '.');
	if (!strcmp(ext, ".mp3"))
		return mime_mp3;
	return mime_octetstream;
}

static int _execute(sqlite3_stmt *statement)
{
	int id = -1;
	int ret;
	ret = sqlite3_step(statement);
	dbg("execute %d", ret);
	while (ret == SQLITE_ROW)
	{
		int i = 0, nbColumns = sqlite3_column_count(statement);
		if (i < nbColumns)
		{
			//const char *key = sqlite3_column_name(statement, i);
			if (sqlite3_column_type(statement, i) == SQLITE_INTEGER)
			{
				id = sqlite3_column_int(statement, i);
			}
		}
		ret = sqlite3_step(statement);
	}
	return id;
}

int media_count(mediaplayer_ctx_t *ctx)
{
	sqlite3 *db = ctx->db;
	int count = 0;
	sqlite3_stmt *statement;
	int size = 256;
	char *sql = sqlite3_malloc(size);
	snprintf(sql, size, "select \"id\" from \"media\" ");
	sqlite3_prepare_v2(db, sql, size, &statement, NULL);

	int ret = sqlite3_step(statement);
	if (ret != SQLITE_ERROR)
	{
		do
		{
			count++;
			ret = sqlite3_step(statement);
		} while (ret == SQLITE_ROW);
	}
	sqlite3_finalize(statement);
	sqlite3_free(sql);

	return count;
}

static int findmedia(sqlite3 *db, const char *path)
{
	sqlite3_stmt *statement;
	int size = 256;
	char *sql = sqlite3_malloc(size);
	snprintf(sql, size, "select \"id\" from \"media\" where \"url\"=\"@PATH\"");
	sqlite3_prepare_v2(db, sql, size, &statement, NULL);
	/** set the default value of @FIELDS **/
	sqlite3_bind_text(statement, sqlite3_bind_parameter_index(statement, "@PATH"), path, -1, SQLITE_STATIC);

	int id = _execute(statement);

	sqlite3_finalize(statement);
	sqlite3_free(sql);
	return id;
}

int media_insert(mediaplayer_ctx_t *ctx, const char *path, const char *info, const char *mime)
{
	sqlite3 *db = ctx->db;

	if (path == NULL)
		return -1;

	int id = findmedia(db, path);
	if (id != -1)
		return 0;

	int ret = 0;
	sqlite3_stmt *statement;
	int size = 1024;
	char *sql;
	sql = sqlite3_malloc(size);
	snprintf(sql, size, "insert into \"media\" (\"url\", \"mime\", \"info\") values(@PATH , @MIME , @INFO);");

	ret = sqlite3_prepare_v2(db, sql, size, &statement, NULL);
	if (ret != SQLITE_OK)
		return -1;

	int index;
	index = sqlite3_bind_parameter_index(statement, "@PATH");
	ret = sqlite3_bind_text(statement, index, path, -1, SQLITE_STATIC);
	index = sqlite3_bind_parameter_index(statement, "@INFO");
	if (info != NULL)
		ret = sqlite3_bind_text(statement, index, info, -1, SQLITE_STATIC);
	else
		ret = sqlite3_bind_null(statement, index);
	index = sqlite3_bind_parameter_index(statement, "@MIME");
	if (mime == NULL)
		mime = utils_getmime(path);
	if (mime)
		ret = sqlite3_bind_text(statement, index, mime, -1, SQLITE_STATIC);
	else
		ret = sqlite3_bind_null(statement, index);

	ret = sqlite3_step(statement);
	if (ret != SQLITE_DONE)
		ret = -1;
	else
	{
		dbg("putv: new media %s", path);
	}
	sqlite3_finalize(statement);
	sqlite3_free(sql);

	return ret;
}

int media_find(mediaplayer_ctx_t *ctx, int id, char *url, int *urllen, char *info, int *infolen)
{
	sqlite3_stmt *statement;
	int size = 256;
	char *sql = sqlite3_malloc(size);
	snprintf(sql, size, "select \"url\" \"info\" from \"media\" where id = @ID");
	sqlite3_prepare_v2(ctx->db, sql, size, &statement, NULL);

	int index = sqlite3_bind_parameter_index(statement, "@ID");
	sqlite3_bind_int(statement, index, id);

	int ret = sqlite3_step(statement);
	if (ret == SQLITE_ROW)
	{
		int len = sqlite3_column_bytes(statement, 0);
		*urllen = (*urllen > len)? len: *urllen;
		len = sqlite3_column_bytes(statement, 1);
		*infolen = (*infolen > len)? len: *infolen;
		strncpy(url, (const char *)sqlite3_column_text(statement, 0), *urllen);
		strncpy(info, (const char *)sqlite3_column_text(statement, 1), *infolen);
		ret = 0;
	}
	else
		ret = -1;
	sqlite3_finalize(statement);
	sqlite3_free(sql);
	return ret;
}

int media_current(mediaplayer_ctx_t *ctx, char *url, int *urllen, char *info, int *infolen)
{
	return media_find(ctx, ctx->mediaid, url, urllen, info, infolen);
}

int media_next(mediaplayer_ctx_t *ctx)
{
	sqlite3_stmt *statement;
	int size = 256;
	char *sql = sqlite3_malloc(size);
	if (ctx->mediaid != 0)
	{
		snprintf(sql, size, "select \"id\" from \"media\" where id > @ID");
		sqlite3_prepare_v2(ctx->db, sql, size, &statement, NULL);

		int index = sqlite3_bind_parameter_index(statement, "@ID");
		sqlite3_bind_int(statement, index, ctx->mediaid);
	}
	else
	{
		snprintf(sql, size, "select \"id\" from \"media\"");
		sqlite3_prepare_v2(ctx->db, sql, size, &statement, NULL);
	}

	int ret = sqlite3_step(statement);
	if (ret == SQLITE_ROW)
	{
		ctx->mediaid = sqlite3_column_int(statement, 0);
	}
	else
		ctx->mediaid = -1;
	sqlite3_finalize(statement);
	sqlite3_free(sql);
	return ctx->mediaid;
}

typedef int (*play_fcn_t)(void *arg, const char *url, const char *info, const char *mime);
int media_play(mediaplayer_ctx_t *ctx, play_fcn_t play, void *data)
{
	int ret = -1;
	sqlite3_stmt *statement;
	int size = 256;
	char *sql = sqlite3_malloc(size);
	snprintf(sql, size, "select \"url\" \"mime\" from \"media\" where id = @ID");
	sqlite3_prepare_v2(ctx->db, sql, size, &statement, NULL);

	int index = sqlite3_bind_parameter_index(statement, "@ID");
	sqlite3_bind_int(statement, index, ctx->mediaid);

	const char *url = NULL;
	const char *info = NULL;
	const char *mime = NULL;
	int sqlret = sqlite3_step(statement);
	if (sqlret == SQLITE_ROW)
	{
		url = (const char *)sqlite3_column_text(statement, 0);
		mime = (const char *)sqlite3_column_text(statement, 1);
	}
	if (url != NULL)
	{
		ret = play(data, url, info, mime);
	}
	sqlite3_finalize(statement);
	sqlite3_free(sql);
	return ret;
}

mediaplayer_ctx_t *player_init(const char *dbpath)
{
	mediaplayer_ctx_t *ctx = calloc(1, sizeof(*ctx));
	pthread_mutex_init(&ctx->mutex, NULL);
	pthread_cond_init(&ctx->cond, NULL);
	if (dbpath)
	{
		int ret;
		if (!access(dbpath, R_OK|W_OK))
		{
			ret = sqlite3_open_v2(dbpath, &ctx->db, SQLITE_OPEN_READWRITE, NULL);
		}
		else if (!access(dbpath, R_OK))
		{
			ret = sqlite3_open_v2(dbpath, &ctx->db, SQLITE_OPEN_READONLY, NULL);
		}
		else
		{
			ret = sqlite3_open_v2(dbpath, &ctx->db, SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE, NULL);

			const char *query[] = {
				"create table media (\"id\" INTEGER PRIMARY KEY, \"url\" TEXT UNIQUE NOT NULL, \"mime\" TEXT, \"info\" BLOB);",
				NULL,
			};
			char *error = NULL;
			int i = 0;
			while (query[i] != NULL)
			{
				if (ret != SQLITE_OK)
				{
					break;
				}
				ret = sqlite3_exec(ctx->db, query[i], NULL, NULL, &error);
				i++;
			}
		}
		if (ret != SQLITE_OK)
			ctx->db = NULL;
		else
		{
			dbg("open db %s", dbpath);
		}
	}
	return ctx;
}

void player_destroy(mediaplayer_ctx_t *ctx)
{
	pthread_cond_destroy(&ctx->cond);
	pthread_mutex_destroy(&ctx->mutex);
	free(ctx);
}

void player_onchange(mediaplayer_ctx_t *ctx, player_event_cb_t callback, void *cbctx)
{
	player_event_t *event = calloc(1, sizeof(*event));
	event->cb = callback;
	event->ctx = cbctx;
	event->type = EVENT_ONCHANGE;
	event->next = ctx->events;
	ctx->events = event; 
}

state_t player_state(mediaplayer_ctx_t *ctx, state_t state)
{
	if ((state != STATE_UNKNOWN) && ctx->state != state)
	{
		ctx->state = state;
		pthread_cond_broadcast(&ctx->cond);
		player_event_t *it = ctx->events;
		while (it != NULL)
		{
			it->cb(it->ctx, ctx);
			it = it->next;
		}
	}
	
	return ctx->state;
}

int player_waiton(mediaplayer_ctx_t *ctx, int state)
{
	if (ctx->state == STATE_STOP)
		return -1;
	pthread_mutex_lock(&ctx->mutex);
	while (ctx->state == state)
	{
		pthread_cond_wait(&ctx->cond, &ctx->mutex);
	}
	pthread_mutex_unlock(&ctx->mutex);
	return 0;
}

struct _player_play_t
{
	mediaplayer_ctx_t *ctx;
	jitter_t *jitter_encoder;
};

static int _player_play(void* arg, const char *url, const char *info, const char *mime)
{
	struct _player_play_t *player = (struct _player_play_t *)arg;
	mediaplayer_ctx_t *ctx = player->ctx;
	const decoder_t *decoder = NULL;
	const src_t *src = SRC;
	src_ctx_t *src_ctx = NULL;
	decoder_ctx_t *decoder_ctx = NULL;

	dbg("putv: play %s", url);

#ifdef DECODER_MAD
	if (mime && !strcmp(mime, mime_mp3))
		decoder = decoder_mad;
#endif
	if (decoder == NULL)
		decoder = DECODER;

	if (decoder != NULL)
	{
		decoder_ctx = decoder->init(ctx);
		src_ctx = src->init(ctx, url);
	}
	if (src_ctx != NULL)
	{
		decoder->run(decoder_ctx, player->jitter_encoder);
		src->run(src_ctx, decoder->jitter(decoder_ctx));
		decoder->destroy(decoder_ctx);
		src->destroy(src_ctx);
	}
	return 0;
}

int player_run(mediaplayer_ctx_t *ctx)
{
	const sink_t *sink;
	sink_ctx_t *sink_ctx;
	const encoder_t *encoder;
	encoder_ctx_t *encoder_ctx;

	jitter_t *jitter[3];

	sink = SINK;
	sink_ctx = sink->init(ctx, "default");
	sink->run(sink_ctx);
	jitter[0] = sink->jitter(sink_ctx);

	encoder = ENCODER;
	encoder_ctx = encoder->init(ctx);
	encoder->run(encoder_ctx,jitter[0]);
	jitter[1] = encoder->jitter(encoder_ctx);

	while (ctx->state != STATE_ERROR)
	{
		pthread_mutex_lock(&ctx->mutex);
		while (ctx->state == STATE_STOP)
		{
			dbg("putv: stop");
			pthread_cond_wait(&ctx->cond, &ctx->mutex);
		}
		pthread_mutex_unlock(&ctx->mutex);

		jitter[0]->ops->reset(jitter[0]->ctx);
		jitter[1]->ops->reset(jitter[1]->ctx);
		struct _player_play_t player =
		{
			.ctx = ctx,
			.jitter_encoder = jitter[1],
		};
		if (media_next(ctx) == -1)
			ctx->state = STATE_STOP;
		else
			media_play(ctx, _player_play, &player);
	}
	encoder->destroy(encoder_ctx);
	sink->destroy(sink_ctx);
	pthread_exit(0);
	return 0;
}
