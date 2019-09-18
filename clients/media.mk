bin-y+=putv_media
putv_media_SOURCES+=media.c
putv_media_SOURCES+=client_json.c
putv_media_CFLAGS+=-I../lib/jsonrpc
putv_media_LDFLAGS+=-L../lib/jsonrpc
putv_media_LIBS+=jansson pthread
putv_media_LIBRARY+=jsonrpc
putv_media_CFLAGS-$(DEBUG)+=-g -DDEBUG
