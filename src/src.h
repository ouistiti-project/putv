#ifndef __SRC_H__
#define __SRC_H__

typedef struct mediaplayer_ctx_s mediaplayer_ctx_t;
typedef struct decoder_s decoder_t;
typedef struct jitter_s jitter_t;

#ifndef SRC_CTX
typedef void src_ctx_t;
#endif
typedef struct src_s src_t;
struct src_s
{
	src_ctx_t *(*init)(mediaplayer_ctx_t *, const char *path);
	int (*run)(src_ctx_t *, jitter_t *jitter);
	void (*destroy)(src_ctx_t *);
};

const src_t *src_get(src_ctx_t *ctx);

extern const src_t *src_file;
#endif
