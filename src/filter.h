#ifndef __FILTER_H__
#define __FILTER_H__

#include "jitter.h"

# define SIZEOF_INT 4

#if SIZEOF_INT >= 4
typedef   signed int sample_t;
#else
typedef   signed long sample_t;
#endif

#define MAXCHANNELS 8
typedef struct filter_audio_s filter_audio_t;
struct filter_audio_s
{
	sample_t *samples[MAXCHANNELS];
	int nsamples;
	int samplerate;
	char bitspersample;
	char nchannels;
	char regain;
};


#ifndef FILTER_CTX
typedef void filter_ctx_t;
#endif
typedef int (*sampled_t)(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out);
int sampled_change(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out);
int sampled_scaling(filter_ctx_t *ctx, sample_t sample, int bitspersample, unsigned char *out);

typedef struct filter_ops_s filter_ops_t;
struct filter_ops_s
{
	const char *name;
	filter_ctx_t *(*init)(sampled_t sampled, jitter_format_t format, ...);
	int (*set)(filter_ctx_t *ctx, sampled_t sampled, jitter_format_t format, unsigned int rate);
	int (*run)(filter_ctx_t *ctx, filter_audio_t *audio, unsigned char *buffer, size_t size);
	void (*destroy)(filter_ctx_t *);
};

typedef struct filter_s filter_t;
struct filter_s
{
	const filter_ops_t *ops;
	filter_ctx_t *ctx;
};

filter_t *filter_build(const char *name, jitter_format_t format, sampled_t sampled);

#endif
