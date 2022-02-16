#ifndef __FILTER_H__
#define __FILTER_H__

#include <stdint.h>

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


#define FILTER_SAMPLED 1
#define FILTER_MONOLEFT 2
#define FILTER_MONORIGHT 3
#define FILTER_MONOMIXED 4
#define FILTER_FORMAT 5
#define FILTER_SAMPLERATE 6

#ifndef FILTER_CTX
typedef void filter_ctx_t;
#endif
typedef sample_t (*sampled_t)(void * ctx, sample_t sample, int bitlength, int channel);

typedef struct filter_ops_s filter_ops_t;
struct filter_ops_s
{
	const char *name;
	filter_ctx_t *(*init)(jitter_format_t format, int samplerate);
	int (*set)(filter_ctx_t *ctx,...);
	int (*run)(filter_ctx_t *ctx, filter_audio_t *audio, unsigned char *buffer, size_t size);
	void (*destroy)(filter_ctx_t *);
};

typedef struct filter_s filter_t;
struct filter_s
{
	const filter_ops_t *ops;
	filter_ctx_t *ctx;
};

const filter_ops_t *filter_build(const char *name);

/**
 * boost filter sampled
 */
typedef struct boost_s boost_t;
struct boost_s
{
	int replaygain;
	int rgshift;
	float coef;
	sample_t (*cb)(boost_t *ctx, sample_t sample, int bitspersample, int channel);
};
boost_t *boost_init(boost_t *input, int db);
sample_t boost_cb(void *arg, sample_t sample, int bitspersample);

/**
 * statistics filter sampled
 */
typedef struct stats_s stats_t;
struct stats_s
{
	uint64_t nbs;
	long double rms;
	uint32_t mean;
	uint32_t peak;
	int bitspersample;
};

stats_t *stats_init(stats_t *input);
sample_t stats_cb(void *arg, sample_t sample, int bitspersample, int channel);
#endif
