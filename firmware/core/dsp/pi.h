/* Pulse-induction processing of the decay following each current reversal.
 * Windows (us after the reversal, at 512 kSPS = 1.953 us per sample):
 *   early  5..30 us   : small / low-conductivity targets, also ground (viscous) response
 *   mid   30..60 us
 *   late  60..200 us  : deep or large high-conductivity targets, ground response weak
 * Ground balance: viscous ground decays as ~1/t, so a fixed ratio between the early
 * and late windows (measured during ground grab) cancels it: sig = early - k * late.
 * Two early sub-windows give a time-constant estimate (target size / conductivity). */
#ifndef MDX_PI_H
#define MDX_PI_H
#include "../hal/hal.h"

typedef struct {
    float k_gb;              /* early/late ground ratio                                   */
    float k_gb_mid;          /* mid/late ground ratio                                     */
    float k_e1, k_e2;        /* sub-window ground ratios for the tau estimate             */
    int   gb_valid;
    float baseline_early;    /* averaged residual with no target (for thresholding)       */
    float baseline_late;
    float noise;             /* running rms of the balanced signal (no target)            */
} pi_state_t;

typedef struct {
    float early, mid, late;  /* window sums, coil volts                                   */
    float e1, e2;            /* equal-width early sub-windows 5-17.5, 17.5-30 us          */
    float sig_early;         /* ground-balanced early channel                             */
    float sig_late;          /* late channel (deep targets)                               */
    float tau_us;            /* decay time constant estimate                              */
    float snr;               /* sig_early / noise                                         */
    int   target;            /* detection flag                                            */
} pi_result_t;

void pi_init(pi_state_t *p);
void pi_process(pi_state_t *p, const pi_record_t *r, pi_result_t *out);
void pi_ground_grab(pi_state_t *p, const pi_result_t *avg_no_target);
#endif
