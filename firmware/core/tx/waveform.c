#include "waveform.h"
#include <string.h>

/* From tools/tx_waveform_opt.py (score 0.1756): equal lines at bins 1,2,3,4,8, zero DC */
const int8_t MDX_TX_SLOTS[MDX_SLOTS] = {
     0, 0, 1, 1, 1, 1, 1, 1, 1,-1, 0, 0,-1, 0, 0, 0, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,-1,-1,-1,-1,-1, 0,-1,-1,
    -1, 0,-1,-1, 0, 1, 1, 1, 1, 0, 0,-1,-1,-1, 0, 1, 1, 0, 1, 0,
     0,-1,-1,-1, 0,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0 };
static const float LINE_REL[MDX_NLINES] = {0.2241f, 0.2241f, 0.2253f, 0.2259f, 0.2257f};

float waveform_line_rel_amplitude(int line) { return LINE_REL[line]; }

void waveform_build(int M, tx_table_t *t)
{
    uint32_t total = (uint32_t)M * MDX_TICKS_PER_SAMPLE;
    uint32_t acc = 0;
    for (int i = 0; i < MDX_SLOTS; i++) {
        uint32_t end = (total * (uint32_t)(i + 1)) / MDX_SLOTS;
        t->ticks[i] = (uint16_t)(end - acc);
        acc = end;
        t->level[i] = MDX_TX_SLOTS[i];
    }
}

void waveform_build_single(int M, int k, tx_table_t *t)
{
    /* square wave with k cycles per period; k must divide 80 (1, 2, 4, 5, 8, 10, 16, 20) */
    waveform_build(M, t);
    int cyc = MDX_SLOTS / (k > 0 ? k : 1);
    for (int i = 0; i < MDX_SLOTS; i++) t->level[i] = ((i % cyc) < cyc / 2) ? 1 : -1;
}

void waveform_pi_default(pi_seq_t *s)
{
    memset(s, 0, sizeof(*s));
    s->rail_v = MDX_VBRIDGE_PI_V;
    s->current_a = 2.0f;
    s->reversal_us = 16;        /* 100 V / 0.35 mH: 4 A swing in 14 us */
    s->hold_us = 300;
    s->sample_start_us = 5;
    s->bursts = 32;             /* 32 x ~316 us = 10 ms per PI frame */
}
