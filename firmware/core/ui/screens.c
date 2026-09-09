#include "screens.h"
#include "../log/log.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ menu definition */
typedef struct { const char *name; int (*get)(const detector_t *); void (*set)(detector_t *, int); int lo, hi; const char *const *labels; int action; } item_t;
static const char *const MODE_L[]  = {"Multi 5-41k", "5.1 kHz", "10.2 kHz", "20.5 kHz", "41 kHz", "PI", "Hybrid V+PI"};
static const char *const PROG_L[]  = {"Park", "Field", "Beach", "Gold", "Relic"};
static const char *const TONE_L[]  = {"VCO", "2 tone", "5 tone", "Pitch"};
static const char *const GB_L[]    = {"Auto track", "Manual", "Off"};
static const char *const OFFON[]   = {"Off", "On"};
static int g_mode(const detector_t *d) { return d->set.mode; }            static void s_mode(detector_t *d, int v) { d->set.mode = (uint8_t)v; }
static int g_prog(const detector_t *d) { return d->set.program; }         static void s_prog(detector_t *d, int v) { detector_load_program(d, v); }
static int g_sens(const detector_t *d) { return d->set.sensitivity; }     static void s_sens(detector_t *d, int v) { d->set.sensitivity = (uint8_t)v; }
static int g_rec(const detector_t *d) { return d->set.recovery; }         static void s_rec(detector_t *d, int v) { d->set.recovery = (uint8_t)v; }
static int g_tone(const detector_t *d) { return d->set.tone_scheme; }     static void s_tone(detector_t *d, int v) { d->set.tone_scheme = (uint8_t)v; }
static int g_vol(const detector_t *d) { return d->set.volume; }           static void s_vol(detector_t *d, int v) { d->set.volume = (uint8_t)v; }
static int g_thr(const detector_t *d) { return d->set.threshold_vol; }    static void s_thr(detector_t *d, int v) { d->set.threshold_vol = (uint8_t)v; }
static int g_iron(const detector_t *d) { return d->set.iron_volume; }     static void s_iron(detector_t *d, int v) { d->set.iron_volume = (uint8_t)v; }
static int g_gb(const detector_t *d) { return d->set.gb_mode; }           static void s_gb(detector_t *d, int v) { d->set.gb_mode = (uint8_t)v; }
static int g_salt(const detector_t *d) { return d->set.salt_mode; }       static void s_salt(detector_t *d, int v) { d->set.salt_mode = (uint8_t)v; }
static int g_tx(const detector_t *d) { return d->set.tx_level; }          static void s_tx(detector_t *d, int v) { d->set.tx_level = (uint8_t)v; }
static int g_fe(const detector_t *d) { return d->set.fe_limit_lo; }       static void s_fe(detector_t *d, int v) { d->set.fe_limit_lo = (uint8_t)v; }
static int g_hyb(const detector_t *d) { return d->set.hybrid_ratio; }     static void s_hyb(detector_t *d, int v) { d->set.hybrid_ratio = (uint8_t)v; }
static int g_fl(const detector_t *d) { return d->set.frontlight; }        static void s_fl(detector_t *d, int v) { d->set.frontlight = (uint8_t)v; }
static int g_log(const detector_t *d) { return d->set.log_enable; }       static void s_log(detector_t *d, int v) { d->set.log_enable = (uint8_t)v; }
static int g_units(const detector_t *d) { return d->set.units_in; }       static void s_units(detector_t *d, int v) { d->set.units_in = (uint8_t)v; }
static int g_sauto(const detector_t *d) { return d->set.sens_auto; }      static void s_sauto(detector_t *d, int v) { d->set.sens_auto = (uint8_t)v; }
static int g_anc(const detector_t *d) { return d->set.auto_nc_at_boot; }  static void s_anc(detector_t *d, int v) { d->set.auto_nc_at_boot = (uint8_t)v; }
static int g_grunt(const detector_t *d) { return d->set.iron_grunt; }     static void s_grunt(detector_t *d, int v) { d->set.iron_grunt = (uint8_t)v; }
static int g_pin(const detector_t *d) { return d->set.link_pin; }          static void s_pin(detector_t *d, int v) { d->set.link_pin = (uint16_t)v; }
static const char *const UNITS_L[] = {"cm", "inch"};
enum { ACT_NONE = 0, ACT_NC, ACT_COIL, ACT_DIAG, ACT_LEARN, ACT_LOG, ACT_SAVE, ACT_DISC };
static const item_t MENU[] = {
    {"Program",        g_prog,  s_prog,  0, PROG_COUNT - 1, PROG_L, 0},
    {"Mode",           g_mode,  s_mode,  0, MODE_COUNT - 1, MODE_L, 0},
    {"Sensitivity",    g_sens,  s_sens,  1, 30, 0, 0},
    {"Auto sensitivity", g_sauto, s_sauto, 0, 1, OFFON, 0},
    {"Recovery speed", g_rec,   s_rec,   1, 8, 0, 0},
    {"TX power",       g_tx,    s_tx,    1, 10, 0, 0},
    {"Ferrous limit",  g_fe,    s_fe,    20, 95, 0, 0},
    {"Ground balance", g_gb,    s_gb,    0, 2, GB_L, 0},
    {"Salt mode",      g_salt,  s_salt,  0, 1, OFFON, 0},
    {"Hybrid PI rate", g_hyb,   s_hyb,   0, 32, 0, 0},
    {"Tones",          g_tone,  s_tone,  0, 3, TONE_L, 0},
    {"Volume",         g_vol,   s_vol,   0, 10, 0, 0},
    {"Threshold hum",  g_thr,   s_thr,   0, 10, 0, 0},
    {"Iron volume",    g_iron,  s_iron,  0, 10, 0, 0},
    {"Iron grunt",     g_grunt, s_grunt, 0, 1, OFFON, 0},
    {"Noise cancel at start", g_anc, s_anc, 0, 1, OFFON, 0},
    {"Noise cancel",   0, 0, 0, 0, 0, ACT_NC},
    {"Coil wizard",    0, 0, 0, 0, 0, ACT_COIL},
    {"Learn target",   0, 0, 0, 0, 0, ACT_LEARN},
    {"Find log",       0, 0, 0, 0, 0, ACT_LOG},
    {"Diagnostics",    0, 0, 0, 0, 0, ACT_DIAG},
    {"Front light",    g_fl,    s_fl,    0, 100, 0, 0},
    {"GPS logging",    g_log,   s_log,   0, 1, OFFON, 0},
    {"Depth units",    g_units, s_units, 0, 1, UNITS_L, 0},
    {"Phone PIN",      g_pin,   s_pin,   0, 9999, 0, 0},
    {"Save settings",  0, 0, 0, 0, 0, ACT_SAVE},
};
#define NMENU ((int)(sizeof(MENU) / sizeof(MENU[0])))

void ui_init(ui_t *u) { memset(u, 0, sizeof(*u)); u->screen = SCR_HOME; u->dirty = 1; }

/* ------------------------------------------------------------------ drawing helpers */
static void status_bar(fb_t *fb, const detector_t *d, uint32_t now)
{
    char b[48]; const status_t *st = &d->st;
    gfx_fill(fb, 0, 0, LCD_W, 14, 1);
    snprintf(b, sizeof b, "%s  %s", PROG_L[d->set.program], MODE_L[d->set.mode]);
    gfx_text(fb, 3, 2, &FONT_SMALL, b, 0);
    /* noise level, GPS, SD, battery */
    int x = LCD_W - 3;
    snprintf(b, sizeof b, "%d%%", st->batt_pct); x -= gfx_text_width(&FONT_SMALL, b) + 2; gfx_text(fb, x, 2, &FONT_SMALL, b, 0);
    x -= 20; gfx_rect(fb, x, 3, 16, 8, 0); gfx_fill(fb, x + 1, 4, (14 * st->batt_pct) / 100, 6, 0); gfx_vline(fb, x + 16, 5, 8, 0);
    if (st->charging) gfx_text(fb, x - 8, 2, &FONT_SMALL, "+", 0);
    x -= 28; gfx_text(fb, x, 2, &FONT_SMALL, st->gnss.valid ? "GPS" : "gps", 0); if (!st->gnss.valid && (now / 500) & 1) gfx_hline(fb, x, x + 17, 12, 0);
    x -= 24; gfx_text(fb, x, 2, &FONT_SMALL, hal_sd_present() ? "SD" : "--", 0);
    x -= 44; snprintf(b, sizeof b, "N%d", st->noise_M); gfx_text(fb, x, 2, &FONT_SMALL, b, 0);
    x -= 30; if (d->set.sens_auto) snprintf(b, sizeof b, "SAUT"); else snprintf(b, sizeof b, "S%02d", d->set.sensitivity); gfx_text(fb, x, 2, &FONT_SMALL, b, 0);
    x -= 34; snprintf(b, sizeof b, "%d:%02dh", (int)st->batt_hours, (int)(60 * (st->batt_hours - (int)st->batt_hours))); gfx_text(fb, x, 2, &FONT_SMALL, b, 0);
}

static void spectrum_bars(fb_t *fb, const status_t *st, int x, int y, int w, int h)
{
    /* five bars = |T| at 5/10/15/20/41 kHz normalised: the target's frequency signature */
    static const char *lab[MDX_NLINES] = {"5", "10", "15", "20", "41"};
    int bw = (w - 4 * 3) / MDX_NLINES;
    for (int k = 0; k < MDX_NLINES; k++) {
        int bx = x + k * (bw + 3); int bh = st->target_present ? (int)((float)(h - 10) * st->spectrum[k]) : 0;
        gfx_rect(fb, bx, y, bw, h - 10, 1);
        if (bh > 0) gfx_fill(fb, bx + 1, y + (h - 10) - bh, bw - 2, bh, 1);
        gfx_text_center(fb, bx + bw / 2, y + h - 9, &FONT_SMALL, lab[k], 1);
    }
}

static void id_map(fb_t *fb, const status_t *st, int x, int y, int w, int h)
{
    /* 2-D target map: horizontal = conductivity 0..99, vertical = ferrous index 0 (top) .. 100 (bottom) */
    gfx_rect(fb, x, y, w, h, 1);
    for (int i = 1; i < 4; i++) { int gx = x + (w * i) / 4; for (int yy = y + 2; yy < y + h - 2; yy += 4) gfx_pixel(fb, gx, yy, 1); }
    int fe_y = y + (h * 55) / 100; for (int xx = x + 2; xx < x + w - 2; xx += 3) gfx_pixel(fb, xx, fe_y, 1);
    gfx_text(fb, x + 2, y + h - 11, &FONT_SMALL, "Fe", 1);
    gfx_text(fb, x + 2, y + 2, &FONT_SMALL, "0", 1); gfx_text_right(fb, x + w - 2, y + 2, &FONT_SMALL, "99", 1);
    for (int i = 0; i < st->trace_n; i++) {
        int idx = (st->trace_head - 1 - i + 48) % 24;
        int px = x + 2 + (int)((float)(w - 5) * st->trace_cond[idx] / 99.0f);
        int py = y + 2 + (int)((float)(h - 5) * st->trace_fe[idx] / 100.0f);
        if (i == 0) { gfx_disc(fb, px, py, 4, 1); gfx_circle(fb, px, py, 6, 1); }
        else if (i < 8) gfx_disc(fb, px, py, 2, 1);
        else gfx_pixel(fb, px, py, 1);
    }
}

static void depth_gauge(fb_t *fb, const detector_t *d, int x, int y, int w, int h)
{
    const status_t *st = &d->st; char b[16];
    gfx_text(fb, x, y, &FONT_SMALL, "DEPTH", 1);
    gfx_rect(fb, x, y + 12, w, h - 12, 1);
    float dcm = st->target_present ? st->est.depth_cm : 0;
    int fill = (int)((float)(h - 14) * (dcm / 40.0f)); if (fill > h - 14) fill = h - 14;
    if (st->target_present) gfx_fill(fb, x + 1, y + 13, w - 2, fill, 1);
    for (int i = 1; i < 4; i++) { int gy = y + 12 + ((h - 12) * i) / 4; gfx_hline(fb, x - 3, x - 1, gy, 1); }
    if (st->target_present) { if (d->set.units_in) snprintf(b, sizeof b, "%.0f\"", dcm / 2.54f); else snprintf(b, sizeof b, "%.0fcm", dcm); gfx_text(fb, x, y + h + 2, &FONT_SMALL, b, 1); }
}

static void draw_home(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; const status_t *st = &d->st; char b[48];
    gfx_clear(fb, 0); status_bar(fb, d, now);
    /* big ID */
    if (st->target_present) {
        snprintf(b, sizeof b, "%2d", (int)(st->est.cond_id + 0.5f));
        gfx_digits(fb, 150, 22, b, 1);
        if (!st->accepted) gfx_invert(fb, 148, 22, 60, 48);
        snprintf(b, sizeof b, "Fe %2d  conf %2d%%", (int)st->est.fe_index, (int)(100 * st->est.confidence));
        gfx_text_center(fb, 178, 74, &FONT_SMALL, b, 1);
        if (st->match_idx >= 0) { gfx_text_center(fb, 178, 86, &FONT_BOLD, d->cls.e[st->match_idx].name, 1); }
        else if (st->est.ferrous) gfx_text_center(fb, 178, 86, &FONT_MED, "IRON", 1);
    } else {
        gfx_digits(fb, 150, 22, "--", 1);
        gfx_text_center(fb, 178, 74, &FONT_SMALL, st->nc_active ? "noise cancel..." : "sweep", 1);
    }
    /* signal strength bar (dB above threshold) */
    float s01 = st->strength_db / 40.0f; if (s01 < 0) s01 = 0; if (s01 > 1) s01 = 1;
    gfx_bar(fb, 20, 112, 316, 8, st->target_present ? s01 : 0, 1);
    gfx_text(fb, 20, 100, &FONT_SMALL, "SIGNAL", 1);
    /* target map */
    id_map(fb, st, 20, 126, 200, 108);
    /* depth */
    depth_gauge(fb, d, 236, 126, 22, 90);
    /* ground and PI panel */
    int gx = 270, gy = 126;
    gfx_text(fb, gx, gy, &FONT_SMALL, "GROUND", 1);
    snprintf(b, sizeof b, "%+.0f deg", st->ground_phase_deg); gfx_text(fb, gx, gy + 11, &FONT_MED, b, 1);
    snprintf(b, sizeof b, "mineral"); gfx_text(fb, gx, gy + 30, &FONT_SMALL, b, 1);
    float m01 = st->ground_strength / 0.02f; gfx_bar(fb, gx + 44, gy + 30, 84, 8, m01, 1);
    snprintf(b, sizeof b, "salt"); gfx_text(fb, gx, gy + 42, &FONT_SMALL, b, 1); gfx_bar(fb, gx + 44, gy + 42, 84, 8, st->salt_ratio, 1);
    snprintf(b, sizeof b, "hot rock"); gfx_text(fb, gx, gy + 54, &FONT_SMALL, b, 1); gfx_bar(fb, gx + 44, gy + 54, 84, 8, st->hotrock * 4.0f, 1);
    snprintf(b, sizeof b, "noise"); gfx_text(fb, gx, gy + 66, &FONT_SMALL, b, 1); gfx_bar(fb, gx + 44, gy + 66, 84, 8, st->noise_level / 200e-6f, 1);
    if (d->set.mode == MODE_HYBRID || d->set.mode == MODE_PI) {
        snprintf(b, sizeof b, "PI %s %.0fcm", st->pi_target ? "TARGET" : "quiet", st->pi_target ? st->pi_depth_cm : 0.0f);
        gfx_text(fb, gx, gy + 80, &FONT_SMALL, b, 1);
    }
    if (d->set.gb_mode == 1) gfx_text(fb, gx, gy + 92, &FONT_SMALL, "GB manual", 1);
    spectrum_bars(fb, st, 270, 22, 110, 50);
    gfx_text(fb, 270, 74, &FONT_SMALL, "kHz signature", 1);
    if (st->low_batt) { gfx_fill(fb, 100, 97, 170, 13, 1); gfx_text_center(fb, 185, 99, &FONT_SMALL, "LOW BATTERY - charge soon", 0); }
    /* left column: coil / tx */
    snprintf(b, sizeof b, "%s", d->coil.cal.name); gfx_text(fb, 20, 22, &FONT_SMALL, b, 1);
    snprintf(b, sizeof b, "TX %.0fV %.2fA", st->vtx_v, st->itx_rms_a); gfx_text(fb, 20, 34, &FONT_SMALL, b, 1);
    if (st->tx_fault) gfx_text(fb, 20, 46, &FONT_BOLD, "TX FAULT", 1);
    else if (!st->coil_present) gfx_text(fb, 20, 46, &FONT_BOLD, "NO COIL", 1);
    else if (st->clipped) gfx_text(fb, 20, 46, &FONT_BOLD, "OVERLOAD", 1);
    if (st->pinpoint) { gfx_fill(fb, 240, 22, 140, 20, 1); gfx_text_center(fb, 310, 25, &FONT_MED, "PINPOINT", 0); }
    if (st->gnss.valid) { snprintf(b, sizeof b, "%.5f %.5f", st->gnss.lat, st->gnss.lon); gfx_text_right(fb, LCD_W - 20, 86, &FONT_SMALL, b, 1); }
    snprintf(b, sizeof b, "finds %lu", (unsigned long)st->finds); gfx_text_right(fb, LCD_W - 20, 98, &FONT_SMALL, b, 1);
}

static void draw_log(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; char b[64]; const status_t *st = &d->st;
    gfx_clear(fb, 0); status_bar(fb, d, now);
    gfx_text(fb, 16, 20, &FONT_BOLD, "FIND LOG", 1);
    snprintf(b, sizeof b, "Finds this session: %lu", (unsigned long)st->finds); gfx_text(fb, 16, 46, &FONT_MED, b, 1);
    snprintf(b, sizeof b, "GPS: %s  sats %d  hdop %.1f", st->gnss.valid ? "fix" : "no fix", st->gnss.sats, st->gnss.hdop); gfx_text(fb, 16, 64, &FONT_MED, b, 1);
    snprintf(b, sizeof b, "Position: %.5f, %.5f", st->gnss.lat, st->gnss.lon); gfx_text(fb, 16, 82, &FONT_MED, b, 1);
    snprintf(b, sizeof b, "SD card: %s   logging %s", hal_sd_present() ? "present" : "missing", d->set.log_enable ? "on" : "off"); gfx_text(fb, 16, 100, &FONT_MED, b, 1);
    gfx_text(fb, 16, 130, &FONT_SMALL, "Files on the card: MDX1/finds.csv (one line per find)", 1);
    gfx_text(fb, 16, 142, &FONT_SMALL, "and MDX1/trail.csv (position + ground every 2 s).", 1);
    gfx_text(fb, 16, 154, &FONT_SMALL, "A target held for 1.5 s is logged automatically.", 1);
    if (st->target_present) { snprintf(b, sizeof b, "Current: ID %d  Fe %d  %.0f cm", (int)st->est.cond_id, (int)st->est.fe_index, st->est.depth_cm); gfx_text(fb, 16, 170, &FONT_BOLD, b, 1); }
    gfx_text(fb, 16, 226, &FONT_SMALL, "RIGHT = mark find here   BACK = exit", 1);
}

static void draw_menu(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; char b[48];
    gfx_clear(fb, 0); status_bar(fb, d, now);
    gfx_text(fb, 16, 20, &FONT_BOLD, "MENU", 1);
    int rows = 10, y0 = 44, rh = 18;
    if (u->menu_idx < u->menu_top) u->menu_top = u->menu_idx;
    if (u->menu_idx >= u->menu_top + rows) u->menu_top = u->menu_idx - rows + 1;
    for (int r = 0; r < rows && u->menu_top + r < NMENU; r++) {
        int i = u->menu_top + r; const item_t *it = &MENU[i]; int y = y0 + r * rh;
        int sel = i == u->menu_idx;
        if (sel) gfx_fill(fb, 12, y - 2, LCD_W - 24, rh, 1);
        gfx_text(fb, 18, y, &FONT_MED, it->name, !sel);
        if (it->get) {
            int v = it->get(d);
            if (it->labels) snprintf(b, sizeof b, "%s", it->labels[v]); else snprintf(b, sizeof b, "%d", v);
            if (sel && u->editing) { char c[52]; snprintf(c, sizeof c, "< %s >", b); gfx_text_right(fb, LCD_W - 20, y, &FONT_MED, c, 0); }
            else gfx_text_right(fb, LCD_W - 20, y, &FONT_MED, b, !sel);
            if (!it->labels && sel) { float f = (float)(v - it->lo) / (float)(it->hi - it->lo); gfx_bar(fb, 200, y + 5, 80, 7, f, 0); }
        } else gfx_text_right(fb, LCD_W - 20, y, &FONT_MED, ">", !sel);
    }
    gfx_text(fb, 16, 226, &FONT_SMALL, u->editing ? "LEFT/RIGHT change   BACK done" : "UP/DOWN select   RIGHT edit   BACK exit", 1);
}

static void draw_pinpoint(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; const status_t *st = &d->st; char b[32];
    gfx_clear(fb, 0); status_bar(fb, d, now);
    gfx_text_center(fb, LCD_W / 2, 24, &FONT_BOLD, u->gb_active ? "GROUND GRAB" : "PINPOINT", 1);
    float lvl = st->pinpoint_level; if (lvl > 1) lvl = 1; if (lvl < 0) lvl = 0;
    int r = (int)(90.0f * lvl);
    gfx_circle(fb, LCD_W / 2, 140, 90, 1); if (r > 2) gfx_disc(fb, LCD_W / 2, 140, r, 1);
    gfx_bar(fb, 40, 226, 320, 10, lvl, 1);
    if (u->gb_active) gfx_text_center(fb, LCD_W / 2, 48, &FONT_SMALL, "pump the coil over clean ground, release when done", 1);
    else if (st->target_present) { snprintf(b, sizeof b, "ID %d  %.0f cm", (int)st->est.cond_id, st->est.depth_cm); gfx_text_center(fb, LCD_W / 2, 48, &FONT_MED, b, 1); }
    if (!u->gb_active) gfx_text(fb, 16, 226 - 14, &FONT_SMALL, "hold 1.5 s + pump = ground grab", 1);
}

static void draw_wizard(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; char b[64];
    gfx_clear(fb, 0); status_bar(fb, d, now);
    gfx_text(fb, 16, 20, &FONT_BOLD, "COIL WIZARD", 1);
    static const char *steps[] = {
        "1  Hold the coil in the air, away from metal.", "   Measuring TX winding L and R...",
        "2  Recording the residual null (air).", "3  Hold a FERRITE rod at the coil centre.", "   Recording magnetic axis...",
        "4  Hold a COIN 10 cm under the coil.", "   Recording depth reference...", "Done. Saved to coil chip / memory." };
    int s = u->wizard_step; if (s > 7) s = 7;
    for (int i = 0; i <= s && i < 8; i++) gfx_text(fb, 16, 44 + 16 * i, &FONT_MED, steps[i], 1);
    if (s >= 1) { snprintf(b, sizeof b, "L = %.3f mH   R = %.2f ohm", u->wiz_L * 1e3f, u->wiz_R); gfx_text(fb, 30, 190, &FONT_MED, b, 1); }
    if (s >= 2) { snprintf(b, sizeof b, "null 41k = %.1f uohm  (%.1f mV)", cpx_abs(u->wiz_air[4]) * 1e6f, cpx_abs(u->wiz_air[4]) * 0.063f * 1e3f); gfx_text(fb, 30, 206, &FONT_MED, b, 1); }
    gfx_text(fb, 16, 226, &FONT_SMALL, s >= 7 ? "BACK to exit" : "RIGHT = next step   BACK = cancel", 1);
}

static void draw_nc(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; char b[48];
    gfx_clear(fb, 0); status_bar(fb, d, now);
    gfx_text(fb, 16, 20, &FONT_BOLD, "NOISE CANCEL", 1);
    for (int i = 0; i < NC_NCAND; i++) {
        int M = MDX_M_MIN + i; float sc = d->nc.score[i];
        float h01 = sc > 0 ? logf(1 + sc * 1e9f) / 20.0f : 0; if (h01 > 1) h01 = 1;
        int x = 30 + i * 38, hh = (int)(120 * h01);
        gfx_fill(fb, x, 180 - hh, 26, hh, 1); if (M == d->nc.best_M) gfx_rect(fb, x - 2, 58, 30, 124, 1);
        snprintf(b, sizeof b, "%d", M); gfx_text_center(fb, x + 13, 184, &FONT_SMALL, b, 1);
        snprintf(b, sizeof b, "%.2f", MDX_FS_HZ / (float)M / 1000.0f); gfx_text_center(fb, x + 13, 196, &FONT_SMALL, b, 1);
    }
    gfx_text(fb, 30, 44, &FONT_MED, d->nc.active ? "scanning..." : "done - quietest channel selected", 1);
    gfx_text(fb, 16, 226, &FONT_SMALL, "RIGHT = rescan   BACK = exit", 1);
}

static void draw_diag(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; char b[64]; const status_t *st = &d->st;
    gfx_clear(fb, 0); status_bar(fb, d, now);
    gfx_text(fb, 16, 18, &FONT_BOLD, "DIAGNOSTICS", 1);
    int y = 40;
    snprintf(b, sizeof b, "FW %s   uptime %lus   %.1f C", MDX_FW_VERSION, (unsigned long)st->uptime_s, st->temp_c); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "Rails VSYS %.2f  3V3 %.2f  5VA %.2f  -5VA %.2f  12V %.1f", hal_rail_v(0), hal_rail_v(1), hal_rail_v(2), hal_rail_v(3), hal_rail_v(4)); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "VTX %.1f V   VBRIDGE %.1f V   ITX %.3f A rms   fault %d", st->vtx_v, st->vbridge_v, st->itx_rms_a, st->tx_fault); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "Coil %s  chip %d  L %.3f mH  R %.2f  cal %d", d->coil.cal.name, d->coil.has_chip, d->coil.cal.L_tx_mH, d->coil.cal.R_tx_ohm, d->coil.calibrated); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "Period M %d  lines %.2f %.2f %.2f %.2f %.2f kHz", d->demod.M, d->demod.f_hz[0] / 1e3f, d->demod.f_hz[1] / 1e3f, d->demod.f_hz[2] / 1e3f, d->demod.f_hz[3] / 1e3f, d->demod.f_hz[4] / 1e3f); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "Floor %.1f uohm  thresh %.1f uohm  noise %.1f uohm", d->noise_floor * 1e6f, d->thresh * 1e6f, st->noise_level * 1e6f); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "Ground a %.2f m  b %.2f m  phase %.1f  salt %.2f  misfit %.3f", d->ground.a * 1e3f, d->ground.b * 1e3f, st->ground_phase_deg, st->salt_ratio, st->hotrock); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "Battery %.2f V %d%%  %s   periods %lu  PI %lu", st->batt_v, st->batt_pct, st->charging ? "charging" : "", (unsigned long)st->periods, (unsigned long)st->pi_records); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "GNSS %s sats %d  %.5f %.5f  hdop %.1f", st->gnss.valid ? "fix" : "no fix", st->gnss.sats, st->gnss.lat, st->gnss.lon, st->gnss.hdop); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 12;
    snprintf(b, sizeof b, "IMU swing %.2f m/s   SD %s   headphones %s", st->swing_speed, hal_sd_present() ? "ok" : "none", hal_headphone_present() ? "in" : "out"); gfx_text(fb, 16, y, &FONT_SMALL, b, 1); y += 16;
    gfx_text(fb, 16, y, &FONT_SMALL, "Per-line null (uohm, re/im):", 1); y += 11;
    for (int k = 0; k < MDX_NLINES; k++) { snprintf(b, sizeof b, "%5.1fk %+8.1f %+8.1f", d->demod.f_hz[k] / 1e3f, d->coil.cal.null[k].re * 1e6f, d->coil.cal.null[k].im * 1e6f); gfx_text(fb, 24 + (k % 2) * 190, y + (k / 2) * 11, &FONT_SMALL, b, 1); }
    gfx_text(fb, 16, 226, &FONT_SMALL, "BACK = exit", 1);
}

static void draw_learn(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; char b[48];
    gfx_clear(fb, 0); status_bar(fb, d, now);
    gfx_text(fb, 16, 20, &FONT_BOLD, "LEARN TARGET", 1);
    gfx_text(fb, 16, 44, &FONT_MED, "Sweep the target, press RIGHT while", 1);
    gfx_text(fb, 16, 60, &FONT_MED, "the ID is shown to store it.", 1);
    if (d->st.target_present) { snprintf(b, sizeof b, "ID %d  Fe %d  conf %d%%", (int)d->st.est.cond_id, (int)d->st.est.fe_index, (int)(100 * d->st.est.confidence)); gfx_text(fb, 16, 84, &FONT_BOLD, b, 1); }
    int y = 112; gfx_text(fb, 16, y, &FONT_SMALL, "Stored:", 1); y += 12;
    for (int i = 0; i < CLS_MAX; i++) if (d->cls.e[i].used) { snprintf(b, sizeof b, "%-12s ID %2.0f Fe %2.0f", d->cls.e[i].name, d->cls.e[i].cond_id, d->cls.e[i].fe_index); gfx_text(fb, 24, y, &FONT_SMALL, b, 1); y += 11; }
    gfx_text(fb, 16, 226, &FONT_SMALL, "RIGHT = store   BACK = exit", 1);
}

static void draw_charge(ui_t *u, const detector_t *d, uint32_t now)
{
    fb_t *fb = &u->fb; char b[32];
    gfx_clear(fb, 0);
    gfx_rect(fb, 150, 90, 100, 50, 1); gfx_fill(fb, 250, 105, 8, 20, 1);
    gfx_fill(fb, 153, 93, (94 * d->st.batt_pct) / 100, 44, 1);
    snprintf(b, sizeof b, "%d%%  %.2f V", d->st.batt_pct, d->st.batt_v); gfx_text_center(fb, 200, 150, &FONT_MED, b, 1);
    gfx_text_center(fb, 200, 170, &FONT_SMALL, (now / 700) & 1 ? "charging" : "", 1);
}

void ui_draw(ui_t *u, const detector_t *d, uint32_t now)
{
    switch (u->screen) {
    case SCR_MENU: draw_menu(u, d, now); break;
    case SCR_PINPOINT: draw_pinpoint(u, d, now); break;
    case SCR_COIL_WIZARD: draw_wizard(u, d, now); break;
    case SCR_NOISE_CANCEL: draw_nc(u, d, now); break;
    case SCR_DIAG: draw_diag(u, d, now); break;
    case SCR_LEARN: draw_learn(u, d, now); break;
    case SCR_LOG: draw_log(u, d, now); break;
    case SCR_CHARGE: draw_charge(u, d, now); break;
    case SCR_OFF_CONFIRM: gfx_clear(&u->fb, 0); gfx_text_center(&u->fb, 200, 110, &FONT_BOLD, "Hold POWER to switch off", 1); break;
    default: draw_home(u, d, now); break;
    }
}

/* ------------------------------------------------------------------ input handling */
static void menu_action(ui_t *u, detector_t *d, int act)
{
    switch (act) {
    case ACT_NC: noisecancel_start(&d->nc); u->screen = SCR_NOISE_CANCEL; break;
    case ACT_COIL: u->screen = SCR_COIL_WIZARD; u->wizard_step = 0; break;
    case ACT_DIAG: u->screen = SCR_DIAG; break;
    case ACT_LEARN: u->screen = SCR_LEARN; break;
    case ACT_LOG: u->screen = SCR_LOG; break;
    case ACT_SAVE: d->set.crc = settings_crc(&d->set); hal_nvm_write(&d->set, sizeof(d->set)); break;
    }
}

static void wizard_step(ui_t *u, detector_t *d);
void ui_wizard_advance(ui_t *u, detector_t *d) { wizard_step(u, d); }

/* ---- item table access for the phone link */
int ui_item_count(void) { return NMENU; }
int ui_item_info(int idx, ui_item_info_t *o)
{
    if (idx < 0 || idx >= NMENU) return 0;
    const item_t *it = &MENU[idx];
    o->name = it->name; o->lo = it->lo; o->hi = it->hi; o->labels = it->labels; o->is_action = it->get == 0;
    o->nlabels = it->labels ? it->hi - it->lo + 1 : 0;
    return 1;
}
int ui_item_get(const detector_t *d, int idx) { if (idx < 0 || idx >= NMENU || !MENU[idx].get) return 0; return MENU[idx].get(d); }
int ui_item_set(ui_t *u, detector_t *d, int idx, int v)
{
    if (idx < 0 || idx >= NMENU) return 1;
    const item_t *it = &MENU[idx];
    if (!it->set) { menu_action(u, d, it->action); return 0; }
    if (v < it->lo || v > it->hi) return 2;
    it->set(d, v); detector_apply_settings(d); d->settings_dirty = 1; return 0;
}

static void wizard_step(ui_t *u, detector_t *d)
{
    /* each RIGHT press advances; measurements use the detector's current slow average */
    switch (u->wizard_step) {
    case 0: {   /* measure L/R from the last raw frame is done in ui_update via wiz_acc; here we finalise */
        u->wizard_step = 1; u->wizard_t0 = hal_millis(); break; }
    case 1: u->wizard_step = 2; for (int k = 0; k < MDX_NLINES; k++) u->wiz_air[k] = d->S_slow[k]; u->wizard_step = 3; break;
    case 3: u->wizard_step = 4; u->wiz_n = 0; break;
    case 4: { cpx raw_air[MDX_NLINES]; cpx dummy; (void)dummy;
        /* S_slow is coil-corrected; undo to raw for the calibration record: raw = S/rot + null */
        for (int k = 0; k < MDX_NLINES; k++) raw_air[k] = cpx_add(cpx_div(u->wiz_air[k], d->coil.cal.rot[k]), d->coil.cal.null[k]);
        cpx raw_fer[MDX_NLINES]; for (int k = 0; k < MDX_NLINES; k++) raw_fer[k] = cpx_add(cpx_div(d->S_slow[k], d->coil.cal.rot[k]), d->coil.cal.null[k]);
        coil_set_null(&d->coil.cal, raw_air); coil_set_rotation(&d->coil.cal, raw_fer, raw_air);
        u->wizard_step = 5; break; }
    case 5: u->wizard_step = 6; break;
    case 6: { float s = sqrtf(cpx_norm2(d->T_fast, MDX_NLINES)); if (s > 0) coil_set_depth_ref(&d->coil.cal, s); d->coil.calibrated = 1; coil_store(&d->coil); detector_apply_settings(d); u->wizard_step = 7; break; }
    default: u->screen = SCR_HOME; break;
    }
}

void ui_wizard_frame(ui_t *u, detector_t *d, const adc_frame_t *f)
{
    if (u->screen != SCR_COIL_WIZARD || u->wizard_step != 1) return;
    float L, R;
    if (coil_measure_LR(f->itx, f->M, &d->txtab, hal_tx_rail_v(), &L, &R)) {
        if (u->wiz_L <= 0) { u->wiz_L = L; u->wiz_R = R; } else { u->wiz_L += 0.1f * (L - u->wiz_L); u->wiz_R += 0.1f * (R - u->wiz_R); }
        d->coil.cal.L_tx_mH = u->wiz_L * 1e3f; d->coil.cal.R_tx_ohm = u->wiz_R;
    }
}

void ui_update(ui_t *u, detector_t *d, uint32_t keys, uint32_t now)
{
    uint32_t raw = keys;
    /* MENU: short press (release before 0.8 s) = MENU, held 0.8 s = BACK (fires once while held) */
    keys &= ~KEY_MENU;                                            /* (virtual BACK/GB may be injected by the test bench) */
    if (raw & KEY_MENU) {
        if (!u->menu_down) { u->menu_down = 1; u->menu_t0 = now; u->menu_long = 0; }
        else if (!u->menu_long && now - u->menu_t0 >= 800) { u->menu_long = 1; keys |= KEY_BACK; }
    } else if (u->menu_down) { if (!u->menu_long) keys |= KEY_MENU; u->menu_down = 0; }
    /* PINPOINT held 1.5 s = ground grab, repeated every 250 ms while held (pump the coil) */
    if (raw & KEY_PINPOINT) {
        if (!u->pp_down) { u->pp_down = 1; u->pp_t0 = now; u->gb_active = 0; }
        else if (now - u->pp_t0 >= 1500) { u->gb_active = 1; if (now - u->gb_t >= 250) { u->gb_t = now; keys |= KEY_GB; } }
    } else { u->pp_down = 0; u->gb_active = 0; }
    uint32_t pressed = keys & ~u->last_keys;
    /* auto-repeat for the navigation keys: after 500 ms held, repeat at 8 Hz */
    uint32_t nav = keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    if (nav && nav == (u->last_keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))) {
        if (u->hold_t0 == 0) u->hold_t0 = now;
        else if (now - u->hold_t0 > 500 && now - u->repeat_t >= 125) { pressed |= nav; u->repeat_t = now; }
    } else { u->hold_t0 = nav ? now : 0; u->repeat_t = now; }
    u->last_keys = keys;
    if (d->st.charging && d->st.batt_pct < 100 && u->screen == SCR_HOME && !d->st.coil_present) { /* charging with no coil: charge screen */ }
    /* global keys */
    if (pressed & KEY_PINPOINT) { if (u->screen != SCR_PINPOINT) { u->screen = SCR_PINPOINT; detector_set_pinpoint(d, 1); } else { detector_set_pinpoint(d, 1); } }
    if (u->screen == SCR_PINPOINT && !(keys & KEY_PINPOINT) && (u->last_keys & KEY_PINPOINT) == 0) { /* released: leave */ }
    if (keys & KEY_GB) { detector_ground_grab(d); }
    if (keys & KEY_POWER) { if (u->key_t0 == 0) u->key_t0 = now; else if (now - u->key_t0 > 1500) hal_power_off(); } else u->key_t0 = 0;
    switch (u->screen) {
    case SCR_HOME:
        if (pressed & KEY_MENU) { u->screen = SCR_MENU; u->editing = 0; }
        if (pressed & KEY_UP) { if (d->set.sensitivity < 30) d->set.sensitivity++; detector_apply_settings(d); }
        if (pressed & KEY_DOWN) { if (d->set.sensitivity > 1) d->set.sensitivity--; detector_apply_settings(d); }
        if (pressed & KEY_LEFT) { d->set.program = (uint8_t)((d->set.program + PROG_COUNT - 1) % PROG_COUNT); detector_load_program(d, d->set.program); }
        if (pressed & KEY_RIGHT) { d->set.program = (uint8_t)((d->set.program + 1) % PROG_COUNT); detector_load_program(d, d->set.program); }
        break;
    case SCR_PINPOINT:
        if (!(keys & KEY_PINPOINT)) { u->screen = SCR_HOME; detector_set_pinpoint(d, 0); }
        break;
    case SCR_MENU: {
        const item_t *it = &MENU[u->menu_idx];
        if (u->editing) {
            if (pressed & (KEY_LEFT | KEY_RIGHT)) { int v = it->get(d) + ((pressed & KEY_RIGHT) ? 1 : -1); if (v < it->lo) v = it->hi; if (v > it->hi) v = it->lo; it->set(d, v); detector_apply_settings(d); }
            if (pressed & (KEY_BACK | KEY_MENU)) u->editing = 0;
        } else {
            if (pressed & KEY_UP) u->menu_idx = (u->menu_idx + NMENU - 1) % NMENU;
            if (pressed & KEY_DOWN) u->menu_idx = (u->menu_idx + 1) % NMENU;
            if (pressed & KEY_RIGHT) { if (it->get) u->editing = 1; else menu_action(u, d, it->action); }
            if (pressed & (KEY_BACK | KEY_MENU)) { u->screen = SCR_HOME; if (d->settings_dirty) detector_save_settings(d); }
        }
        break; }
    case SCR_LOG:
        if (pressed & KEY_RIGHT) u->mark_find = 1;
        if (pressed & KEY_BACK) u->screen = SCR_MENU;
        break;
    case SCR_COIL_WIZARD:
        if (pressed & KEY_RIGHT) wizard_step(u, d);
        if (pressed & KEY_BACK) u->screen = SCR_MENU;
        if (u->wizard_step == 1 && now - u->wizard_t0 > 300) { /* L/R measured by ui_wizard_frame(); advance automatically */ if (u->wiz_L > 0) u->wizard_step = 2; }
        break;
    case SCR_NOISE_CANCEL:
        if (pressed & KEY_RIGHT) noisecancel_start(&d->nc);
        if (pressed & KEY_BACK) u->screen = SCR_MENU;
        break;
    case SCR_LEARN:
        if ((pressed & KEY_RIGHT) && d->st.target_present) { char name[CLS_NAME]; snprintf(name, sizeof name, "T%d-%02d", ++u->learn_idx, (int)d->st.est.cond_id); detector_learn_target(d, name); }
        if (pressed & KEY_BACK) u->screen = SCR_MENU;
        break;
    default:
        if (pressed & (KEY_BACK | KEY_MENU)) u->screen = SCR_MENU;
        break;
    }
    u->dirty = 1;
}
