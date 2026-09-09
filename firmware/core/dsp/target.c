#include "target.h"
#include <math.h>
#include <string.h>

static cpx eddy(float f, float zeta)
{
    float x = f / zeta;
    /* -(j x)/(1 + j x) = -(x^2 + j x)/(1 + x^2) */
    float d = 1.0f + x * x;
    return cpx_make(-x * x / d, -x / d);
}

void target_model_init(target_model_t *tm, const float f_hz[MDX_NLINES])
{
    memcpy(tm->f_hz, f_hz, sizeof(tm->f_hz));
    float l0 = logf(TARGET_ZETA_MIN_HZ), l1 = logf(TARGET_ZETA_MAX_HZ);
    for (int i = 0; i < TARGET_ZETA_STEPS; i++) {
        tm->zeta[i] = expf(l0 + (l1 - l0) * (float)i / (float)(TARGET_ZETA_STEPS - 1));
        for (int k = 0; k < MDX_NLINES; k++) tm->m[i][k] = eddy(f_hz[k], tm->zeta[i]);
    }
    tm->depth_ref_strength = 2.0e-3f;   /* ohm: default until the coil wizard measures a coin */
    tm->depth_ref_cm = 10.0f;
    tm->fe_threshold = 55.0f;
}

float target_cond_id_from_zeta(float zeta_hz)
{
    float l = (logf(zeta_hz) - logf(TARGET_ZETA_MIN_HZ)) / (logf(TARGET_ZETA_MAX_HZ) - logf(TARGET_ZETA_MIN_HZ));
    float id = 99.0f * (1.0f - l);
    return id < 0 ? 0 : (id > 99 ? 99 : id);
}

void target_fit(const target_model_t *tm, const cpx T[MDX_NLINES], target_est_t *e)
{
    memset(e, 0, sizeof(*e));
    float n2 = cpx_norm2(T, MDX_NLINES);
    e->strength = sqrtf(n2);
    if (n2 < 1e-24f) return;
    cpx u[MDX_NLINES]; for (int k = 0; k < MDX_NLINES; k++) u[k] = cpx_make(1, 0);
    float uu = cpx_dot_re(u, u, MDX_NLINES), su = cpx_dot_re(T, u, MDX_NLINES);
    float best = 1e30f; int bi = 0; float ba = 0, bb = 0;
    for (int i = 0; i < TARGET_ZETA_STEPS; i++) {
        const cpx *m = tm->m[i];
        float mm = cpx_dot_re(m, m, MDX_NLINES), um = cpx_dot_re(u, m, MDX_NLINES), sm = cpx_dot_re(T, m, MDX_NLINES);
        float det = uu * mm - um * um;
        float a, b;
        if (fabsf(det) < 1e-12f) { a = su / uu; b = 0; }
        else { a = (su * mm - sm * um) / det; b = (sm * uu - su * um) / det; }
        if (b < 0) { b = 0; a = su / uu; }
        if (a < 0) { a = 0; b = sm / mm; if (b < 0) b = 0; }
        float r = 0;
        for (int k = 0; k < MDX_NLINES; k++) r += cpx_abs2(cpx_sub(T[k], cpx_add(cpx_scale(u[k], a), cpx_scale(m[k], b))));
        if (r < best) { best = r; bi = i; ba = a; bb = b; }
    }
    e->zeta_hz = tm->zeta[bi];
    e->a_mag = ba; e->b_eddy = bb;
    e->cond_id = target_cond_id_from_zeta(e->zeta_hz);
    float den = ba + bb;
    e->fe_index = den > 1e-12f ? 100.0f * ba / den : 0.0f;
    e->confidence = 1.0f - sqrtf(best / n2);
    if (e->confidence < 0) e->confidence = 0;
    e->ferrous = e->fe_index > tm->fe_threshold;
    /* dipole-dipole coupling falls as 1/d^6 for a small target under a large coil */
    float ratio = tm->depth_ref_strength / e->strength;
    e->depth_cm = tm->depth_ref_cm * powf(ratio, 1.0f / 6.0f);
    if (e->depth_cm > 60) e->depth_cm = 60;
    e->valid = 1;
}
