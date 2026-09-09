/* Noise cancel: the five inter-line bins (5,6,7,9,10) are free of transmit energy, so their
 * power is a live measurement of interference at the receiver. Scanning steps the period
 * length M (96..104), which moves every line by up to +-4 %, dwells a few periods on each and
 * keeps the quietest. Coherence is preserved at any M because the demodulator length follows. */
#ifndef MDX_NOISECANCEL_H
#define MDX_NOISECANCEL_H
#include "../mdx_config.h"
#include "cpx.h"

#define NC_NCAND (MDX_M_MAX - MDX_M_MIN + 1)
typedef struct {
    int   active;               /* scan in progress                                    */
    int   idx;                  /* candidate index                                     */
    int   dwell;                /* periods remaining at this candidate                 */
    int   settle;               /* periods to discard after a change                   */
    float acc;                  /* accumulated noise power                             */
    cpx   lp[MDX_NLINES], sum[MDX_NLINES]; float sum2;   /* dwell statistics             */
    int   nacc;
    float score[NC_NCAND];
    int   best_M;
    float best_score;
    float current_level;        /* running noise level (ohm units) for the display     */
    int   dwell_periods;
} noisecancel_t;

void noisecancel_init(noisecancel_t *n);
void noisecancel_start(noisecancel_t *n);
/* Feed each period's raw line ratios and inter-line noise. Returns the M to use next (may change).
 * Metric = variance of the low-passed line ratios over the dwell (catches interference inside a
 * line's bin, which shows up as a beat) + inter-line broadband power. */
int  noisecancel_step(noisecancel_t *n, const cpx S[MDX_NLINES], const float noise[MDX_NNOISE], int current_M);
#endif
