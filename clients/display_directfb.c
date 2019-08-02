/*****************************************************************************
 * display_console.c
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
#include <string.h>

#include <directfb.h>

#include "display.h"

extern unsigned int crc32b(unsigned char *message);

#define err(format, ...) fprintf(stderr, "\x1B[31m"format"\x1B[0m\n",  ##__VA_ARGS__)
#define warn(format, ...) fprintf(stderr, "\x1B[35m"format"\x1B[0m\n",  ##__VA_ARGS__)
#ifdef DEBUG
#define dbg(format, ...) fprintf(stderr, "\x1B[32m"format"\x1B[0m\n",  ##__VA_ARGS__)
#else
#define dbg(...)
#endif

#ifndef DATADIR
#define DATADIR "."
#endif
#ifndef FONTNAME
#define FONTNAME "decker.dgiff"
#endif

typedef struct elem_directfb_ctx_s
{
	int fonth;
	IDirectFBFont *font;
	DFBRectangle rect;
	DFBColor color;
	DFBPoint padding;
	DFBSurfaceTextFlags textflags;
} elem_directfb_ctx_t;

typedef struct display_console_ctx_s
{
	IDirectFB *dfb;
	IDirectFBDisplayLayer *layer;
	IDirectFBSurface *surface;
	IDirectFBFont *font;
	IDirectFBFont *lfont;
	IDirectFBFont *sfont;
	DFBDimension size;

	DFBPoint padding;
	DFBColor fgcolor;
	DFBColor bgcolor;
	display_elem_t *elems;
} display_console_ctx_t;

static int E_FONT_LARGE = 48;
static int E_FONT_MEDIUM = 35;
static int E_FONT_SMALL = 24;

static int elem_printtext(void *arg, display_elem_t *elem, const char *text)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	IDirectFBSurface *surface = ctx->surface;
	IDirectFBDisplayLayer *layer = ctx->layer;
	DFBDimension *size = &ctx->size;
	int length = strlen(text);

	elem_directfb_ctx_t *elemp = (elem_directfb_ctx_t *)elem->arg;

	surface->SetFont(surface, elemp->font);
	surface->SetColor(surface, elemp->color.r, elemp->color.g, elemp->color.b, elemp->color.a);

	int x = elemp->rect.x + elemp->padding.x;
	int y = elemp->rect.y + elemp->padding.y + elemp->rect.h / 4 + elemp->fonth;
	if (elemp->textflags & DSTF_LEFT)
	{
		x = elemp->rect.x + elemp->padding.x + elemp->rect.w;
	}
	else if (elemp->textflags & DSTF_CENTER)
	{
		x = elemp->rect.x + elemp->padding.x + elemp->rect.w / 2;
	}
	if (elemp->textflags & DSTF_TOP)
		y = elemp->rect.y + elemp->padding.y;
	else if (elemp->textflags & DSTF_BOTTOM)
		y = elemp->rect.y + elemp->padding.y + elemp->rect.h;
	surface->DrawString(surface, text, -1, x, y, elemp->textflags);

	return 0;
}

static int elem_printborder(void *arg, display_elem_t *elem, const char *text)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	IDirectFBSurface *surface = ctx->surface;
	IDirectFBDisplayLayer *layer = ctx->layer;
	DFBDimension *size = &ctx->size;
	elem_directfb_ctx_t *elemp = (elem_directfb_ctx_t *)elem->arg;

	surface->SetColor(surface, elemp->color.r, elemp->color.g, elemp->color.b, elemp->color.a);
	surface->DrawRectangle(surface, elemp->rect.x, elemp->rect.y, elemp->rect.w, elemp->rect.h);

	return 0;
}

static void elem_appendfunc(display_elem_t *elem, display_func_cb_t cb, void *arg)
{
	display_func_t *func;

	func = calloc(1 , sizeof(*func));
	func->cb = cb;
	func->arg = arg;
	func->next = elem->funcs;
	elem->funcs = func;
}

static void elem_setfgcolor(display_elem_t *elem, void *arg)
{
	elem_directfb_ctx_t *elemp = (elem_directfb_ctx_t *)elem->arg;
	DFBColor *color = (DFBColor *)arg;

	memcpy(&elemp->color, color, sizeof(elemp->color));
}

static void elem_setpadding(display_elem_t *elem, void *arg)
{
	elem_directfb_ctx_t *elemp = (elem_directfb_ctx_t *)elem->arg;
	DFBPoint *padding = (DFBPoint *)arg;

	memcpy(&elemp->padding, padding, sizeof(elemp->padding));
}

static void elem_setfont(display_elem_t *elem, void *arg)
{
	elem_directfb_ctx_t *elemp = (elem_directfb_ctx_t *)elem->arg;
	IDirectFBFont *font = (IDirectFBFont *)arg;

	elemp->font = font;
	font->GetHeight(font, &elemp->fonth);
}

static void elem_settextalign(display_elem_t *elem, int align)
{
	elem_directfb_ctx_t *elemp = (elem_directfb_ctx_t *)elem->arg;

	elemp->textflags = 0;

	if (align & ELEM_TOP)
		elemp->textflags |= DSTF_TOP;
	if (align & ELEM_BOTTOM)
		elemp->textflags |= DSTF_BOTTOM;

	if (align & ELEM_CENTER)
		elemp->textflags |= DSTF_CENTER;
	if (align & ELEM_BOTTOM)
		elemp->textflags |= DSTF_RIGHT;
}

static void elem_destroy(display_elem_t *elem)
{
	free(elem);
}

static display_elem_t *elem_generate(void *arg, int type, int id, int x, int y, int w, int h)
{
	elem_directfb_ctx_t *elemp = calloc(1, sizeof(*elemp));
	elemp->rect.x = x;
	elemp->rect.y = y;
	elemp->rect.w = w;
	elemp->rect.h = h;

	display_elem_t *elem = calloc(1, sizeof(*elem));
	elem->id = id;
	elem->arg = elemp;
	elem->appendfunc = elem_appendfunc;
	elem->setfont = elem_setfont;
	elem->setfgcolor = elem_setfgcolor;
	elem->setpadding = elem_setpadding;
	elem->settextalign = elem_settextalign;
	elem->destroy = elem_destroy;
	switch (type)
	{
		case T_DIV:
			elem_appendfunc(elem, elem_printtext, arg);
		break;
		case T_IMG:
			elem_appendfunc(elem, elem_printtext, arg);
		break;
	}
	return elem;
}

static void * display_create(int argc, char** argv)
{
	DFBResult  ret;
	ret = DirectFBInit(&argc, &argv);
	if (ret)
		return NULL;

	IDirectFB *dfb;
	ret = DirectFBCreate(&dfb);
	if (ret)
		return NULL;

	IDirectFBDisplayLayer *layer;
	ret = dfb->GetDisplayLayer(dfb, DLID_PRIMARY, &layer);
	if (ret) {
		dfb->Release(dfb);
		return NULL;
	}

	layer->SetCooperativeLevel(layer, DFSCL_EXCLUSIVE);

	DFBDisplayLayerConfig  config;
	layer->GetConfiguration(layer, &config);
	config.options    = DLOP_OPACITY | DLOP_SRC_COLORKEY;
	config.buffermode = DLBM_FRONTONLY;
	layer->SetConfiguration( layer, &config );

	layer->SetSrcColorKey(layer, 0x00, 0xff, 0x00);

	IDirectFBSurface      *surface;
	ret = layer->GetSurface(layer, &surface);
	if (ret) {
		layer->Release(layer);
		dfb->Release(dfb);
		return NULL;
	}

	DFBPoint padding = {2,2};

	DFBDimension size;
	surface->GetSize(surface, &size.w, &size.h );
	size.w -= 2 * padding.x;
	size.h -= 2 * padding.y;

	IDirectFBFont *font;
	do
	{
		DFBFontDescription fdesc = { .flags = DFDESC_HEIGHT, .height = E_FONT_LARGE };
		ret = dfb->CreateFont(dfb, DATADIR"/"FONTNAME, &fdesc, &font );
		E_FONT_LARGE -= 1;
	} while (ret == DR_UNSUPPORTED);
	if (ret) {
		surface->Release(surface);
		layer->Release(layer);
		dfb->Release(dfb);
		return NULL;
	}
	display_console_ctx_t *ctx;
	ctx = calloc(1, sizeof(*ctx));
	ctx->dfb = dfb;
	ctx->surface = surface;
	ctx->layer = layer;
	ctx->lfont = font;
	E_FONT_SMALL = E_FONT_LARGE * 3 / 4;
	do
	{
		DFBFontDescription fdesc = { .flags = DFDESC_HEIGHT, .height = E_FONT_SMALL };
		ret = dfb->CreateFont(dfb, DATADIR"/"FONTNAME, &fdesc, &font );
		E_FONT_SMALL -= 1;
	} while (ret == DR_UNSUPPORTED);
	ctx->sfont = font;
	E_FONT_MEDIUM = E_FONT_LARGE * 4 / 5;
	do
	{
		DFBFontDescription fdesc = { .flags = DFDESC_HEIGHT, .height = E_FONT_MEDIUM };
		ret = dfb->CreateFont(dfb, DATADIR"/"FONTNAME, &fdesc, &font );
		E_FONT_MEDIUM -= 1;
	} while (ret == DR_UNSUPPORTED);
	ctx->font = font;

	memcpy(&ctx->size, &size, sizeof(size));

	memcpy(&ctx->padding, &padding, sizeof(ctx->padding));
	memcpy(&ctx->fgcolor, &(DFBColor){0x00, 0xff, 0xff, 0xff}, sizeof(ctx->fgcolor));
	memcpy(&ctx->bgcolor, &(DFBColor){0x00, 0x00, 0x00, 0x00}, sizeof(ctx->fgcolor));

	return ctx;
}

static void display_setdom(void *arg, display_elem_t *elems)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	ctx->elems = elems;
}

static void display_clear(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	IDirectFBSurface *surface = ctx->surface;
	IDirectFBDisplayLayer *layer = ctx->layer;

	surface->Clear(surface, 0, 0, 0, 0xff );
}

static int display_print(void *arg, int type, const char *string)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	IDirectFBSurface *surface = ctx->surface;
	IDirectFBDisplayLayer *layer = ctx->layer;
	DFBDimension *size = &ctx->size;
	int length = strlen(string);

	display_elem_t *elem = ctx->elems;
	while (elem != NULL)
	{
		if (elem->id == type)
		{
			display_func_t *func = elem->funcs;
			while (func != NULL)
			{
				func->cb(func->arg, elem, string);
				func = func->next;
			}
			break;
		}
		elem = elem->next;
	}

	return 0;
}

static void display_flush(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	IDirectFBSurface *surface = ctx->surface;
	IDirectFBDisplayLayer *layer = ctx->layer;
	DFBDimension *size = &ctx->size;

	surface->Flip(surface, NULL, DSFLIP_NONE);

	layer->SetSourceRectangle(layer, ctx->padding.x, ctx->padding.y, size->w - (ctx->padding.x * 2), size->h - (ctx->padding.y * 2) );
}

static void display_destroy(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	IDirectFBSurface *surface = ctx->surface;
	IDirectFBDisplayLayer *layer = ctx->layer;

	if (surface)
		surface->Release(surface);
	if (layer)
		layer->Release(layer);

	ctx->dfb->Release(ctx->dfb);
	free(arg);
}

static int display_width(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	return ctx->size.w;
}

static int display_height(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	return ctx->size.h;
}

static void *display_getlfont(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	return ctx->lfont;
}

static void *display_getfont(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	return ctx->font;
}

static void *display_getsfont(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	return ctx->sfont;
}

static void *display_getpadding(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	return &ctx->padding;
}

static void *display_getfgcolor(void *arg)
{
	display_console_ctx_t *ctx = (display_console_ctx_t *)arg;
	return &ctx->fgcolor;
}

display_ops_t *display_directfb = &(display_ops_t)
{
	.create = display_create,
	.setdom = display_setdom,
	.clear = display_clear,
	.print = display_print,
	.flush = display_flush,
	.generator = &(generator_ops_t)
	{
		.new_elem = elem_generate,
		. window = {
			.width = display_width,
			.height = display_height,
			.getlfont = display_getlfont,
			.getfont = display_getfont,
			.getsfont = display_getsfont,
			.getpadding = display_getpadding,
			.getfgcolor = display_getfgcolor,
			.printtext = elem_printtext,
			.printborder = elem_printborder,
		}
	},
	.destroy = display_destroy,
};
