/*****************************************************************************
 * unix_server.c
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
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "player.h"
#include "unix_server.h"

struct thread_server_s
{
	void *(*init)();
	void (*destroy)(void *);
	int (*run)(void *);
	void *ctx;
};

thread_info_t firstinfo;

typedef int (*client_routine_t)(struct thread_info_s *info);

typedef void *(*start_routine_t)(void*);
int start(client_routine_t service, thread_info_t *info)
{
	pthread_t thread;
	pthread_create(&thread, NULL, (start_routine_t)service, (void *)info);
	pthread_detach(thread);
}

void unixserver_remove(thread_info_t *info)
{
	thread_info_t *it = &firstinfo;
	while (it->next) {
		thread_info_t *old = it->next;
		if (old->sock == info->sock) {
			close(info->sock);
			it->next = old->next;
			free(old);
		}
		it = it->next;
		if (it == NULL)
			break;
	}
}

int unixserver_run(client_routine_t routine, void *userctx, const char *socketpath)
{
	int sock;
	int ret = -1;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock > 0)
	{
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(struct sockaddr_un));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, socketpath, sizeof(addr.sun_path));
		unlink(addr.sun_path);

		firstinfo.sock = sock;
		firstinfo.userctx = userctx;
		firstinfo.next = NULL;

		ret = bind(sock, (struct sockaddr *) &addr, sizeof(addr));
		if (ret == 0) {
			ret = listen(sock, 10);
			fprintf(stderr, "Unix server on : %s\n", socketpath);
		}
		if (ret == 0) {
			int newsock = 0;
			do {
				newsock = accept(sock, NULL, NULL);
				if (newsock > 0) {
					struct thread_info_s *info = calloc(1, sizeof(*info));
					info->sock = newsock;
					info->userctx = firstinfo.userctx;
					info->next = firstinfo.next;
					firstinfo.next = info;
					start(routine, info);
				}
			} while(newsock > 0);
		}
	}
	if (ret) {
		fprintf(stderr, "Unix server error : %s\n", strerror(errno));
	}
	return ret;
}
