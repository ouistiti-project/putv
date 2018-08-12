/*****************************************************************************
 * decoder_mad.c
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
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>

#include <mad.h>
#include <id3tag.h>

#include "player.h"
typedef int (*write_sample_t)(signed int sample, unsigned char *out);
typedef struct decoder_s decoder_t;
typedef struct decoder_ctx_s decoder_ctx_t;
struct decoder_ctx_s
{
	const decoder_t *ops;
	struct mad_decoder decoder;
	int nchannels;
	pthread_t thread;
	player_ctx_t *ctx;
	jitter_t *in;
	unsigned char *inbuffer;
	jitter_t *out;
	unsigned char *buffer;
	size_t bufferlen;
	write_sample_t write_sample;
};
#define DECODER_CTX
#include "decoder.h"
#include "jitter.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define decoder_dbg(...)

#define ENDIAN le
#define RENDIAN be

#define WRITEFUNC(END,BITS)	write_ ## END ## BITS
//#define WRITE_ENDIAN16 WRITEFUNC(ENDIAN,16)
#define WRITE_ENDIAN16 write_le16
#define WRITE_ENDIAN24 write_le24
#define WRITE_ENDIAN32 write_le32
#define WRITE_RENDIAN16 write_be16
#define WRITE_RENDIAN24 write_be24
#define WRITE_RENDIAN32 write_be32

static
signed int scale_16bits(mad_fixed_t sample)
{
	/* round */
	sample += (1L << (MAD_F_FRACBITS - 16));

	/* clip */
	if (sample >= MAD_F_ONE)
		sample = MAD_F_ONE - 1;
	else if (sample < -MAD_F_ONE)
		sample = -MAD_F_ONE;

	/* quantize */
	return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static
signed int scale_24bits(mad_fixed_t sample)
{
	/* round */
	sample += (1L << (MAD_F_FRACBITS - 24));

	/* clip */
	if (sample >= MAD_F_ONE)
		sample = MAD_F_ONE - 1;
	else if (sample < -MAD_F_ONE)
		sample = -MAD_F_ONE;

	/* quantize */
	return sample >> (MAD_F_FRACBITS + 1 - 24);
}

static int WRITE_RENDIAN32(signed int sample, unsigned char *out)
{
	int offset = 0;
	out[offset++] = (sample >> 24) & 0xff;
	out[offset++] = (sample >>16) & 0xff;
	out[offset++] = (sample >> 8) & 0xff;
	out[offset++] = (sample >> 0) & 0xff;
	return offset;
}

static int WRITE_RENDIAN24(signed int sample, unsigned char *out)
{
	signed int scaled = scale_24bits(sample);
	int offset = 0;
	out[offset++] = (scaled >> 16) & 0xff;
	out[offset++] = (scaled >> 8) & 0xff;
	out[offset++] = (scaled >> 0) & 0xff;
	return offset;
}

static int WRITE_RENDIAN16(signed int sample, unsigned char *out)
{
	signed int scaled = scale_16bits(sample);
	int offset = 0;
	out[offset++] = (scaled >> 8) & 0xff;
	out[offset++] = (scaled >> 0) & 0xff;
	return offset;
}

static int WRITE_ENDIAN32(signed int sample, unsigned char *out)
{
	int offset = 0;
	out[offset++] = (sample >> 0) & 0xff;
	out[offset++] = (sample >> 8) & 0xff;
	out[offset++] = (sample >> 16) & 0xff;
	out[offset++] = (sample >> 24) & 0xff;
	return offset;
}

static int WRITE_ENDIAN24(signed int sample, unsigned char *out)
{
	signed int scaled = scale_24bits(sample);
	int offset = 0;
	out[offset++] = (scaled >> 0) & 0xff;
	out[offset++] = (scaled >> 8) & 0xff;
	out[offset++] = (scaled >> 16) & 0xff;
	return offset;
}

static int WRITE_ENDIAN16(signed int sample, unsigned char *out)
{
	signed int scaled = scale_16bits(sample);
	int offset = 0;
	out[offset++] = (scaled >> 0) & 0xff;
	out[offset++] = (scaled >> 8) & 0xff;
	return offset;
}

static
enum mad_flow input(void *data,
		    struct mad_stream *stream)
{
	decoder_ctx_t *ctx = (decoder_ctx_t *)data;
	size_t len = ctx->in->ctx->size;

	if (stream->next_frame)
		len = stream->next_frame - ctx->inbuffer;
	ctx->in->ops->pop(ctx->in->ctx, len);

	ctx->inbuffer = ctx->in->ops->peer(ctx->in->ctx);

	//input is called for each frame when there is
	// not enought data to decode the "next frame"
	if (stream->next_frame)
		stream->next_frame = ctx->inbuffer;

	if (ctx->inbuffer == NULL)
	{
		return MAD_FLOW_STOP;
	}
	mad_stream_buffer(stream, ctx->inbuffer, ctx->in->ctx->size);

	return MAD_FLOW_CONTINUE;
}

static
enum mad_flow output(void *data,
		     struct mad_header const *header,
		     struct mad_pcm *pcm)
{
	decoder_ctx_t *decoder = (decoder_ctx_t *)data;
	unsigned int nchannels, nsamples;
	mad_fixed_t const *left_ch, *right_ch;
	write_sample_t write_sample = decoder->write_sample;
	unsigned int samplerate;

	/* pcm->samplerate contains the sampling frequency */

	samplerate = pcm->samplerate;
	nchannels = pcm->channels;
	nsamples  = pcm->length;
	left_ch   = pcm->samples[0];
	right_ch  = pcm->samples[1];
	decoder_dbg("decoder: audio frame %d Hz, %d channels, %d samples", samplerate, nchannels, nsamples);

	while (nsamples--)
	{
		if (decoder->buffer == NULL)
		{
			decoder->buffer = decoder->out->ops->pull(decoder->out->ctx);
		}
		signed int sample;

		sample = (signed int)*left_ch++;
		decoder->bufferlen += write_sample(sample, decoder->buffer + decoder->bufferlen);
		if (decoder->nchannels == 2) {
			if (nchannels == 2)
				sample = (signed int)*right_ch++;
			decoder->bufferlen += write_sample(sample, decoder->buffer + decoder->bufferlen);
		}
		if (decoder->bufferlen >= decoder->out->ctx->size)
		{
			decoder->buffer = NULL;
			decoder->bufferlen = 0;
			decoder->out->ops->push(decoder->out->ctx, decoder->out->ctx->size, NULL);
			if (player_waiton(decoder->ctx, STATE_PAUSE) < 0)
				return MAD_FLOW_BREAK;
		}
	}

	return MAD_FLOW_CONTINUE;
}

static
enum mad_flow error(void *data,
		    struct mad_stream *stream,
		    struct mad_frame *frame)
{
	if (MAD_RECOVERABLE(stream->error))
	{
#ifdef USE_ID3TAG
		if (stream->error == MAD_ERROR_LOSTSYNC)
		{
			signed long tagsize;
			tagsize = id3_tag_query(stream->this_frame,
					stream->bufend - stream->this_frame);
			if (tagsize > 0)
			{
#ifdef PROCESS_ID3
				struct id3_tag *tag;

				tag = get_id3(stream, tagsize, decoder);
				if (tag) {
					//process_id3(tag, player);
					id3_tag_delete(tag);
				}
#else
				mad_stream_skip(stream, tagsize);
#endif
			}
		}
		else
#endif
			dbg("decoding error 0x%04x (%s) at byte offset %p",
				stream->error, mad_stream_errorstr(stream),
				stream->this_frame );
		return MAD_FLOW_CONTINUE;
	}
	else
	{
		err("decoding error 0x%04x (%s) at byte offset %p",
			stream->error, mad_stream_errorstr(stream),
			stream->this_frame );
		return MAD_FLOW_BREAK;
	}
}

#define LATENCE 200 /*ms*/
#define BUFFERSIZE (40*LATENCE)
//#define BUFFERSIZE (1*MAD_BUFFER_MDLEN)

#define NBUFFER 3

static const char *jitter_name = "mad decoder";
static decoder_ctx_t *mad_init(player_ctx_t *ctx)
{
	decoder_ctx_t *decoder = calloc(1, sizeof(*decoder));
	decoder->ops = decoder_mad;
	decoder->ctx = ctx;
	decoder->write_sample = write_le16;
	decoder->nchannels = 2;

	mad_decoder_init(&decoder->decoder, decoder,
			input, 0 /* header */, 0 /* filter */, output,
			error, 0 /* message */);

	jitter_t *jitter = jitter_ringbuffer_init(jitter_name, NBUFFER, BUFFERSIZE);
	decoder->in = jitter;
	jitter->format = MPEG2_3_MP3;

	return decoder;
}

static jitter_t *mad_jitter(decoder_ctx_t *decoder)
{
	return decoder->in;
}

static void *mad_thread(void *arg)
{
	int result = 0;
	decoder_ctx_t *decoder = (decoder_ctx_t *)arg;
	/* start decoding */
	result = mad_decoder_run(&decoder->decoder, MAD_DECODER_MODE_SYNC);
	if (decoder->bufferlen > 0)
	{
		decoder->buffer = NULL;
		decoder->out->ops->push(decoder->out->ctx, decoder->bufferlen, NULL);
	}
	return (void *)result;
}

static int mad_run(decoder_ctx_t *decoder, jitter_t *jitter)
{
	decoder->out = jitter;
	if (decoder->out->format == PCM_16bits_LE_mono)
		decoder->nchannels = 1;
	else if (decoder->out->format == PCM_24bits_LE_stereo)
	{
		decoder->write_sample = write_le24;
	}
	else if (decoder->out->format == PCM_32bits_LE_stereo)
	{
		decoder->write_sample = write_le32;
	}
	else if (decoder->out->format == PCM_32bits_BE_stereo)
	{
		decoder->write_sample = write_be32;
	}
	else if (decoder->out->format != PCM_16bits_LE_stereo)
	{
		err("decoder out format not supported %d", decoder->out->format);
		return -1;
	}
	pthread_create(&decoder->thread, NULL, mad_thread, decoder);
	return 0;
}

static void mad_destroy(decoder_ctx_t *decoder)
{
	pthread_join(decoder->thread, NULL);
	/* release the decoder */
	mad_decoder_finish(&decoder->decoder);
	jitter_ringbuffer_destroy(decoder->in);
	free(decoder);
}

const decoder_t *decoder_mad = &(decoder_t)
{
	.init = mad_init,
	.jitter = mad_jitter,
	.run = mad_run,
	.destroy = mad_destroy,
};

#ifndef DECODER_GET
#define DECODER_GET
const decoder_t *decoder_get(decoder_ctx_t *ctx)
{
	return ctx->ops;
}
#endif
