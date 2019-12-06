#include <string.h>

#include "sink.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define sink_dbg(...)

const sink_ops_t *sink_alsa;
const sink_ops_t *sink_tinyalsa;
const sink_ops_t *sink_file;
const sink_ops_t *sink_udp;
const sink_ops_t *sink_unix;

static sink_t _sink = {0};
sink_t *sink_build(player_ctx_t *player, const char *arg)
{
	const sink_ops_t *sinkops = NULL;
	if (!strcmp(arg, "none"))
		return NULL;
#ifdef SINK_ALSA
	sinkops = sink_alsa;
#endif
#ifdef SINK_FILE
	sinkops = sink_file;
#endif
#ifdef SINK_TINYALSA
	sinkops = sink_tinyalsa;
#endif
#ifdef SINK_UDP
	sinkops = sink_udp;
#endif
#ifdef SINK_UNIX
	sinkops = sink_unix;
#endif
	_sink.ctx = sinkops->init(player, arg);
	if (_sink.ctx == NULL)
		return NULL;
	_sink.ops = sinkops;
	return &_sink;
}
