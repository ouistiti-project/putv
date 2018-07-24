#ifndef __MEDIA_H__
#define __MEDIA_H__

typedef struct media_ctx_s media_ctx_t;

typedef int (*play_fcn_t)(void *arg, const char *url, const char *info, const char *mime);

media_ctx_t *media_init();
void media_destroy(media_ctx_t *ctx);

int media_count(media_ctx_t *ctx);
int media_insert(media_ctx_t *ctx, const char *path, const char *info, const char *mime);
int media_find(media_ctx_t *ctx, int id, char *url, int *urllen, char *info, int *infolen);
int media_current(media_ctx_t *ctx, char *url, int *urllen, char *info, int *infolen);
int media_play(media_ctx_t *ctx, play_fcn_t play, void *data);
int media_next(media_ctx_t *ctx);

#endif
