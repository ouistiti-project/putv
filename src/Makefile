
bin-y+=putv
putv_SOURCES+=main.c
putv_SOURCES+=player.c
putv_SOURCES-$(MEDIA_SQLITE)+=media_sqlite.c
putv_CFLAGS-$(MEDIA_SQLITE)+=-DMEDIA=media_sqlite
putv_SOURCES-$(MEDIA_FILE)+=media_file.c
putv_CFLAGS-$(MEDIA_FILE)+=-DMEDIA=media_file
putv_SOURCES+=jitter_sg.c
putv_SOURCES+=jitter_ring.c
putv_LIBRARY+=pthread sqlite3
putv_SOURCES-$(SRC_FILE)+=src_file.c
putv_CFLAGS-$(SRC_FILE)+=-DSRC_FILE
putv_CFLAGS-$(SRC_FILE)+=-DSRC=src_file
putv_SOURCES-$(DECODER_MAD)+=decoder_mad.c
putv_CFLAGS-$(DECODER_MAD)+=-DDECODER_MAD
putv_CFLAGS-$(DECODER_MAD)+=-DDECODER=decoder_mad
putv_LIBRARY-$(DECODER_MAD)+=mad
putv_CFLAGS-$(USE_ID3TAG)+=-DUSE_ID3TAG
putv_LIBRARY-$(USE_ID3TAG)+=id3tag
putv_SOURCES-$(ENCODER_PASSTHROUGH)+=encoder_passthrough.c
putv_CFLAGS-$(ENCODER_PASSTHROUGH)+=-DENCODER_PASSTHROUGH
putv_CFLAGS-$(ENCODER_PASSTHROUGH)+=-DENCODER=encoder_passthrough
putv_SOURCES-$(ENCODER_LAME)+=encoder_lame.c
putv_CFLAGS-$(ENCODER_LAME)+=-DENCODER_LAME
putv_CFLAGS-$(ENCODER_LAME)+=-DENCODER=encoder_lame
putv_CFLAGES-$(ENCODER_LAME)+=-DENCODER_FRAME_SIZE=1024
#putv_CFLAGS-$(ENCODER_LAME)+=-DLAME_DUMP
putv_LIBRARY-$(ENCODER_LAME)+=mp3lame
putv_SOURCES-$(SINK_ALSA)+=sink_alsa.c
putv_CFLAGS-$(SINK_ALSA)+=-DSINK_ALSA
putv_CFLAGS-$(SINK_ALSA)+=-DSINK_ALSA_FORMAT=$(SINK_ALSA_FORMAT)
putv_CFLAGS-$(SINK_ALSA)+=-DSINK=sink_alsa
putv_LIBRARY-$(SINK_ALSA)+=asound
putv_SOURCES-$(SINK_TINYALSA)+=sink_tinyalsa.c
putv_CFLAGS-$(SINK_TINYALSA)+=-DSINK_TINYALSA
putv_CFLAGS-$(SINK_TINYALSA)+=-DSINK=sink_tinyalsa
putv_LIBRARY-$(SINK_TINYALSA)+=tinyalsa
putv_SOURCES-$(SINK_FILE)+=sink_file.c
putv_CFLAGS-$(SINK_FILE)+=-DSINK_FILE
putv_CFLAGS-$(SINK_FILE)+=-DSINK=sink_file
putv_SOURCES-$(CMDLINE)+=cmds_line.c
putv_CFLAGS-$(CMDLINE)+=-DCMDLINE
putv_SOURCES-$(JSONRPC)+=cmds_json.c
putv_SOURCES-$(JSONRPC)+=unix_server.c
putv_CFLAGS-$(JSONRPC)+=-DJSONRPC
putv_LIBRARY-$(JSONRPC)+=jsonrpc jansson
putv_CFLAGS+=-I ../lib/jsonrpc
putv_LDFLAGS+=-L ../lib/jsonrpc

putv_CFLAGS-$(DEBUG)+=-g -DDEBUG
