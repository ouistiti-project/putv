ifeq ($(HEARTBEAT),)
	ifeq ($(SINK_UNIX),y)
		HEARTBEAT=y
	endif
	ifeq ($(SINK_UDP),y)
		HEARTBEAT=y
	endif
endif

PUTV?=putv

bin-y+=$(PUTV)
$(PUTV)_SOURCES+=main.c
$(PUTV)_SOURCES+=daemonize.c
$(PUTV)_SOURCES+=player.c
$(PUTV)_SOURCES+=jitter_sg.c
$(PUTV)_SOURCES+=jitter_ring.c
$(PUTV)_LIBS+=pthread
$(PUTV)_CFLAGS-$(SAMPLERATE_AUTO)+=-DDEFAULT_SAMPLERATE=44100
$(PUTV)_CFLAGS-$(SAMPLERATE_44100)+=-DDEFAULT_SAMPLERATE=44100
$(PUTV)_CFLAGS-$(SAMPLERATE_48000)+=-DDEFAULT_SAMPLERATE=48000
$(PUTV)_SOURCES+=media_common.c
$(PUTV)_SOURCES+=src_common.c
$(PUTV)_SOURCES+=decoder_common.c
$(PUTV)_SOURCES+=sink_common.c

$(PUTV)_SOURCES+=filter_pcm.c

$(PUTV)_SOURCES-$(MEDIA_SQLITE)+=media_sqlite.c
$(PUTV)_LIBRARY-$(MEDIA_SQLITE)+=sqlite3
$(PUTV)_LIBRARY-$(MEDIA_SQLITE_EXT)+=jansson
$(PUTV)_SOURCES-$(MEDIA_FILE)+=media_file.c
$(PUTV)_SOURCES-$(MEDIA_DIR)+=media_dir.c
$(PUTV)_SOURCES-$(HEARTBEAT)+=heartbeat_samples.c
$(PUTV)_SOURCES-$(HEARTBEAT)+=heartbeat_bitrate.c
$(PUTV)_CFLAGS-$(HEARTBEAT)+=-DHEARTBEAT_COEF_1000=1000
$(PUTV)_LIBS-$(HEARTBEAT)+=rt
$(PUTV)_SOURCES-$(SRC_FILE)+=src_file.c
$(PUTV)_SOURCES-$(SRC_ALSA)+=src_alsa.c
$(PUTV)_LIBS-$(SRC_ALSA)+=asound
$(PUTV)_SOURCES-$(SRC_CURL)+=src_curl.c
$(PUTV)_LIBRARY-$(SRC_CURL)+=libcurl
$(PUTV)_SOURCES-$(SRC_UNIX)+=src_unix.c
$(PUTV)_SOURCES-$(SRC_UDP)+=src_udp.c
$(PUTV)_LIBS-$(SRC_UDP)+=pthread
$(PUTV)_SOURCES-$(DEMUX_PASSTHROUGH)+=demux_passthrough.c
$(PUTV)_SOURCES-$(DEMUX_RTP)+=demux_rtp.c
$(PUTV)_SOURCES-$(DECODER_PASSTHROUGH)+=decoder_passthrough.c
ifneq ($(DECODER_MODULES),y)
$(PUTV)_SOURCES-$(DECODER_MAD)+=decoder_mad.c
$(PUTV)_LIBRARY-$(DECODER_MAD)+=mad
$(PUTV)_SOURCES-$(DECODER_FLAC)+=decoder_flac.c
$(PUTV)_LIBRARY-$(DECODER_FLAC)+=flac
endif
$(PUTV)_LIBS-$(DECODER_MODULES)+=dl
$(PUTV)_SOURCES-$(ENCODER_PASSTHROUGH)+=encoder_passthrough.c
$(PUTV)_CFLAGS-$(ENCODER_PASSTHROUGH)+=-DENCODER=encoder_passthrough
$(PUTV)_SOURCES-$(ENCODER_LAME)+=encoder_lame.c
$(PUTV)_CFLAGS-$(ENCODER_LAME)+=-DENCODER=encoder_lame
$(PUTV)_LIBS-$(ENCODER_LAME)+=mp3lame
$(PUTV)_SOURCES-$(ENCODER_FLAC)+=encoder_flac.c
$(PUTV)_CFLAGS-$(ENCODER_FLAC)+=-DENCODER=encoder_flac
$(PUTV)_LIBRARY-$(ENCODER_FLAC)+=flac
$(PUTV)_SOURCES-$(MUX)+=mux_common.c
$(PUTV)_SOURCES-$(MUX)+=mux_passthrough.c
$(PUTV)_SOURCES-$(MUX_RTP)+=mux_rtp.c
$(PUTV)_SOURCES-$(SINK_ALSA)+=sink_alsa.c
$(PUTV)_LIBS-$(SINK_ALSA)+=asound
$(PUTV)_SOURCES-$(SINK_TINYALSA)+=sink_tinyalsa.c
$(PUTV)_LIBRARY-$(SINK_TINYALSA)+=tinyalsa
$(PUTV)_SOURCES-$(SINK_FILE)+=sink_file.c
$(PUTV)_SOURCES-$(SINK_UDP)+=sink_udp.c
$(PUTV)_SOURCES-$(SINK_UNIX)+=sink_unix.c
$(PUTV)_SOURCES-$(SINK_UNIX)+=unix_server.c
$(PUTV)_SOURCES-$(SINK_PULSE)+=sink_pulse.c
$(PUTV)_LIBRARY-$(SINK_PULSE)+=libpulse-simple
$(PUTV)_SOURCES-$(CMDLINE)+=cmds_line.c
$(PUTV)_SOURCES-$(CMDINPUT)+=cmds_input.c
$(PUTV)_SOURCES-$(JSONRPC)+=cmds_json.c
$(PUTV)_SOURCES-$(JSONRPC)+=unix_server.c
$(PUTV)_LIBRARY-$(JSONRPC)+=jansson
$(PUTV)_LIBS-$(JSONRPC)+=jsonrpc
$(PUTV)_CFLAGS-$(JSONRPC)+=-I ../lib/jsonrpc
$(PUTV)_LDFLAGS-$(JSONRPC)+=-L ../lib/jsonrpc
$(PUTV)_SOURCES-$(TINYSVCMDNS)+=cmds_tinysvcmdns.c
$(PUTV)_LIBS-$(TINYSVCMDNS)+=tinysvcmdns
$(PUTV)_CFLAGS-$(TINYSVCMDNS)+=-I ../lib/tinysvcmdns
$(PUTV)_LDFLAGS-$(TINYSVCMDNS)+=-L ../lib/tinysvcmdns
$(PUTV)_LIBRARY-$(USE_ID3TAG)+=id3tag
$(PUTV)_LIBS-$(USE_OGGMETADDATA)+=FLAC
$(PUTV)_LIBS-$(USE_TIMER)+=rt

$(PUTV)_CFLAGS-$(DEBUG)+=-g -DDEBUG

ifeq ($(DECODER_MODULES),y)
modules-$(DECODER_MAD)+=decoder_mad
decoder_mad_SOURCES+=decoder_mad.c
decoder_mad_LIBS+=mad
modules-$(DECODER_FLAC)+=decoder_flac
decoder_flac_CFLAGS-$(SAMPLERATE_AUTO)+=-DDEFAULT_SAMPLERATE=44100
decoder_flac_CFLAGS-$(SAMPLERATE_44100)+=-DDEFAULT_SAMPLERATE=44100
decoder_flac_CFLAGS-$(SAMPLERATE_48000)+=-DDEFAULT_SAMPLERATE=48000
decoder_flac_SOURCES+=decoder_flac.c
decoder_flac_LIBRARY+=FLAC
endif
