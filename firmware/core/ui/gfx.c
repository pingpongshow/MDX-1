#include "gfx.h"
#include <string.h>
#include <stdlib.h>

void gfx_clear(fb_t *fb, int black) { memset(fb->px, black ? 0xFF : 0x00, sizeof(fb->px)); }
void gfx_pixel(fb_t *fb, int x, int y, int on)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return;
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    if (on) fb->px[y][x >> 3] |= m; else fb->px[y][x >> 3] &= (uint8_t)~m;
}
int gfx_get(const fb_t *fb, int x, int y) { if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return 0; return (fb->px[y][x >> 3] >> (7 - (x & 7))) & 1; }
void gfx_hline(fb_t *fb, int x0, int x1, int y, int on) { if (x0 > x1) { int t = x0; x0 = x1; x1 = t; } for (int x = x0; x <= x1; x++) gfx_pixel(fb, x, y, on); }
void gfx_vline(fb_t *fb, int x, int y0, int y1, int on) { if (y0 > y1) { int t = y0; y0 = y1; y1 = t; } for (int y = y0; y <= y1; y++) gfx_pixel(fb, x, y, on); }
void gfx_line(fb_t *fb, int x0, int y0, int x1, int y1, int on)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { gfx_pixel(fb, x0, y0, on); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
}
void gfx_rect(fb_t *fb, int x, int y, int w, int h, int on) { gfx_hline(fb, x, x + w - 1, y, on); gfx_hline(fb, x, x + w - 1, y + h - 1, on); gfx_vline(fb, x, y, y + h - 1, on); gfx_vline(fb, x + w - 1, y, y + h - 1, on); }
void gfx_fill(fb_t *fb, int x, int y, int w, int h, int on) { for (int j = 0; j < h; j++) gfx_hline(fb, x, x + w - 1, y + j, on); }
void gfx_circle(fb_t *fb, int cx, int cy, int r, int on)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_pixel(fb, cx + x, cy + y, on); gfx_pixel(fb, cx - x, cy + y, on); gfx_pixel(fb, cx + x, cy - y, on); gfx_pixel(fb, cx - x, cy - y, on);
        gfx_pixel(fb, cx + y, cy + x, on); gfx_pixel(fb, cx - y, cy + x, on); gfx_pixel(fb, cx + y, cy - x, on); gfx_pixel(fb, cx - y, cy - x, on);
        y++; if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}
void gfx_disc(fb_t *fb, int cx, int cy, int r, int on) { for (int j = -r; j <= r; j++) { int w = 0; while ((w + 1) * (w + 1) + j * j <= r * r) w++; gfx_hline(fb, cx - w, cx + w, cy + j, on); } }

static void glyph(fb_t *fb, int x, int y, const font_t *f, int idx, int on)
{
    const unsigned char *g = f->data + (size_t)idx * (size_t)(f->h * f->bpr);
    for (int r = 0; r < f->h; r++)
        for (int c = 0; c < f->w; c++)
            if (g[r * f->bpr + (c >> 3)] & (0x80 >> (c & 7))) gfx_pixel(fb, x + c, y + r, on);
}
int gfx_text(fb_t *fb, int x, int y, const font_t *f, const char *s, int on)
{
    for (; *s; s++) {
        int idx = (unsigned char)*s - f->first;
        if (idx >= 0 && idx < f->count) glyph(fb, x, y, f, idx, on);
        x += f->w;
    }
    return x;
}
int gfx_text_width(const font_t *f, const char *s) { return (int)strlen(s) * f->w; }
void gfx_text_center(fb_t *fb, int cx, int y, const font_t *f, const char *s, int on) { gfx_text(fb, cx - gfx_text_width(f, s) / 2, y, f, s, on); }
void gfx_text_right(fb_t *fb, int xr, int y, const font_t *f, const char *s, int on) { gfx_text(fb, xr - gfx_text_width(f, s), y, f, s, on); }
void gfx_digits(fb_t *fb, int x, int y, const char *s, int on) { for (; *s; s++) { glyph(fb, x, y, &FONT_DIGITS, digits_index(*s), on); x += FONT_DIGITS.w; } }
void gfx_bar(fb_t *fb, int x, int y, int w, int h, float frac, int on)
{
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    gfx_rect(fb, x, y, w, h, on);
    int fw = (int)((float)(w - 2) * frac + 0.5f);
    if (fw > 0) gfx_fill(fb, x + 1, y + 1, fw, h - 2, on);
}
void gfx_dither(fb_t *fb, int x, int y, int w, int h) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (((i + j) & 1) == 0) gfx_pixel(fb, x + i, y + j, 1); }
void gfx_invert(fb_t *fb, int x, int y, int w, int h) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) gfx_pixel(fb, x + i, y + j, !gfx_get(fb, x + i, y + j)); }
