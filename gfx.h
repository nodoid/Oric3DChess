/*
 * gfx.h - Oric hi-res (240x200) primitives + 3D chess scene renderer.
 *
 * Screen layout: $A000, 40 bytes per line, 6 pixels per byte.
 * Byte $40 = pixel-data byte, all pixels = paper (black); setting a pixel
 * ORs its bit.  Default line attributes: white ink, black paper, and we
 * never write attribute bytes, so everything is white-on-black.
 */
#ifndef GFX_H
#define GFX_H

#include "engine.h"

#define SW 240
#define SH 200

/* fill modes for spans/polygons */
#define FILL_W 0   /* solid white            */
#define FILL_B 1   /* solid black            */
#define FILL_D50 2 /* 50% checker (white on) */
#define FILL_D25 3 /* 25% white dots         */
#define FILL_NONE 4

void gfx_init(void);              /* clear screen to black */
void gfx_clear(void);
void gfx_px(int x, int y);        /* set white pixel */
void gfx_clrp(int x, int y);      /* clear (black) pixel */
void gfx_span(int x0, int x1, int y, unsigned char mode);
void gfx_line(int x0, int y0, int x1, int y1, unsigned char color);
void gfx_box(int x0, int y0, int x1, int y1, unsigned char mode);

/* text: 5x7 glyphs in 6x8 cells; color 1 = white, 0 = black */
void gfx_text(int x, int y, const char *s, unsigned char color);

/* full 3D redraw of board and pieces from the given position */
void scene_draw(const Position *p);

#endif
