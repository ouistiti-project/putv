#ifndef __CMDS_H__
#define __CMDS_H__

#ifndef CMDS_CTX
typedef void cmds_ctx_t;
#endif
typedef struct cmds_s cmds_t;
struct cmds_s
{
	cmds_ctx_t *(*init)(mediaplayer_ctx_t *, media_ctx_t *, void *arg);
	int (*run)(cmds_ctx_t *);
	void (*destroy)(cmds_ctx_t *);
};

extern cmds_t *cmds_line;
extern cmds_t *cmds_json;
#endif
