#ifndef __DEMUX_H__
#define __DEMUX_H__

#include "event.h"
#include "jitter.h"

typedef struct player_ctx_s player_ctx_t;

#ifndef DEMUX_CTX
typedef void demux_ctx_t;
#endif
typedef struct demux_ops_s demux_ops_t;
struct demux_ops_s
{
	demux_ctx_t *(*init)(player_ctx_t *player, const char *arg);
	jitter_t *(*jitter)(demux_ctx_t *ctx, jitte_t jitte);
	int (*run)(demux_ctx_t *ctx);
	const char *(*mime)(demux_ctx_t *ctx, int index);
	void (*eventlistener)(demux_ctx_t *ctx, event_listener_cb_t listener, void *arg);
	int (*attach)(demux_ctx_t *ctx, long index, decoder_t *decoder);
	decoder_t *(*estream)(demux_ctx_t *ctx, long index);
	void (*destroy)(demux_ctx_t *ctx);
};

typedef struct demux_s demux_t;
struct demux_s
{
	const demux_ops_t *ops;
	demux_ctx_t *ctx;
};

demux_t *demux_build(player_ctx_t *player, const char *mime, const filter_t *);

extern const demux_ops_t *demux_rtp;
extern const demux_ops_t *demux_mpegts;
extern const demux_ops_t *demux_passthrough;
#endif
