#include "ground.h"
#include <math.h>
#include <string.h>

void ground_init(ground_t *g, const float f_hz[MDX_NLINES])
{
    memset(g, 0, sizeof(*g));
    for (int k = 0; k < MDX_NLINES; k++) {
        g->f_hz[k] = f_hz[k];
        /* viscous magnetic soil: log-uniform relaxation spectrum gives a response that is flat
         * in magnitude with a constant small phase (~3 deg) across the band */
        g->gmag[k]  = cpx_make(1.0f, 0.05f);
        /* salt: eddy response of a poor conductor, ~ j f / f_ref in the low-frequency limit */
        g->gsalt[k] = cpx_make(0.0f, f_hz[k] / 10240.0f);
    }
    g->rate_slow = 8.0f;   /* seconds */
    g->rate_fast = 1.0f;
    g->mode = 0;
}

void ground_fit(const ground_t *g, const cpx S[MDX_NLINES], float *a, float *b)
{
    float g11 = cpx_dot_re(g->gmag, g->gmag, MDX_NLINES);
    float g12 = cpx_dot_re(g->gmag, g->gsalt, MDX_NLINES);
    float g22 = cpx_dot_re(g->gsalt, g->gsalt, MDX_NLINES);
    float s1  = cpx_dot_re(S, g->gmag, MDX_NLINES);
    float s2  = cpx_dot_re(S, g->gsalt, MDX_NLINES);
    float det = g11 * g22 - g12 * g12;
    if (fabsf(det) < 1e-12f) { *a = s1 / g11; *b = 0; return; }
    *a = (s1 * g22 - s2 * g12) / det;
    *b = (s2 * g11 - s1 * g12) / det;
    if (*b < 0) { *b = 0; *a = s1 / g11; }     /* salt response cannot be negative */
}

static void update_reported(ground_t *g)
{
    cpx G0 = cpx_add(cpx_scale(g->gmag[0], g->a), cpx_scale(g->gsalt[0], g->b));
    g->strength = cpx_abs(G0);
    g->phase_deg = cpx_arg(G0) * 180.0f / (float)M_PI;
    float den = fabsf(g->a) + fabsf(g->b);
    g->salt_ratio = den > 1e-12f ? g->b / den : 0.0f;
}

void ground_update(ground_t *g, const cpx S[MDX_NLINES], float dt, float speed, int target_present)
{
    if (g->mode == 1) { g->a = g->a_grab; g->b = g->b_grab; update_reported(g); return; }
    if (g->mode == 2) { g->a = 0; g->b = 0; update_reported(g); return; }
    float a, b; ground_fit(g, S, &a, &b);
    if (target_present) return;                       /* a metal target is in the field: freeze */
    /* tracking speed follows coil speed: stationary -> slow (do not learn the target), sweeping -> fast */
    float v = speed < 0 ? 0 : (speed > 1.5f ? 1.5f : speed);
    float tau = g->rate_slow + (g->rate_fast - g->rate_slow) * (v / 1.5f);
    float alpha = dt / (tau + dt);
    if (alpha > 1) alpha = 1;
    g->a += alpha * (a - g->a);
    g->b += alpha * (b - g->b);
    update_reported(g);
}

void ground_grab(ground_t *g, const cpx S[MDX_NLINES])
{
    ground_fit(g, S, &g->a_grab, &g->b_grab);
    g->a = g->a_grab; g->b = g->b_grab;
    update_reported(g);
}

void ground_balance(const ground_t *g, const cpx S[MDX_NLINES], cpx T[MDX_NLINES])
{
    for (int k = 0; k < MDX_NLINES; k++)
        T[k] = cpx_sub(S[k], cpx_add(cpx_scale(g->gmag[k], g->a), cpx_scale(g->gsalt[k], g->b)));
}

float ground_misfit(const ground_t *g, const cpx S[MDX_NLINES])
{
    float a, b; ground_fit(g, S, &a, &b);
    float r = 0;
    for (int k = 0; k < MDX_NLINES; k++) {
        cpx m = cpx_add(cpx_scale(g->gmag[k], a), cpx_scale(g->gsalt[k], b));
        r += cpx_abs2(cpx_sub(S[k], m));
    }
    float n = cpx_norm2(S, MDX_NLINES);
    return n > 1e-20f ? sqrtf(r / n) : 0;
}

void ground_set_freqs(ground_t *g, const float f_hz[MDX_NLINES])
{
    for (int k = 0; k < MDX_NLINES; k++) { g->f_hz[k] = f_hz[k]; g->gsalt[k] = cpx_make(0.0f, f_hz[k] / 10240.0f); }
}
