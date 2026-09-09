#include "noisecancel.h"
#include <string.h>

static void dwell_reset(noisecancel_t *n) { n->acc = 0; n->nacc = 0; n->sum2 = 0; for (int k = 0; k < MDX_NLINES; k++) n->sum[k] = cpx_make(0, 0); }

void noisecancel_init(noisecancel_t *n)
{
    memset(n, 0, sizeof(*n));
    n->best_M = MDX_M_DEFAULT;
    n->dwell_periods = 256;     /* 50 ms per candidate: resolves beats down to ~20 Hz */
}

void noisecancel_start(noisecancel_t *n)
{
    n->active = 1; n->idx = 0; n->dwell = n->dwell_periods; n->settle = 16; dwell_reset(n);
    n->best_score = 1e30f;
    for (int i = 0; i < NC_NCAND; i++) n->score[i] = 0;
}

static float level(const float noise[MDX_NNOISE])
{
    float p = 0; for (int i = 0; i < MDX_NNOISE; i++) p += noise[i] * noise[i];
    return p;
}


int noisecancel_step(noisecancel_t *n, const cpx S[MDX_NLINES], const float noise[MDX_NNOISE], int current_M)
{
    float p = level(noise);
    /* low-pass the lines like the fast detection channel (alpha 1/8) */
    for (int k = 0; k < MDX_NLINES; k++) n->lp[k] = cpx_add(n->lp[k], cpx_scale(cpx_sub(S[k], n->lp[k]), 0.125f));
    n->current_level += 0.05f * (p - n->current_level);
    if (!n->active) return current_M;
    int cand = MDX_M_MIN + n->idx;
    if (current_M != cand) return cand;                 /* ask the system to switch first */
    if (n->settle > 0) { n->settle--; if (n->settle == 0) dwell_reset(n); return cand; }
    n->acc += p; n->nacc++;
    for (int k = 0; k < MDX_NLINES; k++) { n->sum[k] = cpx_add(n->sum[k], n->lp[k]); n->sum2 += cpx_abs2(n->lp[k]); }
    if (--n->dwell > 0) return cand;
    float var = n->sum2 / (float)n->nacc;
    for (int k = 0; k < MDX_NLINES; k++) var -= cpx_abs2(cpx_scale(n->sum[k], 1.0f / (float)n->nacc));
    if (var < 0) var = 0;
    n->score[n->idx] = var + n->acc / (float)(n->nacc > 0 ? n->nacc : 1);
    if (n->score[n->idx] < n->best_score) { n->best_score = n->score[n->idx]; n->best_M = cand; }
    n->idx++; dwell_reset(n); n->dwell = n->dwell_periods; n->settle = 16;
    if (n->idx >= NC_NCAND) { n->active = 0; return n->best_M; }
    return MDX_M_MIN + n->idx;
}
