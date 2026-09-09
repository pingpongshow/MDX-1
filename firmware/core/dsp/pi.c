#include "pi.h"
#include <math.h>
#include <string.h>

#define US_PER_SAMPLE (1e6f / MDX_FS_HZ)

static float win(const pi_record_t *r, float t0_us, float t1_us)
{
    int a = (int)(t0_us / US_PER_SAMPLE + 0.5f), b = (int)(t1_us / US_PER_SAMPLE + 0.5f);
    if (b > r->n) b = r->n;
    float s = 0;
    for (int i = a; i < b; i++) s += (float)r->rx[i] * MDX_RX_V_PER_CODE;
    /* the reversal polarity alternates; fold so that a target always adds */
    return r->polarity < 0 ? -s : s;
}

void pi_init(pi_state_t *p)
{
    memset(p, 0, sizeof(*p));
    p->k_gb = 0; p->k_gb_mid = 0; p->noise = 1e-6f;
}

void pi_process(pi_state_t *p, const pi_record_t *r, pi_result_t *o)
{
    o->early = win(r, 5, 30);
    o->mid   = win(r, 30, 60);
    o->late  = win(r, 60, 200);
    float e1 = win(r, 5, 17.5f), e2 = win(r, 17.5f, 30);
    o->e1 = e1; o->e2 = e2;
    o->sig_early = o->early - p->k_gb * o->late;
    o->sig_late  = o->late;
    /* ground-balanced sub-windows; for an exponential of equal-width windows e1/e2 = exp(12.5/tau) */
    float g1 = e1 - p->k_e1 * o->late, g2 = e2 - p->k_e2 * o->late;
    float ratio = (g2 > 1e-12f) ? g1 / g2 : 1e6f;
    o->tau_us = ratio > 1.0001f ? 12.5f / logf(ratio) : 1e4f;
    o->snr = fabsf(o->sig_early) / (p->noise > 1e-12f ? p->noise : 1e-12f);
    o->target = o->snr > 4.0f;
    if (!o->target) p->noise += 0.01f * (fabsf(o->sig_early) - p->noise);
}

void pi_ground_grab(pi_state_t *p, const pi_result_t *g)
{
    if (fabsf(g->late) > 1e-12f) { p->k_gb = g->early / g->late; p->k_gb_mid = g->mid / g->late; p->k_e1 = g->e1 / g->late; p->k_e2 = g->e2 / g->late; p->gb_valid = 1; }
    p->baseline_early = g->early; p->baseline_late = g->late;
}
