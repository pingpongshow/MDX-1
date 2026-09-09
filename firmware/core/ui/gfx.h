/* 1-bpp framebuffer graphics for the 400x240 Sharp memory LCD. 1 = black. */
#ifndef MDX_GFX_H
#define MDX_GFX_H
#include <stdint.h>
#include "font.h"
#include "../hal/hal.h"

typedef struct { uint8_t px[LCD_H][LCD_STRIDE]; } fb_t;

void gfx_clear(fb_t *fb, int black);
void gfx_pixel(fb_t *fb, int x, int y, int on);
int  gfx_get(const fb_t *fb, int x, int y);
void gfx_hline(fb_t *fb, int x0, int x1, int y, int on);
void gfx_vline(fb_t *fb, int x, int y0, int y1, int on);
void gfx_line(fb_t *fb, int x0, int y0, int x1, int y1, int on);
void gfx_rect(fb_t *fb, int x, int y, int w, int h, int on);
void gfx_fill(fb_t *fb, int x, int y, int w, int h, int on);
void gfx_circle(fb_t *fb, int cx, int cy, int r, int on);
void gfx_disc(fb_t *fb, int cx, int cy, int r, int on);
int  gfx_text(fb_t *fb, int x, int y, const font_t *f, const char *s, int on);   /* returns end x */
int  gfx_text_width(const font_t *f, const char *s);
void gfx_text_center(fb_t *fb, int cx, int y, const font_t *f, const char *s, int on);
void gfx_text_right(fb_t *fb, int xr, int y, const font_t *f, const char *s, int on);
void gfx_digits(fb_t *fb, int x, int y, const char *s, int on);            /* big digits font */
void gfx_bar(fb_t *fb, int x, int y, int w, int h, float frac01, int on); /* horizontal bar */
void gfx_dither(fb_t *fb, int x, int y, int w, int h);                     /* 50 % grey fill */
void gfx_invert(fb_t *fb, int x, int y, int w, int h);
#endif
