#ifndef __ENCODER_H__
#define __ENCODER_H__

typedef struct player_ctx_s player_ctx_t;
typedef struct jitter_s jitter_t;
typedef struct sink_s sink_t;

#ifndef ENCODER_CTX
typedef void encoder_ctx_t;
#endif
typedef struct encoder_s encoder_t;
struct encoder_s
{
	encoder_ctx_t *(*init)(player_ctx_t *);
	jitter_t *(*jitter)(encoder_ctx_t *encoder);
	int (*run)(encoder_ctx_t *, jitter_t *);
	const char *(*mime)(encoder_ctx_t *);
	void (*destroy)(encoder_ctx_t *);
};

const encoder_t *encoder_get(encoder_ctx_t *ctx);

extern const encoder_t *encoder_passthrough;
extern const encoder_t *encoder_lame;
extern const encoder_t *encoder_flac;
#endif
