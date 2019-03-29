#ifndef __EVENT_H__
#define __EVENT_H__

#include <stdint.h>

typedef enum event_e
{
	SRC_EVENT_NEW_ES,
} event_t;
typedef struct event_new_es_s event_new_es_t;
struct event_new_es_s
{
	uint32_t pid;
	const char * mime;
};
typedef void (*event_listener_t)(void *arg, event_t event, void *data);
#endif
