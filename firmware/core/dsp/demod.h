/* Coherent demodulation of one transmit period.
 * S_k = RX_k / ITX_k  at the five line bins; units: coil volts per transmit amp (ohms).
 * Because both channels are digitised by identical converters and filters, the ratio
 * is independent of rail voltage, coil inductance drift and filter group delay. */
#ifndef MDX_DEMOD_H
#define MDX_DEMOD_H
#include "../mdx_config.h"
#include "cpx.h"

typedef struct {
    int   M;
    float cs[MDX_NLINES + MDX_NNOISE][MDX_MAXM];
    float sn[MDX_NLINES + MDX_NNOISE][MDX_MAXM];
    float f_hz[MDX_NLINES];
} demod_t;

typedef struct {
    cpx   S[MDX_NLINES];        /* transfer impedance per line (ohm)                 */
    cpx   rx_v[MDX_NLINES];     /* coil volts per line (peak)                        */
    cpx   itx_a[MDX_NLINES];    /* transmit amps per line (peak)                     */
    float noise[MDX_NNOISE];    /* RX magnitude at the inter-line bins, ohm units    */
    float itx_rms_a;            /* transmit rms current (all content)                */
    float rx_peak_v;            /* largest |rx| sample in the period, coil volts     */
    int   clipped;              /* ADC near full scale                               */
} demod_out_t;

void demod_init(demod_t *d, int M);
void demod_period(const demod_t *d, const int32_t *rx, const int32_t *itx, demod_out_t *o);
#endif
