#include "demod.h"
#include <math.h>
#include <stdlib.h>

void demod_init(demod_t *d, int M)
{
    d->M = M;
    for (int b = 0; b < MDX_NLINES + MDX_NNOISE; b++) {
        int k = b < MDX_NLINES ? MDX_LINE_BIN[b] : MDX_NOISE_BIN[b - MDX_NLINES];
        for (int n = 0; n < M; n++) {
            float th = 2.0f * (float)M_PI * (float)k * (float)n / (float)M;
            d->cs[b][n] = cosf(th);
            d->sn[b][n] = sinf(th);
        }
    }
    for (int l = 0; l < MDX_NLINES; l++) d->f_hz[l] = mdx_line_hz(l, M);
}

/* X_k = (2/M) sum x[n] e^{-j 2 pi k n / M}  -> peak amplitude phasor of the k-th line */
static cpx bin(const float *cs, const float *sn, const int32_t *x, int M, float scale)
{
    float re = 0, im = 0;
    for (int n = 0; n < M; n++) { re += cs[n] * (float)x[n]; im -= sn[n] * (float)x[n]; }
    float s = 2.0f * scale / (float)M;
    return cpx_make(re * s, im * s);
}

void demod_period(const demod_t *d, const int32_t *rx, const int32_t *itx, demod_out_t *o)
{
    int M = d->M;
    float i2 = 0; int32_t pk = 0; int clip = 0;
    for (int n = 0; n < M; n++) {
        float a = (float)itx[n] * MDX_ITX_A_PER_CODE; i2 += a * a;
        int32_t r = rx[n] < 0 ? -rx[n] : rx[n]; if (r > pk) pk = r;
        if (r > 8000000) clip = 1;
        int32_t t = itx[n] < 0 ? -itx[n] : itx[n]; if (t > 8000000) clip = 1;
    }
    o->itx_rms_a = sqrtf(i2 / (float)M);
    o->rx_peak_v = (float)pk * MDX_RX_V_PER_CODE;
    o->clipped = clip;
    cpx i1 = cpx_make(1, 0);
    for (int l = 0; l < MDX_NLINES; l++) {
        o->rx_v[l]  = bin(d->cs[l], d->sn[l], rx, M, MDX_RX_V_PER_CODE);
        o->itx_a[l] = bin(d->cs[l], d->sn[l], itx, M, MDX_ITX_A_PER_CODE);
        o->S[l]     = cpx_div(o->rx_v[l], o->itx_a[l]);
        if (l == 0) i1 = o->itx_a[0];
    }
    float ref = cpx_abs(i1); if (ref < 1e-6f) ref = 1e-6f;
    for (int b = 0; b < MDX_NNOISE; b++)
        o->noise[b] = cpx_abs(bin(d->cs[MDX_NLINES + b], d->sn[MDX_NLINES + b], rx, M, MDX_RX_V_PER_CODE)) / ref;
}
