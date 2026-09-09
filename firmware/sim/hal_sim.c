#include "sim.h"
#include "../core/tx/waveform.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

sim_t SIM;

static float frand(void) { SIM.rng = SIM.rng * 1664525u + 1013904223u; return (float)(SIM.rng >> 8) / 16777216.0f; }
static float gauss(void) { float u1 = frand() + 1e-9f, u2 = frand(); return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2); }

void sim_reset(void)
{
    memset(&SIM, 0, sizeof(SIM)); SIM.ui_present = 1;
    SIM.L_tx_H = 0.36e-3f; SIM.R_tx_ohm = 0.63f; SIM.V_rail = 12.0f; SIM.rail_set = 12.0f;
    for (int k = 0; k < MDX_NLINES; k++) { SIM.null[k] = cpx_make(300e-6f * (1 + k), -150e-6f); SIM.phys_rot_deg[k] = 15.0f + 4.0f * (float)k; }
    SIM.g_mag = 0; SIM.g_salt = 0; SIM.noise_v_rms = 20e-6f; SIM.interf_hz = 0; SIM.interf_v = 0;
    SIM.sweep_amp_m = 0.4f; SIM.sweep_period_s = 1.6f; SIM.sweeping = 0; SIM.coil_x_m = 0;
    SIM.M = MDX_M_DEFAULT; waveform_build(SIM.M, &SIM.txtab); SIM.tx_on = 1;
    SIM.batt_v = 3.9f; SIM.gnss_valid = 1; SIM.lat = 51.5074; SIM.lon = -0.1278;
    SIM.rng = 12345u;
    SIM.eeprom_present = 1;
    waveform_pi_default(&SIM.piseq);
}

void sim_add_target(float x, float depth, float zeta, float eddy, float mag)
{
    for (int i = 0; i < SIM_MAX_TARGETS; i++) if (!SIM.tgt[i].active) { SIM.tgt[i] = (sim_target_t){x, depth, zeta, eddy, mag, 1}; return; }
}

float sim_coil_x(void) { return SIM.sweeping ? SIM.sweep_amp_m * sinf(2.0f * (float)M_PI * (float)SIM.t_s / SIM.sweep_period_s) : SIM.coil_x_m; }
float sim_coil_speed(void) { return SIM.sweeping ? fabsf(SIM.sweep_amp_m * 2.0f * (float)M_PI / SIM.sweep_period_s * cosf(2.0f * (float)M_PI * (float)SIM.t_s / SIM.sweep_period_s)) : 0.0f; }
void sim_set_time(double t) { SIM.t_s = t; }

static cpx eddy(float f, float zeta) { float x = f / zeta, d = 1 + x * x; return cpx_make(-x * x / d, -x / d); }

/* physical transfer impedance at each line: everything in the "true" frame, then rotated by the coil phase, plus the residual null */
void sim_line_S(cpx S[MDX_NLINES], float coil_x)
{
    for (int k = 0; k < MDX_NLINES; k++) {
        float f = mdx_line_hz(k, SIM.M);
        cpx s = cpx_make(0, 0);
        s = cpx_add(s, cpx_scale(cpx_make(1.0f, 0.05f), SIM.g_mag));
        s = cpx_add(s, cpx_scale(cpx_make(0.0f, f / 10240.0f), SIM.g_salt));
        for (int i = 0; i < SIM_MAX_TARGETS; i++) if (SIM.tgt[i].active) {
            float dx = SIM.tgt[i].x_m - coil_x, r = sqrtf(dx * dx + SIM.tgt[i].depth_m * SIM.tgt[i].depth_m);
            float c = powf(0.10f / r, 6.0f);
            /* lateral response of a DD coil: a narrow strip; taper with a gaussian of 0.12 m */
            c *= expf(-dx * dx / (2 * 0.12f * 0.12f)) * (1.0f + 0.5f * (SIM.tgt[i].depth_m > 0.1f ? 1 : 0)) / 1.5f;
            s = cpx_add(s, cpx_scale(cpx_make(1, 0), SIM.tgt[i].mag * c));
            s = cpx_add(s, cpx_scale(eddy(f, SIM.tgt[i].zeta_hz), SIM.tgt[i].eddy * c));
        }
        cpx rot = cpx_expj(-SIM.phys_rot_deg[k] * (float)M_PI / 180.0f);   /* physical rotation: S_meas = S_true * e^{-j phi} */
        S[k] = cpx_add(cpx_mul(s, rot), SIM.null[k]);
    }
}

/* ---------------- acquisition: simulate one period ------------------------------------------ */
static void simulate_itx(float *i_out, int M)
{
    /* exact RL response: the slot edges fall on timer ticks (1/448 sample), so integrate the
     * closed-form exponential piecewise between edges and sample instants */
    float tau = SIM.L_tx_H / SIM.R_tx_ohm;
    const float tick_s = 1.0f / (MDX_FS_HZ * MDX_TICKS_PER_SAMPLE);
    uint32_t t = 0, slot_end = SIM.txtab.ticks[0]; int s = 0;
    for (int n = 0; n < M; n++) {
        uint32_t t_sample = (uint32_t)(n + 1) * MDX_TICKS_PER_SAMPLE;
        while (t < t_sample) {
            uint32_t seg_end = slot_end < t_sample ? slot_end : t_sample;
            float v = SIM.tx_on ? (float)SIM.txtab.level[s] * SIM.V_rail : 0.0f;
            float e = expf(-(float)(seg_end - t) * tick_s / tau);
            SIM.i_state = SIM.i_state * e + (v / SIM.R_tx_ohm) * (1 - e);
            t = seg_end;
            if (t >= slot_end && s < MDX_SLOTS - 1) { s++; slot_end += SIM.txtab.ticks[s]; }
        }
        i_out[n] = SIM.i_state;
    }
}

int hal_adc_get_frame(adc_frame_t *f)
{
    int M = SIM.M; f->M = M; f->seq = SIM.seq++; f->t_ms = (uint32_t)(SIM.t_s * 1000.0);
    float itx[MDX_MAXM]; simulate_itx(itx, M);
    /* line phasors of the current (peak) via DFT for bins 1..40 */
    cpx I[41]; for (int k = 1; k <= 40; k++) { float re = 0, im = 0; for (int n = 0; n < M; n++) { float th = 2 * (float)M_PI * k * n / M; re += itx[n] * cosf(th); im -= itx[n] * sinf(th); } I[k] = cpx_make(2 * re / M, 2 * im / M); }
    cpx S[MDX_NLINES]; sim_line_S(S, sim_coil_x());
    /* RX = sum over harmonics of Re(S_k I_k e^{j w t}); non-line harmonics couple only through the residual null of the nearest line */
    static float rx[MDX_MAXM];
    for (int n = 0; n < M; n++) rx[n] = 0;
    for (int k = 1; k <= 40; k++) {
        cpx s = SIM.null[MDX_NLINES - 1];
        for (int l = 0; l < MDX_NLINES; l++) if (MDX_LINE_BIN[l] == k) s = S[l];
        cpx v = cpx_mul(s, I[k]);
        for (int n = 0; n < M; n++) { float th = 2 * (float)M_PI * k * n / M; rx[n] += v.re * cosf(th) - v.im * sinf(th); }
    }
    for (int n = 0; n < M; n++) {
        double t = SIM.t_s + (double)n / MDX_FS_HZ;
        float v = rx[n] + SIM.noise_v_rms * gauss();
        if (SIM.interf_v > 0) v += SIM.interf_v * sinf(2.0f * (float)M_PI * SIM.interf_hz * (float)t);
        float code = v / MDX_RX_V_PER_CODE; if (code > 8388607) code = 8388607; if (code < -8388608) code = -8388608;
        f->rx[n] = (int32_t)code;
        float ic = itx[n] / MDX_ITX_A_PER_CODE + 30.0f * gauss(); if (ic > 8388607) ic = 8388607; if (ic < -8388608) ic = -8388608;
        f->itx[n] = (int32_t)ic;
    }
    SIM.t_s += (double)M / MDX_FS_HZ;
    if (SIM.pi_pending && SIM.piseq.continuous && SIM.pi_avail <= 0) SIM.pi_avail = SIM.piseq.bursts;   /* re-armed by the ISR */
    return 1;
}

int hal_adc_get_pi_record(pi_record_t *r)
{
    if (!SIM.pi_pending || SIM.pi_avail <= 0) return 0;
    int n = (int)(SIM.piseq.hold_us * MDX_FS_HZ / 1e6f); if (n > MDX_PI_MAX_SAMPLES) n = MDX_PI_MAX_SAMPLES;
    r->n = n; r->polarity = (SIM.pi_burst_idx & 1) ? -1 : 1; r->t_ms = (uint32_t)(SIM.t_s * 1000);
    float x = sim_coil_x();
    for (int i = 0; i < n; i++) {
        float t = (float)i / MDX_FS_HZ + SIM.piseq.sample_start_us * 1e-6f;
        float v = 0;
        for (int j = 0; j < SIM_MAX_TARGETS; j++) if (SIM.tgt[j].active) {
            float dx = SIM.tgt[j].x_m - x, rr = sqrtf(dx * dx + SIM.tgt[j].depth_m * SIM.tgt[j].depth_m);
            float c = powf(0.10f / rr, 6.0f) * expf(-dx * dx / (2 * 0.12f * 0.12f));
            float tau = 1.0f / (2 * (float)M_PI * SIM.tgt[j].zeta_hz);
            /* induced voltage ~ eddy strength * 2 A reversal * exp(-t/tau) scaled to volts */
            v += SIM.tgt[j].eddy * 2.0f * 100.0f * c * expf(-t / tau);
        }
        v += SIM.pi_ground * (10e-6f / (t + 2e-6f));       /* viscous ground ~ 1/t */
        v += SIM.noise_v_rms * gauss();
        v *= (float)r->polarity;
        float code = v / MDX_RX_V_PER_CODE; if (code > 8388607) code = 8388607; if (code < -8388608) code = -8388608;
        r->rx[i] = (int32_t)code;
    }
    SIM.t_s += (double)(SIM.piseq.hold_us + SIM.piseq.reversal_us) * 1e-6;
    SIM.pi_avail--;
    if (++SIM.pi_burst_idx >= SIM.piseq.bursts) { SIM.pi_burst_idx = 0; if (!SIM.piseq.continuous) SIM.pi_pending = 0; }
    return 1;
}

void hal_adc_set_period(int M) { SIM.M = M; }
void hal_tx_set_table(const tx_table_t *t) { SIM.txtab = *t; }
void hal_tx_enable(int on) { SIM.tx_on = on; }
void hal_tx_set_rail(float v) { SIM.rail_set = v; SIM.V_rail = v; }
void hal_tx_run_pi(const pi_seq_t *seq) { SIM.piseq = *seq; SIM.pi_pending = 1; SIM.pi_burst_idx = 0; SIM.pi_avail = seq->bursts; }
int hal_tx_pi_busy(void) { return SIM.pi_pending; }
void hal_tx_pi_stop(void) { SIM.pi_pending = 0; SIM.pi_burst_idx = 0; SIM.pi_avail = 0; }
float hal_tx_rail_v(void) { return SIM.V_rail; }
float hal_bridge_rail_v(void) { return SIM.pi_pending ? 100.0f : SIM.V_rail - 0.5f; }
int hal_tx_fault(void) { return 0; }
void hal_tx_clear_fault(void) {}

/* ---------------- UI / peripherals -------------------------------------------------------- */
static uint8_t last_fb[LCD_H * LCD_STRIDE];
void hal_lcd_update(const uint8_t *fb) { memcpy(last_fb, fb, sizeof last_fb); SIM.lcd_updates++; }
const uint8_t *sim_last_fb(void) { return last_fb; }
void hal_lcd_frontlight(int pct) { (void)pct; }
uint32_t hal_keys(void) { return SIM.keys; }
int hal_ui_present(void) { return SIM.ui_present; }
void hal_status_led(int on) { SIM.led_on = on; }
static audio_fill_cb g_cb; static void *g_ctx;
void hal_audio_start(audio_fill_cb cb, void *ctx) { g_cb = cb; g_ctx = ctx; }
void sim_audio_pull(int16_t *buf, int n) { if (g_cb) g_cb(buf, n, g_ctx); }
void hal_audio_route(int h, int s, int b) { (void)h; (void)s; (void)b; }
int hal_headphone_present(void) { return 1; }
int hal_imu_read(imu_sample_t *s) { float v = sim_coil_speed(); s->gx = v / 1.2f; s->gy = 0; s->gz = 0; s->ax = 0; s->ay = 0; s->az = 9.81f; return 1; }
int hal_gnss_read(gnss_fix_t *fix) { fix->valid = SIM.gnss_valid; fix->lat = SIM.lat; fix->lon = SIM.lon; fix->alt_m = 30; fix->hdop = 1.1f; fix->utc_s = 1789000000u + (uint32_t)SIM.t_s; fix->sats = 9; return 1; }
float hal_battery_v(void) { return SIM.batt_v; }
int hal_battery_pct(void) { float p = (SIM.batt_v - 3.3f) / (4.2f - 3.3f) * 100.0f; return p < 0 ? 0 : (p > 100 ? 100 : (int)p); }
int hal_charging(void) { return SIM.charging; }
float hal_rail_v(int r) { static const float v[] = {3.9f, 3.3f, 5.0f, -5.0f, 12.0f, 0, 0}; if (r == 5) return SIM.V_rail; if (r == 6) return hal_bridge_rail_v(); return v[r]; }
float hal_temp_c(void) { return 24.5f; }
int hal_nvm_read(void *buf, size_t len) { if (!SIM.nvm_valid || len > sizeof SIM.nvm) return 0; memcpy(buf, SIM.nvm, len); return 1; }
int hal_nvm_write(const void *buf, size_t len) { if (len > sizeof SIM.nvm) return 0; memcpy(SIM.nvm, buf, len); SIM.nvm_valid = 1; return 1; }
int hal_coil_eeprom_read(uint8_t buf[128]) { if (!SIM.eeprom_present) return 0; memcpy(buf, SIM.eeprom, 128); return 1; }
int hal_coil_eeprom_write(const uint8_t buf[128]) { if (!SIM.eeprom_present) return 0; memcpy(SIM.eeprom, buf, 128); return 1; }
int hal_sd_append(const char *path, const char *line) { char p[256]; snprintf(p, sizeof p, "build/sd_%s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path); FILE *f = fopen(p, "a"); if (!f) return -1; fputs(line, f); fclose(f); return 0; }
int hal_sd_present(void) { return 1; }
uint32_t hal_millis(void) { return (uint32_t)(SIM.t_s * 1000.0); }
void hal_delay_ms(uint32_t ms) { SIM.t_s += ms * 1e-3; }
void hal_power_off(void) { printf("[sim] power off\n"); }
void hal_analog_enable(int on) { (void)on; }
void hal_hv_enable(int on) { (void)on; }
void hal_hv_bleed(int on) { (void)on; }
int hal_link_write(const uint8_t *b, int n) { int room = (int)sizeof SIM.link_from_det - SIM.link_from_n; if (n > room) n = room; memcpy(SIM.link_from_det + SIM.link_from_n, b, n); SIM.link_from_n += n; return n; }
int hal_link_read(uint8_t *b, int max) { int n = SIM.link_to_n - SIM.link_to_rd; if (n > max) n = max; memcpy(b, SIM.link_to_det + SIM.link_to_rd, n); SIM.link_to_rd += n; if (SIM.link_to_rd == SIM.link_to_n) SIM.link_to_n = SIM.link_to_rd = 0; return n; }
int hal_link_connected(void) { return SIM.link_connected; }
void sim_link_phone_send(const uint8_t *b, int n) { if (SIM.link_to_n + n > (int)sizeof SIM.link_to_det) return; memcpy(SIM.link_to_det + SIM.link_to_n, b, n); SIM.link_to_n += n; }
int sim_link_phone_take(uint8_t *b, int max) { int n = SIM.link_from_n < max ? SIM.link_from_n : max; memcpy(b, SIM.link_from_det, n); memmove(SIM.link_from_det, SIM.link_from_det + n, SIM.link_from_n - n); SIM.link_from_n -= n; return n; }
void hal_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
