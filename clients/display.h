#ifndef __DISPLAY_H__
#define __DISPLAY_H__

enum display_type_e
{
	E_TITLE,
	E_ARTIST,
	E_ALBUM,
	E_PLAY,
	E_PAUSE,
	E_STOP,
};

typedef int (*print_t)(void *data, int type, char *string);

typedef void (*display_create_t)();
typedef int (*display_string_t)(void *arg, int type, char *string);
typedef void (*display_destroy_t)(void *arg);

typedef struct display_ops_s
{
	void *(*create)();
	int (*print)(void *arg, int type, char *string);
	void (*destroy)(void *arg);
} display_ops_t;

typedef struct display_s
{
	void *ctx;
	display_ops_t *ops;
} display_t;

extern display_ops_t *display_console;
#endif
