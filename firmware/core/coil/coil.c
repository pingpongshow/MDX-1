#include "coil.h"
#include <string.h>
#include <math.h>

uint16_t coil_crc16(const uint8_t *d, int n)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void coil_default(coil_cal_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic = COIL_MAGIC; c->type = COIL_TYPE_DD; c->size_mm = 28; c->version = 1;
    strncpy(c->name, "MDX DD11", sizeof(c->name) - 1);
    c->L_tx_mH = 0.36f; c->R_tx_ohm = 0.63f; c->L_rx_mH = 3.05f; c->R_rx_ohm = 17.6f;
    for (int k = 0; k < MDX_NLINES; k++) { c->null[k] = cpx_make(0, 0); c->rot[k] = cpx_make(1, 0); }
    c->depth_ref_strength = 2.0e-3f;
}

static int valid(const coil_cal_t *c)
{
    return c->magic == COIL_MAGIC && c->crc == coil_crc16((const uint8_t *)c, (int)sizeof(*c) - 2);
}

int coil_load(coil_t *c)
{
    uint8_t buf[128];
    _Static_assert(sizeof(coil_cal_t) == 128, "coil_cal_t must be 128 bytes (DS2431)");
    c->has_chip = 0; c->calibrated = 0;
    if (hal_coil_eeprom_read(buf)) {
        c->has_chip = 1;
        memcpy(&c->cal, buf, 128);
        if (valid(&c->cal)) { c->calibrated = (c->cal.flags & 1) != 0; return 1; }
    }
    /* no chip or blank chip: fall back to defaults; NVM may hold a user calibration for a chip-less coil */
    coil_default(&c->cal);
    return c->has_chip ? 2 : 0;
}

int coil_store(coil_t *c)
{
    c->cal.crc = coil_crc16((const uint8_t *)&c->cal, (int)sizeof(c->cal) - 2);
    if (c->has_chip) return hal_coil_eeprom_write((const uint8_t *)&c->cal);
    return 0;
}

int coil_measure_LR(const int32_t *itx, int M, const tx_table_t *t, float V_rail, float *L_H, float *R_ohm)
{
    /* Build the per-sample level map from the slot table (tick resolution), then regress
     * di/dt against i over every pair of consecutive samples that lies inside a run of
     * constant non-zero level (skipping the two samples around each edge).
     *   di/dt = (lvl*V - R*i)/L   ->   y = didt*lvl = V/L - (R/L)*(i*lvl)              */
    int8_t lvl[MDX_MAXM]; uint32_t tick = 0; int s = 0; uint32_t slot_end = t->ticks[0];
    for (int n = 0; n < M; n++) {
        while (tick >= slot_end && s < MDX_SLOTS - 1) { s++; slot_end += t->ticks[s]; }
        lvl[n] = t->level[s];
        tick += MDX_TICKS_PER_SAMPLE;
    }
    float sx = 0, sy = 0, sxx = 0, sxy = 0; int cnt = 0;
    float dt = 1.0f / MDX_FS_HZ;
    for (int n = 2; n + 3 < M; n++) {
        int8_t l = lvl[n];
        if (l == 0) continue;
        if (lvl[n - 2] != l || lvl[n - 1] != l || lvl[n + 1] != l || lvl[n + 2] != l || lvl[n + 3] != l) continue;   /* interior of a run */
        float ia = (float)itx[n] * MDX_ITX_A_PER_CODE, ib = (float)itx[n + 1] * MDX_ITX_A_PER_CODE;
        float y = ((ib - ia) / dt) * (float)l, x = 0.5f * (ia + ib) * (float)l;
        sx += x; sy += y; sxx += x * x; sxy += x * y; cnt++;
    }
    if (cnt < 8) return 0;
    float den = (float)cnt * sxx - sx * sx;
    if (fabsf(den) < 1e-9f) return 0;
    float c1 = ((float)cnt * sxy - sx * sy) / den;
    float c0 = (sy - c1 * sx) / (float)cnt;
    if (c0 <= 0) return 0;
    *L_H = V_rail / c0;
    *R_ohm = -c1 * (*L_H);
    if (*R_ohm < 0) *R_ohm = 0;
    return 1;
}

void coil_apply(const coil_cal_t *c, const cpx S[MDX_NLINES], cpx out[MDX_NLINES])
{
    for (int k = 0; k < MDX_NLINES; k++) out[k] = cpx_mul(cpx_sub(S[k], c->null[k]), c->rot[k]);
}

void coil_set_null(coil_cal_t *c, const cpx S_air[MDX_NLINES])
{
    for (int k = 0; k < MDX_NLINES; k++) c->null[k] = S_air[k];
    c->flags |= 1;
}

void coil_set_rotation(coil_cal_t *c, const cpx S_ferrite[MDX_NLINES], const cpx S_air[MDX_NLINES])
{
    for (int k = 0; k < MDX_NLINES; k++) {
        cpx d = cpx_sub(S_ferrite[k], S_air[k]);
        float m = cpx_abs(d);
        c->rot[k] = m > 1e-12f ? cpx_make(d.re / m, -d.im / m) : cpx_make(1, 0);   /* conj(unit(d)) */
    }
    c->flags |= 2;
}

void coil_set_depth_ref(coil_cal_t *c, float strength_at_10cm) { c->depth_ref_strength = strength_at_10cm; c->flags |= 4; }

float coil_expected_line_current(const coil_cal_t *c, float V, float f_hz, float rel)
{
    float w = 2.0f * (float)M_PI * f_hz;
    float z = sqrtf(c->R_tx_ohm * c->R_tx_ohm + w * w * (c->L_tx_mH * 1e-3f) * (c->L_tx_mH * 1e-3f));
    return 2.0f * V * rel / z;      /* line voltage peak = 2*V*rel (two-sided spectrum) */
}
