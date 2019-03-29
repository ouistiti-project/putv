/*****************************************************************************
 * sink_udp.c
 * this file is part of https://github.com/ouistiti-project/putv
 *****************************************************************************
 * Copyright (C) 2016-2017
 *
 * Authors: Marc Chalain <marc.chalain@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>

#define __USE_GNU
#include <pthread.h>

#include <unistd.h>
#include <poll.h>

#include "player.h"
#include "mux.h"
#include "jitter.h"
#include "unix_server.h"
typedef struct sink_s sink_t;
typedef struct sink_ctx_s sink_ctx_t;
struct sink_ctx_s
{
	player_ctx_t *player;
	const char *filepath;
	struct sockaddr_in saddr;
	pthread_t thread;
	jitter_t *in;
	state_t state;
	unsigned int samplerate;
	int samplesize;
	int nchannels;
	int sock;
	int counter;
#ifdef MUX
	mux_t *mux;
#endif
};
#define SINK_CTX
#include "sink.h"
#include "media.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define sink_dbg(...)

#define SERVER_POLICY REALTIME_SCHED
#define SERVER_PRIORITY 45
#define SINK_POLICY REALTIME_SCHED
#define SINK_PRIORITY 65

static const char *jitter_name = "udp socket";
static sink_ctx_t *sink_init(player_ctx_t *player, const char *url)
{
	int ret = 0;

	char *protocol = NULL;
	char *host = NULL;
	char *port = NULL;
	char *path = NULL;
	char *search = NULL;
	in_addr_t if_addr = INADDR_ANY;

	char *value = utils_parseurl(url, &protocol, &host, &port, &path, &search);

	if (protocol == NULL)
	{
		free(value);
		return NULL;
	}
	int rtp = !strcmp(protocol, "rtp");
	if (!rtp && strcmp(protocol, "udp"))
	{
		free(value);
		return NULL;
	}

	int iport = 4400;
	if (port != NULL)
		iport = atoi(port);

	if (search != NULL)
	{
		char *nif;
		nif = strstr(search, "if=");
		if (nif != NULL)
		{
			nif += 3;
			struct ifaddrs *ifa_list;
			struct ifaddrs *ifa_main;

			if (getifaddrs(&ifa_list) == 0)
			{
				for (ifa_main = ifa_list; ifa_main != NULL; ifa_main = ifa_main->ifa_next)
				{
					if ((nif != NULL) && strcmp(nif, ifa_main->ifa_name) != 0)
						continue;
				}
				if (ifa_main != NULL)
				{
					if_addr = ((struct sockaddr_in *)ifa_main->ifa_addr)->sin_addr.s_addr;
				}
			}
		}
	}

	int count = 2;
	jitter_format_t format = SINK_BITSSTREAM;
	sink_ctx_t *ctx = NULL;
	int sock;
	int mtu;

	sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

	struct sockaddr_in saddr;
	memset(&saddr, 0, sizeof(struct in_addr));
	saddr.sin_family = PF_INET;
	saddr.sin_port = htons(0); // Use the first free port
	saddr.sin_addr.s_addr = htonl(if_addr); // bind socket to any interface

	ret = bind(sock, (struct sockaddr *)&saddr, sizeof(saddr));

	if (ret != -1)
	{
		unsigned long longaddress = inet_addr(host);

		struct ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		ifr.ifr_addr.sa_family = AF_INET;
		int i;
		for ( i = 0; i < 5; i++)
		{
			ifr.ifr_ifindex = i;
			ret = ioctl(sock, SIOCGIFNAME, &ifr);
			if (ret == 0)
			{
				ret = ioctl(sock, SIOCGIFFLAGS, &ifr);
				if (ret)
					continue;
				if ((ifr.ifr_flags & IFF_LOOPBACK))
				{
					continue;
				}
				if (IN_MULTICAST(longaddress) && !(ifr.ifr_flags & IFF_MULTICAST))
					continue;
				if (!(ifr.ifr_flags & IFF_RUNNING))
				{
					break;
				}
			}
		}
		ret = ioctl(sock, SIOCGIFMTU, &ifr);
		mtu = (ret == -1)?1500:ifr.ifr_mtu;

		int value=1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
		//check if the addrese is for broadcast diffusion
		if (longaddress == INADDR_BROADCAST)
		{
			ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value));
			if (! ifr.ifr_flags & IFF_BROADCAST)
				err("sync: udp broadcast interface not supported");
		}
		// check if the address is for multicast diffusion
		else if (IN_MULTICAST(longaddress))
		{
			if (! ifr.ifr_flags & IFF_MULTICAST)
				err("sync: udp multicast interface not supported");

			struct in_addr iaddr;
			// set content of struct saddr and imreq to zero
			memset(&iaddr, 0, sizeof(struct in_addr));
			iaddr.s_addr = if_addr;

			// Set the outgoing interface to DEFAULT
			ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iaddr,
							sizeof(struct in_addr));
			if (ret != 0)
				warn("sink: not allowed to change interface");

			unsigned char ttl = 3;
			// Set multicast packet TTL to 3; default TTL is 1
			ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
							sizeof(unsigned char));
			if (ret != 0)
				warn("sink: not allowed to set TTL");

			unsigned char one = 1;
			// send multicast traffic to myself too
			ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &one,
							sizeof(unsigned char));
			if (ret != 0)
				warn("sink: not allowed to make a loop on data sending");
		}

		saddr.sin_family = PF_INET;
		saddr.sin_addr.s_addr = longaddress;
		saddr.sin_port = htons(iport);

	}
	if (sock > 0 && ret == 0)
	{

		ctx = calloc(1, sizeof(*ctx));

		ctx->player = player;
		ctx->sock = sock;
		memcpy(&ctx->saddr, &saddr, sizeof(saddr));

		unsigned int size = mtu;
		jitter_t *jitter = jitter_scattergather_init(jitter_name, 6, size);
		jitter->ctx->frequence = DEFAULT_SAMPLERATE;
		jitter->ctx->thredhold = 3;
		jitter->format = format;
		ctx->in = jitter;
#ifdef MUX
		ctx->mux = mux_build(player, protocol);
#endif
	}
	free(value);

	return ctx;
}

static jitter_t *sink_jitter(sink_ctx_t *ctx)
{
#ifdef MUX
	return ctx->mux->ops->jitter(ctx->mux->ctx, 0);
#else
	return ctx->in;
#endif
}

static void *sink_thread(void *arg)
{
	sink_ctx_t *ctx = (sink_ctx_t *)arg;
	int run = 1;

	/* start decoding */
	dbg("sink: thread run");
	while (run)
	{
		unsigned char *buff = ctx->in->ops->peer(ctx->in->ctx);
		if (buff == NULL)
		{
			run = 0;
			break;
		}

		size_t length = ctx->in->ops->length(ctx->in->ctx);

		int ret;
		ret = sendto(ctx->sock, buff, length, MSG_NOSIGNAL| MSG_DONTWAIT,
				(struct sockaddr *)&ctx->saddr, sizeof(ctx->saddr));
		if (ret < 0)
		{
			close(ctx->sock);
			run = 0;
		}
		else
			ctx->counter++;

		sink_dbg("sink: boom %d", ctx->counter);
		ctx->in->ops->pop(ctx->in->ctx, length);
	}
	dbg("sink: thread end");
	return NULL;
}

static int sink_run(sink_ctx_t *ctx)
{
	pthread_create(&ctx->thread, NULL, sink_thread, ctx);
#ifdef MUX
	ctx->mux->ops->run(ctx->mux->ctx, ctx->in);
#endif

	return 0;
}

static void sink_destroy(sink_ctx_t *ctx)
{
	if (ctx->thread)
		pthread_join(ctx->thread, NULL);
	jitter_scattergather_destroy(ctx->in);
	free(ctx);
}

const sink_ops_t *sink_udp = &(sink_ops_t)
{
	.init = sink_init,
	.jitter = sink_jitter,
	.run = sink_run,
	.destroy = sink_destroy,
};

static sink_t _sink = {0};
sink_t *sink_build(player_ctx_t *player, const char *arg)
{
	const sink_ops_t *sinkops = NULL;
	sinkops = sink_udp;
	_sink.ctx = sinkops->init(player, arg);
	if (_sink.ctx == NULL)
		return NULL;
	_sink.ops = sinkops;
	return &_sink;
}
