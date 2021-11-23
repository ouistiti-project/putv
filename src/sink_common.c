#include <string.h>
#include <stdio.h>

#include "sink.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define sink_dbg(...)

extern const sink_ops_t *sink_alsa;
extern const sink_ops_t *sink_tinyalsa;
extern const sink_ops_t *sink_file;
extern const sink_ops_t *sink_udp;
extern const sink_ops_t *sink_rtp;
extern const sink_ops_t *sink_unix;

static sink_t _sink = {0};
sink_t *sink_build(player_ctx_t *player, const char *arg)
{
	const sink_ops_t *sinklist[] = {
#ifdef SINK_ALSA
		sink_alsa,
#endif
#ifdef SINK_FILE
		sink_file,
#endif
#ifdef SINK_TINYALSA
		sink_tinyalsa,
#endif
#ifdef SINK_UDP
		sink_udp,
		sink_rtp,
#endif
#ifdef SINK_UNIX
		sink_unix,
#endif
		NULL
	};

	const sink_ops_t *sinkops = NULL;
	if (!strcmp(arg, "none"))
		return NULL;
	int i = 0;
	while (sinklist[i] != NULL)
	{
		dbg("sink: test %s", sinklist[i]->name);
		int len = strlen(sinklist[i]->name);
		if (!strncmp(sinklist[i]->name, arg, len))
			break;
		i++;
	}
	if (sinklist[i] == NULL)
		i = 0;
	sinkops = sinklist[i];
	if (arg[0] == '\0')
		arg = sinklist[i]->default_;
	_sink.ctx = sinkops->init(player, arg);
	if (_sink.ctx == NULL)
		return NULL;
	_sink.ops = sinkops;
	return &_sink;
}
