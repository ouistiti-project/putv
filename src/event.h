#ifndef __EVENT_H__
#define __EVENT_H__

#include <stdint.h>
#include "jitter.h"

typedef struct src_s src_t;
typedef struct decoder_s decoder_t;
typedef struct player_ctx_s player_ctx_t;

typedef enum event_e
{
	SRC_EVENT_NEW_ES,
	SRC_EVENT_DECODE_ES,
	SRC_EVENT_END_ES,
	PLAYER_EVENT_CHANGE = 10,
} event_t;
typedef struct event_new_es_s event_new_es_t;
struct event_new_es_s
{
	uint32_t pid;
	const src_t *src;
	const char * mime;
	jitte_t jitte;
	decoder_t *decoder;
};

typedef struct event_decode_es_s event_end_es_t;
typedef struct event_decode_es_s event_decode_es_t;
struct event_decode_es_s
{
	uint32_t pid;
	const src_t *src;
	decoder_t *decoder;
};

typedef struct event_player_state_s event_player_state_t;
struct event_player_state_s
{
	const player_ctx_t *playerctx;
	int state;
};

typedef void (*event_listener_cb_t)(void *arg, event_t event, void *data);

typedef struct event_listener_s event_listener_t;
struct event_listener_s
{
	event_listener_cb_t cb;
	void *arg;
	event_listener_t *next;
	int id;
};
#endif
