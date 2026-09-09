/* Minimal complex arithmetic (float) used throughout the DSP. */
#ifndef MDX_CPX_H
#define MDX_CPX_H
#include <math.h>
typedef struct { float re, im; } cpx;
static inline cpx cpx_make(float re, float im) { cpx c = {re, im}; return c; }
static inline cpx cpx_add(cpx a, cpx b) { return cpx_make(a.re + b.re, a.im + b.im); }
static inline cpx cpx_sub(cpx a, cpx b) { return cpx_make(a.re - b.re, a.im - b.im); }
static inline cpx cpx_mul(cpx a, cpx b) { return cpx_make(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re); }
static inline cpx cpx_scale(cpx a, float s) { return cpx_make(a.re * s, a.im * s); }
static inline cpx cpx_conj(cpx a) { return cpx_make(a.re, -a.im); }
static inline float cpx_abs2(cpx a) { return a.re * a.re + a.im * a.im; }
static inline float cpx_abs(cpx a) { return sqrtf(cpx_abs2(a)); }
static inline float cpx_arg(cpx a) { return atan2f(a.im, a.re); }
static inline cpx cpx_div(cpx a, cpx b) { float d = cpx_abs2(b); if (d < 1e-30f) d = 1e-30f; cpx n = cpx_mul(a, cpx_conj(b)); return cpx_make(n.re / d, n.im / d); }
static inline cpx cpx_expj(float th) { return cpx_make(cosf(th), sinf(th)); }
/* Re <a, b> = Re sum a_k conj(b_k) over n elements */
static inline float cpx_dot_re(const cpx *a, const cpx *b, int n) { float s = 0; for (int i = 0; i < n; i++) s += a[i].re * b[i].re + a[i].im * b[i].im; return s; }
static inline float cpx_norm2(const cpx *a, int n) { float s = 0; for (int i = 0; i < n; i++) s += cpx_abs2(a[i]); return s; }
#endif
