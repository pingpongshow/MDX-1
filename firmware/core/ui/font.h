#ifndef MDX_FONT_H
#define MDX_FONT_H
typedef struct { int w, h, bpr; int first, count; const unsigned char *data; } font_t;
extern const font_t FONT_SMALL, FONT_MED, FONT_BOLD, FONT_DIGITS;
/* FONT_DIGITS glyph order: "0123456789-. " */
static inline int digits_index(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c == '-') return 10; if (c == '.') return 11; return 12; }
#endif
