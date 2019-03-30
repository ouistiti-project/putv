#ifndef __HEARTBEAT_H__
#define __HEARTBEAT_H__

#define MAXCHANNELS 8
typedef struct heartbeat_samples_s heartbeat_samples_t;
struct heartbeat_samples_s
{
	unsigned int nsamples;
};

#ifndef HEARTBEAT_CTX
typedef void heartbeat_ctx_t;
#endif
typedef struct heartbeat_ops_s heartbeat_ops_t;
struct heartbeat_ops_s
{
	heartbeat_ctx_t *(*init)(unsigned int rate, jitter_format_t format, unsigned int nchannels);
	int (*wait)(heartbeat_ctx_t *ctx, void *data);
	int (*lock)(heartbeat_ctx_t *ctx);
	int (*unlock)(heartbeat_ctx_t *ctx);
	void (*destroy)(heartbeat_ctx_t *);
};

typedef struct heartbeat_s heartbeat_t;
struct heartbeat_s
{
	const heartbeat_ops_t *ops;
	heartbeat_ctx_t *ctx;
};

#ifdef HEARTBEAT
extern const heartbeat_ops_t *heartbeat_samples;
#else
const heartbeat_ops_t *heartbeat_samples = NULL;
#endif
#endif
