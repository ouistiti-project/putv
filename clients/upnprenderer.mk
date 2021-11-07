modules-y+=gmrender_putv
gmrender_putv_SOURCES+=output_putv.c
gmrender_putv_SOURCES+=client_json.c
gmrender_putv_CFLAGS+=-I../lib/jsonrpc
gmrender_putv_LDFLAGS+=-L../lib/jsonrpc
gmrender_putv_LIBRARY+=jansson
gmrender_putv_LIBS+=jsonrpc
gmrender_putv_CFLAGS-$(DEBUG)+=-g -DDEBUG
