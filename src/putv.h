#ifndef __PUTV_H__
#define __PUTV_H__

typedef enum
{
	STATE_UNKNOWN,
	STATE_STOP,
	STATE_PLAY,
	STATE_PAUSE,
	STATE_ERROR,
} state_t;

typedef enum
{
	EVENT_ONCHANGE,
} player_event_type_t;

typedef struct jitter_s jitter_t;
typedef struct src_s src_t;
typedef struct decoder_s decoder_t;
typedef struct encoder_s encoder_t;
typedef struct sink_s sink_t;

typedef struct mediaplayer_ctx_s mediaplayer_ctx_t;
typedef void (*player_event_cb_t)(void *ctx, mediaplayer_ctx_t *);

mediaplayer_ctx_t *player_init(const char *dbpath);
int player_run(mediaplayer_ctx_t *userdata);
void player_destroy(mediaplayer_ctx_t *ctx);
int player_waiton(mediaplayer_ctx_t *ctx, int state);
state_t player_state(mediaplayer_ctx_t *ctx, state_t state);
void player_next(mediaplayer_ctx_t *ctx);
void player_onchange(mediaplayer_ctx_t *ctx, player_event_cb_t callback, void *cbctx);

int media_insert(mediaplayer_ctx_t *ctx, const char *path, const char *info, const char *mime);
int media_find(mediaplayer_ctx_t *ctx, int id, char *url, int *urllen, char *info, int *infolen);
int media_current(mediaplayer_ctx_t *ctx, char *url, int *urllen, char *info, int *infolen);
int media_count(mediaplayer_ctx_t *ctx);

#endif
