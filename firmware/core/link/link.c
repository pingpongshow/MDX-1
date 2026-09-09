#include "link.h"
#include "../dsp/noisecancel.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ------------------------------------------------------------------ small helpers */
uint16_t link_crc16(const uint8_t *d, int n)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
static void p16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void pf(uint8_t *p, float f) { memcpy(p, &f, 4); }
static int  g16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
static int  clampi(float v, int lo, int hi) { int i = (int)lroundf(v); return i < lo ? lo : (i > hi ? hi : i); }
static void pstr(uint8_t *p, const char *s, int n) { memset(p, 0, n); strncpy((char *)p, s, n - 1); }

int link_build_frame(uint8_t *out, int type, int seq, const uint8_t *payload, int len)
{
    if (len < 0 || len > LINK_MAX_PAYLOAD) return 0;
    out[0] = LINK_SOF; out[1] = (uint8_t)len; out[2] = (uint8_t)type; out[3] = (uint8_t)seq;
    if (len) memcpy(out + 4, payload, len);
    uint16_t c = link_crc16(out + 2, len + 2);
    out[4 + len] = (uint8_t)(c >> 8); out[5 + len] = (uint8_t)c;
    return len + 6;
}
static void send(link_t *l, int type, int seq, const uint8_t *p, int n)
{
    int m = link_build_frame(l->tx, type, seq, p, n);
    if (m) { hal_link_write(l->tx, m); l->tx_frames++; }
}
static void send_err(link_t *l, int type, int seq, int code) { uint8_t p[2] = {(uint8_t)type, (uint8_t)code}; send(l, LM_ERR | 0x80, seq, p, 2); }

/* ------------------------------------------------------------------ status packet (LINK_STATUS_LEN bytes) */
int link_pack_status(const link_t *l, uint8_t o[LINK_STATUS_LEN])
{
    const detector_t *d = l->d; const status_t *s = &d->st; const ui_t *u = l->u;
    int ferrous = s->target_present && s->est.fe_index > d->set.fe_limit_lo;
    o[0] = (uint8_t)((s->target_present ? LSF_PRESENT : 0) | (s->accepted ? LSF_ACCEPTED : 0) | (ferrous ? LSF_FERROUS : 0) | (s->pinpoint ? LSF_PINPOINT : 0) |
                     (s->charging ? LSF_CHARGING : 0) | (s->low_batt ? LSF_LOW_BATT : 0) | (s->coil_present ? LSF_COIL : 0) | (s->nc_active ? LSF_NC_ACTIVE : 0));
    p16(o + 1, clampi(s->est.cond_id * 10.0f, -1000, 1000));
    o[3] = (uint8_t)clampi(s->est.fe_index, 0, 100);
    o[4] = (uint8_t)clampi(s->est.confidence * 100.0f, 0, 100);
    o[5] = (uint8_t)(int8_t)clampi(s->strength_db, -100, 100);
    o[6] = (uint8_t)clampi(s->est.depth_cm, 0, 255);
    o[7] = (uint8_t)(s->pinpoint ? clampi(s->pinpoint_level * 255.0f, 0, 255) : clampi(s->strength_db / 40.0f * 255.0f, 0, 255));
    p16(o + 8, clampi(s->ground_phase_deg * 10.0f, -1800, 1800));
    o[10] = (uint8_t)clampi(s->ground_strength * 1e3f, 0, 255);
    o[11] = (uint8_t)clampi(s->salt_ratio * 100.0f, 0, 100);
    o[12] = (uint8_t)clampi(s->hotrock * 100.0f, 0, 255);
    o[13] = (uint8_t)s->noise_M;
    p16(o + 14, clampi(s->noise_level * 1e6f, 0, 65535));
    p16(o + 16, clampi(s->itx_rms_a * 1e3f, 0, 65535));
    o[18] = (uint8_t)clampi(s->vtx_v * 10.0f, 0, 255);
    o[19] = (uint8_t)clampi((float)s->batt_pct, 0, 100);
    p16(o + 20, clampi(s->batt_v * 1e3f, 0, 65535));
    o[22] = (uint8_t)clampi(s->batt_hours * 10.0f, 0, 255);
    for (int k = 0; k < MDX_NLINES; k++) o[23 + k] = (uint8_t)clampi(s->spectrum[k] * 255.0f, 0, 255);
    o[28] = (uint8_t)(s->gnss.valid ? 1 : 0);
    p32(o + 29, (uint32_t)(int32_t)llround(s->gnss.lat * 1e7));
    p32(o + 33, (uint32_t)(int32_t)llround(s->gnss.lon * 1e7));
    o[37] = (uint8_t)clampi((float)s->gnss.sats, 0, 255);
    o[38] = (uint8_t)clampi(s->gnss.hdop * 10.0f, 0, 255);
    p16(o + 39, clampi((float)s->finds, 0, 65535));
    o[41] = (uint8_t)clampi(s->pi_depth_cm, 0, 255);
    o[42] = (uint8_t)(int8_t)(s->match_idx < 0 ? -1 : s->match_idx);
    o[43] = d->set.program; o[44] = d->set.mode; o[45] = d->set.sensitivity;
    p32(o + 46, s->uptime_s);
    o[50] = (uint8_t)clampi(s->pinpoint_level * 255.0f, 0, 255);
    o[51] = (uint8_t)clampi(s->swing_speed * 10.0f, 0, 255);
    o[52] = (uint8_t)(s->pi_target ? 1 : 0);
    o[53] = (uint8_t)(u && u->screen == SCR_COIL_WIZARD ? u->wizard_step : 0xFF);
    o[54] = (uint8_t)(u ? u->screen : 0);
    return LINK_STATUS_LEN;
}

/* ------------------------------------------------------------------ request handlers */
static void h_hello(link_t *l, int seq, const uint8_t *p, int n)
{
    if (n < 3) { send_err(l, LM_HELLO, seq, LE_BAD_LEN); return; }
    int pin = p[1] | (p[2] << 8);
    l->authed = (pin == (int)l->d->set.link_pin);
    uint8_t r[30]; memset(r, 0, sizeof r);
    r[0] = LINK_PROTO_VER; r[1] = (uint8_t)l->authed; pstr(r + 2, LINK_FW_VERSION, 8); pstr(r + 10, LINK_HW_REV, 4);
    r[14] = (uint8_t)ui_item_count(); p16(r + 15, log_find_count(l->lg)); r[17] = LINK_STATUS_LEN; pstr(r + 18, "MDX-1", 12);
    send(l, LM_HELLO | 0x80, seq, r, sizeof r);
    if (!l->authed) send_err(l, LM_HELLO, seq, LE_BAD_PIN);
}
static void h_item(link_t *l, int seq, const uint8_t *p, int n)
{
    if (n < 1) { send_err(l, LM_ITEM, seq, LE_BAD_LEN); return; }
    ui_item_info_t it; if (!ui_item_info(p[0], &it)) { send_err(l, LM_ITEM, seq, LE_BAD_INDEX); return; }
    uint8_t r[LINK_MAX_PAYLOAD]; int k = 0;
    r[k++] = p[0]; r[k++] = (uint8_t)it.is_action; p16(r + k, it.lo); k += 2; p16(r + k, it.hi); k += 2; r[k++] = (uint8_t)it.nlabels;
    k += (int)strlen(it.name) + 1 <= LINK_MAX_PAYLOAD - k ? (int)(strlen(strcpy((char *)r + k, it.name)) + 1) : 0;
    for (int i = 0; i < it.nlabels && k + (int)strlen(it.labels[i]) + 1 <= LINK_MAX_PAYLOAD; i++) { strcpy((char *)r + k, it.labels[i]); k += (int)strlen(it.labels[i]) + 1; }
    send(l, LM_ITEM | 0x80, seq, r, k);
}
static void h_values(link_t *l, int seq)
{
    uint8_t r[1 + 2 * 64]; int n = ui_item_count(); if (n > 64) n = 64;
    r[0] = (uint8_t)n; for (int i = 0; i < n; i++) p16(r + 1 + 2 * i, ui_item_get(l->d, i));
    send(l, LM_VALUES | 0x80, seq, r, 1 + 2 * n);
}
static void h_set(link_t *l, int seq, const uint8_t *p, int n)
{
    if (n < 3) { send_err(l, LM_SET, seq, LE_BAD_LEN); return; }
    int rc = ui_item_set(l->u, l->d, p[0], g16(p + 1));
    if (rc == 1) { send_err(l, LM_SET, seq, LE_BAD_INDEX); return; }
    if (rc == 2) { send_err(l, LM_SET, seq, LE_BAD_VALUE); return; }
    uint8_t r[3]; r[0] = p[0]; p16(r + 1, ui_item_get(l->d, p[0])); send(l, LM_SET | 0x80, seq, r, 3);
}
static void h_action(link_t *l, int seq, const uint8_t *p, int n)
{
    if (n < 1) { send_err(l, LM_ACTION, seq, LE_BAD_LEN); return; }
    detector_t *d = l->d; ui_t *u = l->u; int res = 0;
    switch (p[0]) {
    case LA_NOISE_CANCEL: if (d->nc.active) res = LE_BUSY; else { noisecancel_start(&d->nc); if (u) u->screen = SCR_NOISE_CANCEL; } break;
    case LA_GROUND_GRAB:  detector_ground_grab(d); break;
    case LA_PINPOINT_ON:  detector_set_pinpoint(d, 1); if (u) u->screen = SCR_PINPOINT; break;
    case LA_PINPOINT_OFF: detector_set_pinpoint(d, 0); if (u && u->screen == SCR_PINPOINT) u->screen = SCR_HOME; break;
    case LA_LEARN: {
        if (!d->st.target_present) { res = LE_BUSY; break; }
        char name[CLS_NAME]; memset(name, 0, sizeof name); int m = n - 1; if (m > CLS_NAME - 1) m = CLS_NAME - 1; memcpy(name, p + 1, m);
        if (!name[0]) snprintf(name, sizeof name, "P%02d", (int)d->st.est.cond_id);
        detector_learn_target(d, name); break; }
    case LA_FORGET: if (n < 2 || p[1] >= CLS_MAX) res = LE_BAD_INDEX; else classify_forget(&d->cls, p[1]); break;
    case LA_MARK_FIND: l->mark_find = 1; break;
    case LA_SAVE: detector_save_settings(d); break;
    case LA_WIZARD_START: if (u) { u->screen = SCR_COIL_WIZARD; u->wizard_step = 0; u->wiz_L = u->wiz_R = 0; } break;
    case LA_WIZARD_STEP: if (u && u->screen == SCR_COIL_WIZARD) ui_wizard_advance(u, d); else res = LE_BUSY; break;
    case LA_WIZARD_CANCEL: if (u && u->screen == SCR_COIL_WIZARD) u->screen = SCR_HOME; break;
    case LA_POWER_OFF: l->want_power_off = 1; break;
    case LA_DEFAULTS: { uint16_t pin = d->set.link_pin; detector_default_settings(&d->set); d->set.link_pin = pin; detector_apply_settings(d); d->settings_dirty = 1; break; }
    case LA_ACCEPT_ALL: memset(d->set.accept, 0xFF, sizeof d->set.accept); detector_apply_settings(d); d->settings_dirty = 1; break;
    default: send_err(l, LM_ACTION, seq, LE_UNKNOWN); return;
    }
    uint8_t r[2] = {p[0], (uint8_t)res}; send(l, LM_ACTION | 0x80, seq, r, 2);
}
static void h_status(link_t *l, int seq, const uint8_t *p, int n, uint32_t now)
{
    if (n >= 2) {
        int per = p[0] | (p[1] << 8);
        if (per == 0xFFFF) { /* one-shot */ }
        else { l->status_period_ms = (uint16_t)(per == 0 ? 0 : (per < 50 ? 50 : per)); l->next_status_ms = now + l->status_period_ms; }
    }
    uint8_t r[LINK_STATUS_LEN]; link_pack_status(l, r); send(l, LM_STATUS | 0x80, seq, r, LINK_STATUS_LEN);
}
static void h_find(link_t *l, int seq, const uint8_t *p, int n)
{
    if (n < 2) { send_err(l, LM_FIND, seq, LE_BAD_LEN); return; }
    int idx = p[0] | (p[1] << 8); find_rec_t f;
    if (!log_find_get(l->lg, idx, &f)) { send_err(l, LM_FIND, seq, LE_BAD_INDEX); return; }
    uint8_t r[31]; p16(r, idx); p32(r + 2, f.utc_s); p32(r + 6, (uint32_t)(int32_t)llround(f.lat * 1e7)); p32(r + 10, (uint32_t)(int32_t)llround(f.lon * 1e7));
    p16(r + 14, f.cond_id); r[16] = f.fe; r[17] = f.depth_cm; r[18] = f.conf; pstr(r + 19, f.name, 12);
    send(l, LM_FIND | 0x80, seq, r, sizeof r);
}
static void h_learned(link_t *l, int seq)
{
    uint8_t r[1 + CLS_MAX * 14]; int k = 1, cnt = 0;
    for (int i = 0; i < CLS_MAX; i++) {
        const cls_entry_t *e = &l->d->cls.e[i]; if (!e->used) continue;
        r[k++] = (uint8_t)i; pstr(r + k, e->name, 11); k += 11; r[k++] = (uint8_t)clampi(e->cond_id, 0, 255); r[k++] = (uint8_t)clampi(e->fe_index, 0, 255); cnt++;
    }
    r[0] = (uint8_t)cnt; send(l, LM_LEARNED | 0x80, seq, r, k);
}
static void h_diag(link_t *l, int seq)
{
    const detector_t *d = l->d; const status_t *s = &d->st; uint8_t r[109]; int k = 0;
    float v[] = {hal_rail_v(0), hal_rail_v(1), hal_rail_v(2), hal_rail_v(3), hal_rail_v(4), s->vtx_v, s->vbridge_v, s->itx_rms_a, s->temp_c,
                 d->noise_floor, d->thresh, d->ground.a, d->ground.b, s->ground_phase_deg, d->coil.cal.L_tx_mH, d->coil.cal.R_tx_ohm,
                 d->demod.f_hz[0], d->demod.f_hz[1], d->demod.f_hz[2], d->demod.f_hz[3], d->demod.f_hz[4]};
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) { pf(r + k, v[i]); k += 4; }
    p32(r + k, s->periods); k += 4; p32(r + k, s->pi_records); k += 4; p32(r + k, s->uptime_s); k += 4;
    r[k++] = (uint8_t)((s->tx_fault ? 1 : 0) | (s->clipped ? 2 : 0) | (s->coil_present ? 4 : 0) | (d->coil.calibrated ? 8 : 0));
    pstr(r + k, d->coil.cal.name, 12); k += 12;    /* 21 floats (84) + 3 u32 (12) + flags (1) + name (12) = 109 */
    send(l, LM_DIAG | 0x80, seq, r, k);
}

static void dispatch(link_t *l, int type, int seq, const uint8_t *p, int n, uint32_t now)
{
    l->last_rx_ms = now;
    if (type != LM_HELLO && !l->authed) { send_err(l, type, seq, LE_NOT_AUTH); return; }
    switch (type) {
    case LM_HELLO:   h_hello(l, seq, p, n); break;
    case LM_ITEM:    h_item(l, seq, p, n); break;
    case LM_VALUES:  h_values(l, seq); break;
    case LM_SET:     h_set(l, seq, p, n); break;
    case LM_ACTION:  h_action(l, seq, p, n); break;
    case LM_STATUS:  h_status(l, seq, p, n, now); break;
    case LM_FIND:    h_find(l, seq, p, n); break;
    case LM_LEARNED: h_learned(l, seq); break;
    case LM_DIAG:    h_diag(l, seq); break;
    default:         send_err(l, type, seq, LE_UNKNOWN); break;
    }
}

/* ------------------------------------------------------------------ byte-stream parser */
void link_rx_bytes(link_t *l, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        uint8_t c = b[i];
        switch (l->rx_state) {
        case 0: if (c == LINK_SOF) { l->rx_n = 0; l->rx_state = 1; } break;
        case 1: if (c > LINK_MAX_PAYLOAD) { l->rx_state = 0; l->frames_bad++; } else { l->rx_len = c; l->rx_state = 2; } break;
        default:
            l->rx[l->rx_n++] = c;                                   /* type, seq, payload, crc hi, crc lo */
            if (l->rx_n == l->rx_len + 4) {
                uint16_t want = (uint16_t)((l->rx[l->rx_len + 2] << 8) | l->rx[l->rx_len + 3]);
                if (link_crc16(l->rx, l->rx_len + 2) == want) { l->frames_ok++; dispatch(l, l->rx[0], l->rx[1], l->rx + 2, l->rx_len, hal_millis()); }
                else { l->frames_bad++; send_err(l, l->rx[0], l->rx[1], LE_BAD_CRC); }
                l->rx_state = 0;
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ periodic work */
static void send_event(link_t *l, int ev, int a, int b)
{
    uint8_t p[4] = {(uint8_t)ev, (uint8_t)a, (uint8_t)b, (uint8_t)(b >> 8)}; send(l, LM_EVENT | 0x80, 0, p, 4);
}
void link_tick(link_t *l, uint32_t now)
{
    int conn = hal_link_connected();
    if (!conn && l->connected) link_disconnected(l);
    l->connected = conn;
    if (!l->connected || !l->authed) return;
    const detector_t *d = l->d;
    /* events on edges */
    if (l->last_nc_active && !d->st.nc_active) send_event(l, LV_NC_DONE, d->st.noise_M, (int)(d->st.noise_level * 1e6f));
    l->last_nc_active = d->st.nc_active;
    if (d->st.finds != l->last_finds) { send_event(l, LV_FIND, 0, (int)d->st.finds); l->last_finds = d->st.finds; }
    if (d->st.low_batt && !l->last_low_batt) send_event(l, LV_LOW_BATT, d->st.batt_pct, (int)(d->st.batt_v * 1e3f));
    l->last_low_batt = d->st.low_batt;
    int ws = (l->u && l->u->screen == SCR_COIL_WIZARD) ? l->u->wizard_step : 0xFF;
    if (ws != l->last_wizard_step) { send_event(l, LV_WIZARD, ws, 0); l->last_wizard_step = ws; }
    /* status stream */
    if (l->status_period_ms && (int32_t)(now - l->next_status_ms) >= 0) {
        l->next_status_ms = now + l->status_period_ms;
        uint8_t r[LINK_STATUS_LEN]; link_pack_status(l, r); send(l, LM_STATUS | 0x80, 0, r, LINK_STATUS_LEN);
    }
    /* a phone that has gone quiet for 30 s stops the stream (BLE supervision usually drops it first) */
    if (l->status_period_ms && now - l->last_rx_ms > 30000) l->status_period_ms = 0;
}
void link_disconnected(link_t *l) { l->authed = 0; l->status_period_ms = 0; l->rx_state = 0; }
void link_init(link_t *l, detector_t *d, ui_t *u, logger_t *lg)
{
    memset(l, 0, sizeof(*l)); l->d = d; l->u = u; l->lg = lg; l->last_wizard_step = 0xFF;
}
