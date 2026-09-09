/* Transmit waveform: the optimised 80-slot 3-level pattern (tools/tx_waveform_opt.py) and its
 * timer table for any period length M (samples). Also the PI burst sequencer parameters. */
#ifndef MDX_WAVEFORM_H
#define MDX_WAVEFORM_H
#include "../hal/hal.h"

extern const int8_t MDX_TX_SLOTS[MDX_SLOTS];        /* -1, 0, +1 per slot */

/* Build the per-slot timer table so that the 80 slots sum to exactly M*448 ticks. */
void waveform_build(int M, tx_table_t *t);
/* Expected line voltage magnitudes relative to the rail (from the optimiser), for diagnostics */
float waveform_line_rel_amplitude(int line);
/* Single-frequency table: square wave at bin 'k' of the period (used for salt / single-f modes).
 * The table still spans M samples so the demodulator is unchanged. */
void waveform_build_single(int M, int k, tx_table_t *t);
/* Default PI sequence */
void waveform_pi_default(pi_seq_t *s);
#endif
