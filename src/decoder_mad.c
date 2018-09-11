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
#include "filter.h"
typedef struct decoder_s decoder_t;
typedef struct decoder_ctx_s decoder_ctx_t;
struct decoder_ctx_s
{
	const decoder_t *ops;
	struct mad_decoder decoder;
	int nchannels;
	pthread_t thread;
	jitter_t *in;
	unsigned char *inbuffer;
	jitter_t *out;
	unsigned char *buffer;
	size_t bufferlen;
	filter_t filter;
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

	len = ctx->in->ops->length(ctx->in->ctx);
	if (ctx->inbuffer == NULL)
	{
		dbg("end of decode");
		return MAD_FLOW_STOP;
	}
	//input is called for each frame when there is
	// not enought data to decode the "next frame"
	if (stream->next_frame)
		stream->next_frame = ctx->inbuffer;

	mad_stream_buffer(stream, ctx->inbuffer, len);

	return MAD_FLOW_CONTINUE;
}

static
enum mad_flow output(void *data,
		     struct mad_header const *header,
		     struct mad_pcm *pcm)
{
	decoder_ctx_t *ctx = (decoder_ctx_t *)data;
	filter_audio_t audio;

	/* pcm->samplerate contains the sampling frequency */

	audio.samplerate = pcm->samplerate;
	if (ctx->out->ctx->frequence == 0)
	{
		decoder_dbg("decoder change samplerate to %u", pcm->samplerate);
		ctx->out->ctx->frequence = pcm->samplerate;
	}
	else if (ctx->out->ctx->frequence != pcm->samplerate)
	{
		err("decoder: samplerate %d not supported", ctx->out->ctx->frequence);
	}

	audio.nchannels = pcm->channels;
	audio.nsamples = pcm->length;
	int i;
	for (i = 0; i < audio.nchannels && i < MAXCHANNELS; i++)
		audio.samples[i] = pcm->samples[i];
	decoder_dbg("decoder: audio frame %d Hz, %d channels, %d samples", audio.samplerate, audio.nchannels, audio.nsamples);

	unsigned int nsamples;
	if (audio.nchannels == 1)
		audio.samples[1] = audio.samples[0];
	while (audio.nsamples > 0)
	{
		if (ctx->buffer == NULL)
		{
			ctx->buffer = ctx->out->ops->pull(ctx->out->ctx);
		}

		ctx->bufferlen +=
			ctx->filter.ops->run(ctx->filter.ctx, &audio,
				ctx->buffer + ctx->bufferlen,
				ctx->out->ctx->size - ctx->bufferlen);
		if (ctx->bufferlen >= ctx->out->ctx->size)
		{
			ctx->out->ops->push(ctx->out->ctx, ctx->out->ctx->size, NULL);
			ctx->buffer = NULL;
			ctx->bufferlen = 0;
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
static decoder_ctx_t *mad_init(player_ctx_t *player)
{
	decoder_ctx_t *ctx = calloc(1, sizeof(*ctx));
	ctx->ops = decoder_mad;
	ctx->nchannels = 2;

	mad_decoder_init(&ctx->decoder, ctx,
			input, 0 /* header */, 0 /* filter */, output,
			error, 0 /* message */);

	jitter_t *jitter = jitter_ringbuffer_init(jitter_name, NBUFFER, BUFFERSIZE);
	ctx->in = jitter;
	jitter->format = MPEG2_3_MP3;

	return ctx;
}

static jitter_t *mad_jitter(decoder_ctx_t *decoder)
{
	return decoder->in;
}

static void *mad_thread(void *arg)
{
	int result = 0;
	decoder_ctx_t *ctx = (decoder_ctx_t *)arg;
	/* start decoding */
	result = mad_decoder_run(&ctx->decoder, MAD_DECODER_MODE_SYNC);
	/**
	 * push the last buffer to the encoder, otherwise the next
	 * decoder will begins with a pull buffer
	 */
	if (ctx->bufferlen > 0)
	{
		ctx->out->ops->push(ctx->out->ctx, ctx->bufferlen, NULL);
	}

	return (void *)result;
}

static int mad_run(decoder_ctx_t *ctx, jitter_t *jitter)
{
	int samplesize = 4;
	int nchannels = 2;
	ctx->out = jitter;
	switch (ctx->out->format)
	{
	case PCM_16bits_LE_mono:
		samplesize = 2;
		nchannels = 1;
	break;
	case PCM_16bits_LE_stereo:
		samplesize = 2;
		nchannels = 2;
	break;
	case PCM_24bits_LE_stereo:
		samplesize = 3;
		nchannels = 2;
	break;
	case PCM_32bits_BE_stereo:
	case PCM_32bits_LE_stereo:
		samplesize = 4;
		nchannels = 2;
	break;
	default:
		err("decoder out format not supported %d", ctx->out->format);
		return -1;
	}
	ctx->filter.ops = filter_pcm;
	ctx->filter.ctx = ctx->filter.ops->init(jitter->ctx->frequence, samplesize, nchannels);

	pthread_create(&ctx->thread, NULL, mad_thread, ctx);
	return 0;
}

static void mad_destroy(decoder_ctx_t *ctx)
{
	pthread_join(ctx->thread, NULL);
	/* release the decoder */
	mad_decoder_finish(&ctx->decoder);
	ctx->filter.ops->destroy(ctx->filter.ctx);
	jitter_ringbuffer_destroy(ctx->in);
	free(ctx);
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
