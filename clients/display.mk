
bin-y+=display
display_SOURCES+=display.c
display_SOURCES+=crc32.c
display_SOURCES+=display_console.c
display_SOURCES+=client_json.c
display_CFLAGS+=-I../lib/jsonrpc
display_CFLAGS+=-DUSE_INOTIFY
display_LDFLAGS+=-L../lib/jsonrpc
display_LIBS+=jansson
display_LIBRARY+=jsonrpc

display_SOURCES-$(DISPLAY_DIRECTFB)+=display_directfb.c
display_CFLAGS-$(DISPLAY_DIRECTFB)+=-I=/usr/include/directfb
display_CFLAGS-$(DISPLAY_DIRECTFB)+=-DDIRECTFB
display_LIBS-$(DISPLAY_DIRECTFB)+=directfb pthread

display_CFLAGS-$(JSONRPC_LARGEPACKET)+=-DJSONRPC_LARGEPACKET

display_CFLAGS-$(DEBUG)+=-g -DDEBUG
