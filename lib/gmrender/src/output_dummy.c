/* output_dummy.c - Output module for dummy
 *
 * Copyright (C) 2014-2019   Mar Chalain
 *
 * This file is part of GMediaRender.
 *
 * GMediaRender is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GMediaRender is distributed in the hope that it will be useful,
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>

#include <upnp/ithread.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "output_module.h"
#include "output_dummy.h"

#include "logging.h"

#ifdef HAVE_GLIB
static int output_dummy_add_goptions(GOptionContext *ctx)
{
	return 0;
}
#endif

static int
output_dummy_init(void)
{
	return 0;
}

static void
output_dummy_set_uri(const char *uri,
				     output_update_meta_cb_t meta_cb)
{
}

static void
output_dummy_set_next_uri(const char *uri)
{
}

static int
output_dummy_loop()
{
	return 0;
}

static int
output_dummy_play(output_transition_cb_t callback)
{
	return 0;
}

static int
output_dummy_stop(void)
{
	return 0;
}

static int
output_dummy_pause(void)
{
	return 0;
}

static int
output_dummy_seek(int64_t position_nanos)
{
	return 0;
}

static int
output_dummy_get_position(int64_t *track_duration,
					 int64_t *track_pos)
{
	return 0;
}

static int
output_dummy_getvolume(float *value)
{
	return 0;
}
static int
output_dummy_setvolume(float value)
{
	return 0;
}
static int
output_dummy_getmute(int *value)
{
	return 0;
}
static int
output_dummy_setmute(int value)
{
	return 0;
}


const struct output_module dummy_output = {
    .shortname = "dummy",
	.description = "empty framework",
#ifdef HAVE_GLIB
	.add_goptions = output_dummy_add_goptions,
#endif
	.init        = output_dummy_init,
	.loop        = output_dummy_loop,
	.set_uri     = output_dummy_set_uri,
	.set_next_uri= output_dummy_set_next_uri,
	.play        = output_dummy_play,
	.stop        = output_dummy_stop,
	.pause       = output_dummy_pause,
	.seek        = output_dummy_seek,
	.get_position = output_dummy_get_position,
	.get_volume  = output_dummy_getvolume,
	.set_volume  = output_dummy_setvolume,
	.get_mute  = output_dummy_getmute,
	.set_mute  = output_dummy_setmute,
};
