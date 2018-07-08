
bin-y+=putv
putv_SOURCES+=putv.c
putv_SOURCES+=main.c
putv_SOURCES+=jitter.c
putv_LIBRARY+=pthread sqlite3
putv_SOURCES-$(SRC_FILE)+=src_file.c
putv_CFLAGS-$(SRC_FILE)+=-DSRC_FILE
putv_CFLAGS-$(SRC_FILE)+=-DSRC=src_file
putv_SOURCES-$(DECODER_MAD)+=decoder_mad.c
putv_CFLAGS-$(DECODER_MAD)+=-DDECODER_MAD
putv_CFLAGS-$(DECODER_MAD)+=-DDECODER=decoder_mad
putv_LIBRARY-$(DECODER_MAD)+=mad
putv_SOURCES-$(ENCODER_PASSTHROUGH)+=encoder_passthrough.c
putv_CFLAGS-$(ENCODER_PASSTHROUGH)+=-DENCODER_PASSTHROUGH
putv_CFLAGS-$(ENCODER_PASSTHROUGH)+=-DENCODER=encoder_passthrough
putv_SOURCES-$(SINK_ALSA)+=sink_alsa.c
putv_CFLAGS-$(SINK_ALSA)+=-DSINK_ALSA
putv_CFLAGS-$(SINK_ALSA)+=-DSINK_ALSA_FORMAT=$(SINK_ALSA_FORMAT)
putv_CFLAGS-$(SINK_ALSA)+=-DSINK=sink_alsa
putv_LIBRARY-$(SINK_ALSA)+=asound
putv_SOURCES-$(SINK_TINYALSA)+=sink_tinyalsa.c
putv_CFLAGS-$(SINK_TINYALSA)+=-DSINK_TINYALSA
putv_CFLAGS-$(SINK_TINYALSA)+=-DSINK=sink_tinyalsa
putv_LIBRARY-$(SINK_TINYALSA)+=tinyalsa
putv_SOURCES-$(CMDLINE)+=cmds_line.c
putv_CFLAGS-$(CMDLINE)+=-DCMDLINE
putv_SOURCES-$(JSONRPC)+=cmds_json.c
putv_SOURCES-$(JSONRPC)+=unix_server.c
putv_CFLAGS-$(JSONRPC)+=-DJSONRPC
putv_LIBRARY-$(JSONRPC)+=jsonrpc jansson
putv_CFLAGS+=-I ../lib/jsonrpc
putv_LDFLAGS+=-L ../lib/jsonrpc

putv_CFLAGS-$(DEBUG)+=-g -DDEBUG
