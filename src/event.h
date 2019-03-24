#ifndef __EVENT_H__
#define __EVENT_H__

typedef enum event_e
{
	SRC_EVENT_NEW_ES,
} event_t;
typedef struct event_new_es_s event_new_es_t;
struct event_new_es_s
{
	int pid;
	const char * mime;
};
typedef void (*event_listener_t)(void *arg, event_t event, void *data);
#endif
