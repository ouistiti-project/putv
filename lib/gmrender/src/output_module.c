/* output_module.c - Output module frontend
 *
 * Copyright (C) 2014-2019 Marc Chalain
 *
 * This file is part of GMediaRender.
 *
 * uplaymusic is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * uplaymusic is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GMediaRender; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>

#include "logging.h"
#include "output_module.h"
#ifdef HAVE_GST
#include "output_gstreamer.h"
#endif
#ifdef HAVE_MPG123
#include "output_mpg123.h"
#endif
#include "output_dummy.h"
#include "output.h"

static int nbmodules = 1;
static const struct output_module *modules[5] = {
	&dummy_output,
};

void output_module_load(const char *basedir)
{
	struct dirent **items;
	int nitems;
	int index = 0;

#ifdef HAVE_GST
	modules[nbmodules++] = &gstreamer_output;
#endif
#ifdef HAVE_MPG123
	modules[nbmodules++] = &mpg123_output;
#endif

	nitems = scandir(basedir, &items, NULL, alphasort);
	fprintf(stderr, "scan %s\n", basedir);
	if (chdir(basedir) != 0)
		return;
	while (nitems > 0 && index < nitems)
	{
		if (items[index]->d_name[0] == '.')
		{
			index++;
			continue;
		}
		switch (items[index]->d_type)
		{
		case DT_REG:
			if (strstr(items[index]->d_name,".so") != NULL)
			{
				void *handler = NULL;
				char *path = malloc(strlen(basedir) + 1 + strlen(items[index]->d_name) + 1);
				if (path)
				{
					sprintf(path, "%s/%s", basedir, items[index]->d_name);
					handler = dlopen(path, RTLD_LAZY);
					free(path);
				}
				if (!handler)
				{
					char *error;
					error = dlerror();
					fprintf(stderr, "Load error %s\n", error);

					break;
				}
				dlerror();
				struct output_module *(*get_module)();
				get_module = (struct output_module *(*)()) dlsym(handler, "get_module");
				char *error;
				error = dlerror();
				if (error == NULL)
				{
					modules[nbmodules++] = get_module();
				}
				else
				{
					fprintf(stderr, "%s not loaded\n", path);
				}
			}
		break;
		}
		index++;
	}
}

void output_module_dump_modules(void)
{
	if (nbmodules == 0) {
		puts("  NONE!");
	} else {
		int i;
		for (i=0; i<nbmodules; i++) {
			printf("Available output: %s\t%s%s\n",
			       modules[i]->shortname,
			       modules[i]->description,
			       (i==0) ? " (default)" : "");
		}
	}
}

const struct output_module *output_module_get(const char *shortname)
{
	const struct output_module *output_module = NULL;
	if (nbmodules == 0) {
		Log_error("output", "No output module available");
		return NULL;
	}
	if (shortname == NULL) {
		output_module = modules[0];
	} else {
		int i;
		for (i=0; i<nbmodules; i++) {
			Log_info("output_module", "test %s",modules[i]->shortname);
			if (strcmp(modules[i]->shortname, shortname)==0) {
				Log_info("output_module", "get %s",modules[i]->shortname);
				output_module = modules[i];
				break;
			}
		}
	}

	return output_module;
}

#ifdef HAVE_GLIB
int output_module_add_goptions(GOptionContext *ctx)
{
	int i;
	for (i = 0; i < nbmodules; ++i) {
		if (modules[i]->add_goptions) {
			int result = modules[i]->add_goptions(ctx);
			if (result != 0) {
				fprintf(stderr, "Failed with %s\n", modules[i]->shortname);
				return result;
			}
		}
	}
	return 0;
}
#endif
