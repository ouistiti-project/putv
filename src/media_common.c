/*****************************************************************************
 * media_common.c
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <limits.h>

#ifdef USE_ID3TAG
#include <id3tag.h>
#include <jansson.h>
#define N_(string) string
#endif

#ifdef USE_OGGMETADDATA
#include <FLAC/metadata.h>
#endif

#include "media.h"
#include "decoder.h"
#include "player.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define media_dbg(...)

const char const *mime_octetstream = "octet/stream";
const char const *mime_audiomp3 = "audio/mp3";
const char const *mime_audioflac = "audio/flac";
const char const *mime_audioalac = "audio/alac";
const char const *mime_audiopcm = "audio/pcm";

const char const *str_title = "title";
const char const *str_artist = "artist";
const char const *str_album = "album";
const char const *str_track = "track";
const char const *str_year = "year";
const char const *str_genre = "genre";
const char const *str_date = "date";
const char const *str_comment = "comment";
const char const *str_cover = "cover";

void utils_srandom()
{
	unsigned int seed;
	if (!access(RANDOM_DEVICE, R_OK))
	{
		int fd = open(RANDOM_DEVICE, O_RDONLY);
		sched_yield();
		read(fd, &seed, sizeof(seed));
		close(fd);
	}
	else
	{
		seed = time(NULL);
	}
	srandom(seed);
}

const char *utils_getmime(const char *path)
{
#ifdef DECODER_MAD
	if (!decoder_mad->check(path))
		return decoder_mad->mime(NULL);
#endif
#ifdef DECODER_FLAC
	if (!decoder_flac->check(path))
		return decoder_flac->mime(NULL);
#endif
#ifdef DECODER_PASSTHROUGH
	if (!decoder_passthrough->check(path))
		return decoder_passthrough->mime(NULL);
#endif
	return mime_octetstream;
}

char *utils_getpath(const char *url, const char *proto, char **query)
{
	char *newpath = NULL;
	const char *path = strstr(url, proto);
	if (path == NULL)
	{
		if (strstr(url, "://"))
		{
			return NULL;
		}
		path = url;
	}
	else
		path += strlen(proto);
	if (!strncmp(path,"://", 3))
		path+=3;
	int length = strlen(path);
	*query = strchr(path, '?');
	if (*query != NULL)
	{
		length -= strlen(*query);
		*query += 1;
	}
	if (path[0] == '~')
	{
		path++;
		if (path[0] == '/')
			path++;

		char *home = getenv("HOME");
		length += strlen(home);
		newpath = malloc(length + 2);
		snprintf(newpath, length + 2, "%s/%s", home, path);
	}
	else
	{
		/*
		 * TO CHECK
		 * on Buildroot:
		 * if the calloc allocate (length + 1) as necessary,
		 * for an absolute path the next call to calloc will crash.
		 * for a relative path, the application runs fine.
		 * if the calloc allocate (length + 2)
		 * the application runs fine in any cases.
		 * For the HOME with an absolute path (see under),
		 * the application runs fine in any cases.
		 */
		newpath = calloc(1, length + 2);
		strncpy(newpath, path, length + 1);
		newpath[length] = '\0';
	}
	newpath[length + 1] = 0;
	return newpath;
}

char *utils_parseurl(const char *url, char **protocol, char **host, char **port, char **path, char **search)
{
	char *turl = malloc(strlen(url) + 1 + 1);
	strcpy(turl, url);

	char *str_protocol = turl;
	char *str_host = strstr(turl, "://");
	if (str_host == NULL)
	{
		if (protocol)
			*protocol = NULL;
		if (host)
			*host = NULL;
		if (path)
			*path = turl;
		return turl;
	}
	*str_host = '\0';
	str_host += 3;
	char *str_port = strchr(str_host, ':');
	char *str_path = strchr(str_host, '/');
	char *str_search = strchr(str_host, '?');

	if (str_port != NULL)
	{
		if (str_path && str_path < str_port)
		{
			str_port = NULL;
		}
		else if (str_search && str_search < str_port)
		{
			str_port = NULL;
		}
		else
		{
			*str_port = '\0';
			str_port += 1;
		}
	}
	if (!strncmp(str_protocol, "file", 4))
		str_path = str_host;
	if (str_path != NULL)
	{
		if (str_search && str_search < str_path)
		{
			str_path = NULL;
		}
		else
		{
			memmove(str_path + 1, str_path, strlen(str_path) + 1);
			*str_path = '\0';
			str_path += 1;
		}
	}
	if (str_search != NULL)
	{
		*str_search = '\0';
		str_search += 1;
	}
	if (protocol)
		*protocol = str_protocol;
	if (host)
		*host = str_host;
	if (port)
		*port = str_port;
	if (path)
		*path = str_path;
	if (search)
		*search = str_search;
	return turl;
}

const char *utils_mime2mime(const char *mime)
{
	const char *mime2 = NULL;
	if (mime != NULL)
	{
		if (!strcmp(mime, mime_audiomp3))
			mime2 = mime_audiomp3;
		if (!strcmp(mime, mime_audioflac))
			mime2 = mime_audioflac;
		if (!strcmp(mime, mime_audiopcm))
			mime2 = mime_audiopcm;
	}
	return mime2;
}

const char *utils_format2mime(jitter_format_t format)
{
	switch (format)
	{
	case PCM_16bits_LE_mono:
	case PCM_16bits_LE_stereo:
	case PCM_24bits3_LE_stereo:
	case PCM_24bits4_LE_stereo:
	case PCM_32bits_LE_stereo:
	case PCM_32bits_BE_stereo:
		return mime_audiopcm;
	case MPEG2_3_MP3:
		return mime_audiomp3;
	case FLAC:
		return mime_audioflac;
	case MPEG2_1:
	case MPEG2_2:
		return mime_octetstream;
	case DVB_frame:
	case SINK_BITSSTREAM:
	default:
		break;
	}
	return mime_octetstream;
}

static char *media_regfile(char *path, const char *mime, const unsigned char *data, unsigned long length)
{
	int fd = -1;
	char *ext = strrchr(path, '.');
	if (!strcmp(mime,"image/png"))
		strcpy(ext, ".png");
	else if (!strcmp(mime,"image/jpeg"))
		strcpy(ext, ".jpg");
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd > 0)
	{
		write(fd, data, length);
		close(fd);
	}
	return path;
}

static char *media_tmpfile(char *path, const char *mime, const unsigned char *data, unsigned long length)
{
	static int fd = -1;
	if (fd > 0)
		close(fd);
	char *ext = strrchr(path, '.');
	if (!strcmp(mime,"image/png"))
		strcpy(ext, "XXXXXX.png");
	if (!strcmp(mime,"image/jpeg"))
		strcpy(ext, "XXXXXX.jpg");
	fd = mkstemps(path, 4);
	if (fd > 0)
	{
		write(fd, data, length);
	}
	return path;
}

#ifdef USE_ID3TAG
int media_parseid3tag(const char *path, json_t *object)
{
	struct
	{
		char const *id;
		char const *label;
	} const labels[] =
	{
	{ ID3_FRAME_TITLE,  N_(str_title)     },
	{ ID3_FRAME_ARTIST, N_(str_artist)    },
	{ ID3_FRAME_ALBUM,  N_(str_album)     },
	{ ID3_FRAME_TRACK,  N_(str_track)     },
	{ ID3_FRAME_YEAR,   N_(str_year)      },
	{ ID3_FRAME_GENRE,  N_(str_genre)     },
	{ ID3_FRAME_COMMENT,N_(str_comment)   },
	{ "APIC",           N_(str_cover)   },
	};
	struct id3_file *fd = id3_file_open(path, ID3_FILE_MODE_READONLY);
	if (fd == NULL)
		return -1;
	struct id3_tag *tag = id3_file_tag(fd);

	int i;
#if 0
	for (i = 0; i < tag->nframes; i++)
	{
		struct id3_frame const *frame = NULL;
		frame = tag->frames[i];
		warn("frame %s", frame->id);
		media_dbg("field 0 type %d", field->type);
	}
#endif
	for (i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
	{
		json_t *value = NULL;
		struct id3_frame const *frame = NULL;
		int j = 0;
		frame = id3_tag_findframe(tag, labels[i].id, j);
		while (frame != NULL)
		{
			int fieldid = 0;
			union id3_field const *field;
			enum id3_field_textencoding encoding = ID3_FIELD_TEXTENCODING_ISO_8859_1;
			const unsigned char *data = NULL;
			unsigned long length = 0;
			const char *info[5];
			char *tinfo[5] = {0};

			for (fieldid = 0; (field = id3_frame_field(frame, fieldid)) != NULL && fieldid < 5; fieldid++)
			{
				media_dbg("field[%s][%d] %d", frame->id, fieldid, id3_field_type(field));
				switch (id3_field_type(field))
				{
				case ID3_FIELD_TYPE_TEXTENCODING:
					encoding = id3_field_gettextencoding(field);
				break;
				case ID3_FIELD_TYPE_FRAMEID:
				{
					info[fieldid] = id3_field_getframeid(field);
				}
				break;
				case ID3_FIELD_TYPE_STRINGLIST:
				{
					int nb = 0;
					int k = 0;
					nb = id3_field_getnstrings(field);
					if (nb > 1)
					{
						value = json_array();
						for (k = 0; k < nb; k++)
						{
							json_t *fieldvalue;
							id3_ucs4_t const *ucs4 = NULL;
							ucs4 = id3_field_getstrings(field, k);
							if (labels[i].id == ID3_FRAME_GENRE)
								ucs4 = id3_genre_name(ucs4);
							char *latin1 = id3_ucs4_utf8duplicate(ucs4);
							fieldvalue = json_string(latin1);
							free(latin1);
							json_array_append(value, fieldvalue);
						}
					}
					else if (nb == 1)
					{
						json_t *fieldvalue;
						id3_ucs4_t const *ucs4 = NULL;
						ucs4 = id3_field_getstrings(field, k);
						if (labels[i].id == ID3_FRAME_GENRE)
							ucs4 = id3_genre_name(ucs4);
						char *latin1 = id3_ucs4_utf8duplicate(ucs4);
						fieldvalue = json_string(latin1);
						free(latin1);
						value = fieldvalue;
					}
				}
				break;
				case ID3_FIELD_TYPE_INT8:
				case ID3_FIELD_TYPE_INT16:
				case ID3_FIELD_TYPE_INT24:
				case ID3_FIELD_TYPE_INT32:
				{
					unsigned long integer = id3_field_getint(field);
				}
				break;
				case ID3_FIELD_TYPE_STRING:
				{
					id3_ucs4_t const *ucs4 = NULL;
					ucs4 = id3_field_getstring(field);
					tinfo[fieldid] = id3_ucs4_utf8duplicate(ucs4);
				}
				break;
				case ID3_FIELD_TYPE_LATIN1:
				{
					info[fieldid] = id3_field_getlatin1(field);
				}
				break;
				case ID3_FIELD_TYPE_BINARYDATA:
				{
					char coverpath[PATH_MAX];
					data = id3_field_getbinarydata(field, &length);
					strcpy(coverpath, path);
					char *name = strrchr(coverpath, '/');
					if (name != NULL)
						name++;
					else
						name = coverpath;
					if (tinfo[3] != NULL)
#if 0
						strcpy(name, tinfo[3]);
					else
#else
					{
						if (strstr(tinfo[3], ".jpg") != NULL)
							strcpy(name, "cover.jpg");
						else
							strcpy(name, "cover.png");
					}
					else
#endif
						strcpy(name, "cover.png");
					value = json_string(media_regfile(coverpath, info[1], data, length));
				}
				break;
				}
			}
			for (fieldid = 0; fieldid < 5; fieldid++)
				if (tinfo[fieldid] != NULL)
					free(tinfo[fieldid]);
			if (value == NULL)
				value = json_null();

			json_object_set(object, labels[i].label, value);

			j++;
			frame = id3_tag_findframe(tag, labels[i].id, j);
		}
	}
	id3_file_close(fd);
	return 0;
}
#endif

#ifdef USE_OGGMETADDATA
int media_parseoggmetadata(const char *path, json_t *object)
{
	struct
	{
		char const *id;
		char const *label;
		int const length;
		enum {
			label_string,
			label_integer,
		} type;
	} const labels[] =
	{
		{str_title, str_title, 5, label_string},
		{str_album, str_album, 5, label_string},
		{str_artist, str_artist, 6, label_string},
		{str_year, str_year, 4, label_integer},
		{str_genre, str_genre, 5, label_string},
		{str_date, str_year, 4, label_integer},
		{"TRACKNUMBER", str_track, 11, label_integer},
	};
	FLAC__StreamMetadata *vorbiscomment = NULL;
	if (!FLAC__metadata_get_tags(path, &vorbiscomment))
	{
		warn("media: no Vorbis Comment for %s", path);
		return -1;
	}
	FLAC__StreamMetadata_VorbisComment *data;
	data = &vorbiscomment->data.vorbis_comment;
	int n;
	for (n = 0; n < data->num_comments; n++)
	{
		FLAC__StreamMetadata_VorbisComment_Entry *comments;
		comments = &data->comments[n];
		json_t *value;
		int i;
		for (i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
		{
			const char *svalue = comments->entry + labels[i].length;
			if (!strncasecmp(comments->entry, labels[i].id, labels[i].length) &&
				svalue[0] == '=')
			{
				svalue++;
				switch(labels[i].type)
				{
				case label_string:
					value = json_string(svalue);
				break;
				case label_integer:
					value = json_integer(atoi(svalue));
				break;
				}
				json_object_set(object, labels[i].label, value);
			}
		}
	}
	FLAC__metadata_object_delete(vorbiscomment);

	/**
	 * picture
	 */
	FLAC__StreamMetadata *vorbispicture;
	FLAC__metadata_get_picture(path, &vorbispicture, FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER, NULL, NULL, -1, -1, -1, -1);
	if (vorbispicture != NULL)
	{
		FLAC__StreamMetadata_Picture *picture;
		picture = &vorbispicture->data.picture;

		char coverpath[PATH_MAX];
		strncpy(coverpath, path, PATH_MAX - 10);
		char *name = strrchr(coverpath, '/');
		if (name != NULL)
			name++;
		else
			name = coverpath;
		strcpy(name, "cover.png");

		json_t *value;
		value = json_string(media_regfile(coverpath, picture->mime_type, picture->data, picture->data_length));
		json_object_set(object, str_cover, value);

		FLAC__metadata_object_delete(vorbispicture);
	}
	return 0;
}
#endif

static char *current_path;
media_t *media_build(player_ctx_t *player, const char *url)
{
	if (url == NULL)
		return NULL;

	const media_ops_t *const media_list[] = {
	#ifdef MEDIA_DIR
		media_dir,
	#endif
	#ifdef MEDIA_SQLITE
		media_sqlite,
	#endif
	#ifdef MEDIA_FILE
		media_file,
	#endif
		NULL
	};

	char *oldpath = current_path;
	current_path = strdup(url);

	int i = 0;
	media_ctx_t *media_ctx = NULL;
	while (media_list[i] != NULL)
	{
		media_ctx = media_list[i]->init(player, current_path);
		if (media_ctx != NULL)
			break;
		i++;
	}
	if (media_ctx == NULL)
	{
		free(current_path);
		current_path = oldpath;
		err("media not found %s", url);
		return NULL;
	}
	media_t *media = calloc(1, sizeof(*media));
	media->ops = media_list[i];
	media->ctx = media_ctx;
	if (oldpath)
		free(oldpath);

	return media;
}

const char *media_path()
{
	return current_path;
}
