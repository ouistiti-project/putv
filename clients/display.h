#ifndef __DISPLAY_H__
#define __DISPLAY_H__

extern unsigned int c_album;
extern unsigned int c_artist;
extern unsigned int c_title;
extern unsigned int c_genre;
extern unsigned int c_state;

#define ELEM_BOTTOM 	0x01
#define ELEM_TOP		0x02
#define ELEM_LEFT		0x04
#define ELEM_RIGHT		0x08
#define ELEM_CENTER		0x10

typedef struct display_elem_s display_elem_t;

typedef int (*display_func_cb_t)(void *arg, display_elem_t *elem, const char *text);

typedef struct display_func_s display_func_t;
struct display_func_s
{
	display_func_cb_t cb;
	void *arg;
	display_func_t *next;
};
struct display_elem_s
{
	int id;
	void (*appendfunc)(display_elem_t *elem, display_func_cb_t cb, void *arg);
	void (*setfgcolor)(display_elem_t *elem, void *arg);
	void (*setpadding)(display_elem_t *elem, void *arg);
	void (*setfont)(display_elem_t *elem, void *arg);
	void (*settextalign)(display_elem_t *elem, int align);
	void (*destroy)(display_elem_t *elem);
	void *arg;
	display_func_t *funcs;
	display_elem_t *next;
};

typedef struct window_ops_s
{
	int (*width)(void *arg);
	int (*height)(void *arg);
	void *(*getlfont)(void *arg);
	void *(*getfont)(void *arg);
	void *(*getsfont)(void *arg);
	void *(*getpadding)(void *arg);
	void *(*getfgcolor)(void *arg);
	void *(*getbgcolor)(void *arg);
	display_func_cb_t printtext;
	display_func_cb_t printborder;
} window_ops_t;

#define T_DIV 0x01
#define T_IMG 0x02

typedef struct generator_ops_s
{
	display_elem_t *(*new_elem)(void *arg, int type, int id, int x, int y, int w, int h);
	window_ops_t window;
} generator_ops_t;

typedef struct display_ops_s
{
	void *(*create)(int argc, char** argv);
	void (*setdom)(void *arg, display_elem_t *elems);
	void (*clear)(void *arg);
	int (*print)(void *arg, int type, const char *string);
	void (*flush)(void *arg);
	const generator_ops_t *generator;
	void (*destroy)(void *arg);
} display_ops_t;

typedef struct display_s
{
	void *ctx;
	display_ops_t *ops;
} display_t;

extern display_ops_t *display_console;
extern display_ops_t *display_directfb;
#endif
