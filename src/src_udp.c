/*****************************************************************************
 * src_udp.c
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
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <pwd.h>

#include "player.h"
#include "jitter.h"
#include "demux.h"
typedef struct src_ops_s src_ops_t;
typedef struct src_ctx_s src_ctx_t;
struct src_ctx_s
{
	player_ctx_t *player;
	int sock;
	state_t state;
	pthread_t thread;
	jitter_t *out;
	unsigned int samplerate;
	struct sockaddr_in saddr;
	struct sockaddr_in6 saddr6;
	struct sockaddr *addr;
	int addrlen;
	const char *mime;
	demux_t demux;
};
#define SRC_CTX
#include "src.h"
#include "media.h"
#include "decoder.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#define src_dbg(...)

static const char *jitter_name = "udp socket";
static src_ctx_t *src_init(player_ctx_t *player, const char *url)
{
	int count = 2;

	char *protocol = NULL;
	char *host = NULL;
	char *port = NULL;
	char *path = NULL;
	char *search = NULL;

	char *value = utils_parseurl(url, &protocol, &host, &port, &path, &search);

	if (protocol == NULL)
		return NULL;
	int rtp = !strcmp(protocol, "rtp");
	if (!rtp && strcmp(protocol, "udp"))
		return NULL;

	int iport = 4400;
	if (port != NULL)
		iport = atoi(port);

	char *mime = NULL;
	if (search != NULL)
	{
		mime = strstr(search, "mime=");
		if (mime != NULL)
		{
			mime += 5;
		}
	}

	int ret;
	int af = AF_INET;

	in_addr_t inaddr;
	struct in6_addr in6addr;
	ret = inet_pton(af, host, &inaddr);
	if (ret != 1)
	{
		af = AF_INET6;
		ret = inet_pton(AF_INET6, host, &in6addr);
	}

	int sock = socket(af, SOCK_DGRAM, IPPROTO_IP);
	if (sock == 0)
	{
		return NULL;
	}

	struct sockaddr *addr = NULL;
	int addrlen = 0;
	struct sockaddr_in saddr;
	struct sockaddr_in6 saddr6;
	if (af == AF_INET)
	{
		memset(&saddr, 0, sizeof(struct sockaddr_in));
		saddr.sin_family = PF_INET;
		saddr.sin_addr.s_addr = INADDR_ANY; // bind socket to any interface
		saddr.sin_port = htons(iport); // Use the first free port
		addr = (struct sockaddr*)&saddr;
		addrlen = sizeof(saddr);
	}
	else
	{
		memset(&saddr6, 0, sizeof(struct sockaddr_in6));
		saddr6.sin6_family = PF_INET6;
		memcpy(&saddr6.sin6_addr, &in6addr_any, sizeof(saddr6.sin6_addr)); // bind socket to any interface
		saddr6.sin6_port = htons(iport); // Use the first free port
		addr = (struct sockaddr*)&saddr6;
		addrlen = sizeof(saddr6);
	}

	ret = bind(sock, addr, addrlen);
	if (ret == 0)
	{
		int value=1;
		ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
	}

	if ((ret == 0) && (af == AF_INET) && IN_MULTICAST(inaddr))
	{
		struct ip_mreq imreq;
		memset(&imreq, 0, sizeof(struct ip_mreq));
		imreq.imr_multiaddr.s_addr = inaddr;
		imreq.imr_interface.s_addr = htonl(INADDR_ANY); // use DEFAULT interface

		// JOIN multicast group on default interface
		ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			(const void *)&imreq, sizeof(imreq));
	}
	else if ((ret == 0) && (af == AF_INET6) && IN6_IS_ADDR_MULTICAST(&in6addr))
	{
		struct ipv6_mreq imreq;
		memset(&imreq, 0, sizeof(struct ip_mreq));
		memcpy(&imreq.ipv6mr_multiaddr.s6_addr, &in6addr, sizeof(struct in6_addr));
		imreq.ipv6mr_interface = 0; // use DEFAULT interface

		// JOIN multicast group on default interface
		ret = setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
			(const void *)&imreq, sizeof(imreq));
	}

	src_ctx_t *ctx = NULL;
	if (ret == 0)
	{
		ctx = calloc(1, sizeof(*ctx));

		if (af == AF_INET)
		{
			memset(&ctx->saddr, 0, sizeof(ctx->saddr));
			ctx->saddr.sin_family = PF_INET;
			ctx->saddr.sin_addr.s_addr = inaddr;
			ctx->saddr.sin_port = htons(iport);
			ctx->addr = (struct sockaddr*)&ctx->saddr;
			ctx->addrlen = sizeof(ctx->saddr);
		}
		else
		{
			memset(&ctx->saddr6, 0, sizeof(ctx->saddr6));
			ctx->saddr6.sin6_family = PF_INET6;
			memcpy(&ctx->saddr6.sin6_addr, &in6addr, sizeof(struct in6_addr));
			ctx->saddr6.sin6_port = htons(iport);
			ctx->addr = (struct sockaddr*)&ctx->saddr6;
			ctx->addrlen = sizeof(ctx->saddr6);
		}

		ctx->player = player;
		ctx->sock = sock;
		if (rtp)
		{
			ctx->demux.ops = demux_rtp;
		}
		else
			ctx->demux.ops = demux_passthrough;
		ctx->demux.ctx = ctx->demux.ops->init(player, search);
		ctx->mime = utils_mime2mime(mime);
	}
	if (ctx == NULL)
	{
		close(sock);
	}

	free(value);
	return ctx;
}

static void *src_thread(void *arg)
{
	src_ctx_t *ctx = (src_ctx_t *)arg;
	ctx->demux.ops->run(ctx->demux.ctx);

	int ret;
	while (ctx->state != STATE_ERROR)
	{
		if (player_waiton(ctx->player, STATE_PAUSE) < 0)
		{
			if (player_state(ctx->player, STATE_UNKNOWN) == STATE_ERROR)
			{
				ctx->state = STATE_ERROR;
				continue;
			}
		}

		unsigned char *buff = ctx->out->ops->pull(ctx->out->ctx);
		ret = recvfrom(ctx->sock, buff, ctx->out->ctx->size,
				MSG_NOSIGNAL, ctx->addr, &ctx->addrlen);
		if (ret == -EPIPE)
		{
		}
		if (ret < 0)
		{
			ctx->state = STATE_ERROR;
			err("sink: error write pcm %d", ret);
		}
		else if (ret == 0)
		{
			ctx->out->ops->flush(ctx->out->ctx);
		}
		else
		{
			src_dbg("sink: play %d", ret);
			ctx->out->ops->push(ctx->out->ctx, ret, NULL);
		}
	}
	dbg("sink: thread end");
	return NULL;
}

static int src_run(src_ctx_t *ctx)
{
	ctx->out = ctx->demux.ops->jitter(ctx->demux.ctx);
	pthread_create(&ctx->thread, NULL, src_thread, ctx);
	return 0;
}

static const char *src_mime(src_ctx_t *ctx, int index)
{
	return ctx->demux.ops->mime(ctx->demux.ctx, index);
}

static int src_attach(src_ctx_t *ctx, int index, decoder_t *decoder)
{
	return ctx->demux.ops->attach(ctx->demux.ctx, index, decoder);
}

static decoder_t *src_estream(src_ctx_t *ctx, int index)
{
	return ctx->demux.ops->estream(ctx->demux.ctx, index);
}

static void src_destroy(src_ctx_t *ctx)
{
	if (ctx->thread)
		pthread_join(ctx->thread, NULL);
	ctx->demux.ops->destroy(ctx->demux.ctx);
	close(ctx->sock);
	free(ctx);
}

const src_ops_t *src_udp = &(src_ops_t)
{
	.protocol = "udp://|rtp://",
	.init = src_init,
	.run = src_run,
	.mime = src_mime,
	.attach = src_attach,
	.estream = src_estream,
	.destroy = src_destroy,
};
