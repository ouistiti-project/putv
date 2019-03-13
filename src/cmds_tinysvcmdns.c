/*****************************************************************************
 * cmds_tinysvmdns.c
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <bits/local_lim.h>
#include <sys/utsname.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include <pthread.h>

#include "mdns.h"
#include "mdnsd.h"

#include "player.h"
typedef struct cmds_ctx_s cmds_ctx_t;
struct cmds_ctx_s
{
	player_ctx_t *player;
	sink_t *sink;
	const char *hostname;
	int run;
	struct mdnsd *svr;
	struct mdns_service *svc;
	pthread_t thread;
};
#define CMDS_CTX
#include "cmds.h"
#include "media.h"
#include "sink.h"

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

static cmds_ctx_t *cmds_tinysvcmdns_init(player_ctx_t *player, sink_t *sink, void *arg)
{
	struct mdnsd *svr = mdnsd_start();
	if (svr == NULL) {
		err("mdnsd_start() error\n");
		return NULL;
	}

	char hostname[HOST_NAME_MAX + 6];
	gethostname(hostname, HOST_NAME_MAX);
	if (strlen(hostname) == 0)
	{
		struct utsname sysinfo;
		uname(&sysinfo);
		snprintf(hostname, HOST_NAME_MAX + 6, "%s.local", sysinfo.nodename);
	}
	else
		strcat(hostname, ".local");

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_addr.sa_family = AF_INET;
	int i, first = 1;
	for ( i = 0; i < 5; i++)
	{
		int ret;
		int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
		ifr.ifr_ifindex = i;
		ret = ioctl(sock, SIOCGIFNAME, &ifr);
		if (ret == 0)
		{
			ret = ioctl(sock, SIOCGIFFLAGS, &ifr);
			if (!ret && (ifr.ifr_flags & IFF_LOOPBACK))
			{
				continue;
			}
			if (!ret && (ifr.ifr_flags & IFF_RUNNING))
			{
				ret = ioctl(sock, SIOCGIFADDR, &ifr);
				in_addr_t saddr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
				if (first)
				{
					mdnsd_set_hostname(svr, hostname, saddr);
					first = 0;
				}
				else
				{
					struct rr_entry *a2_e = NULL;
					a2_e = rr_create_a(create_nlabel(hostname), saddr);
					mdnsd_add_rr(svr, a2_e);
				}
			}
		}
	}

	char *path = NULL;
	if (arg != NULL)
	{
		path = malloc(strlen((const char*) arg) + 5 + +1);
		sprintf(path, "path=%s", (const char*) arg);
	}
	const char *txt[] = {
		path,
		NULL
	};
	struct mdns_service *svc = mdnsd_register_svc(svr, "Pump Up The Volume", 
									"_http._tcp.local", 80, NULL, txt);

	cmds_ctx_t *ctx = NULL;
	ctx = calloc(1, sizeof(*ctx));
	ctx->player = player;
	ctx->sink = sink;
	ctx->hostname = (const char *)arg;
	ctx->svr = svr;
	ctx->svc = svc;

	return ctx;
}

static void *_cmds_tinysvcmdns_pthread(void *arg)
{
	int ret;
	cmds_ctx_t *ctx = (cmds_ctx_t *)arg;
	ret = pause();
	return (void*)(intptr_t)ret;
}

static int cmds_tinysvcmdns_run(cmds_ctx_t *ctx)
{
	pthread_create(&ctx->thread, NULL, _cmds_tinysvcmdns_pthread, (void *)ctx);

	return 0;
}

static void cmds_tinysvcmdns_destroy(cmds_ctx_t *ctx)
{
	ctx->run = 0;
	pthread_join(ctx->thread, NULL);
	mdns_service_destroy(ctx->svc);
	mdnsd_stop(ctx->svr);
	free(ctx);
}

cmds_ops_t *cmds_tinysvcmdns = &(cmds_ops_t)
{
	.init = cmds_tinysvcmdns_init,
	.run = cmds_tinysvcmdns_run,
	.destroy = cmds_tinysvcmdns_destroy,
};
