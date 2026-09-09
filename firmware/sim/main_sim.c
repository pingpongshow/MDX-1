/* Host test-bench: runs the detector core against the physics simulator and checks results.
 * Also renders the UI screens to PBM files for the documentation. */
#include "sim.h"
#include "../core/det/detector.h"
#include "../core/ui/screens.h"
#include "../core/log/log.h"
#include "../core/tx/waveform.h"
#include "../core/link/link.h"
#include "../core/sys/sys.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

const uint8_t *sim_last_fb(void);
void sim_audio_pull(int16_t *buf, int n);
static int fails = 0, checks = 0;
static coil_cal_t g_cal;      /* calibration produced by the wizard test, reused by later tests */
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } else { printf("  ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static void save_pbm(const char *name, const uint8_t *fb)
{
    char p[128]; snprintf(p, sizeof p, "build/%s.pbm", name);
    FILE *f = fopen(p, "wb"); if (!f) return;
    fprintf(f, "P4\n%d %d\n", LCD_W, LCD_H); fwrite(fb, 1, LCD_H * LCD_STRIDE, f); fclose(f);
}

/* run n periods through the detector, returns count of periods with target_present */
static int run_periods(detector_t *d, int n, ui_t *u)
{
    adc_frame_t f; imu_sample_t imu; int present = 0;
    for (int i = 0; i < n; i++) {
        hal_adc_get_frame(&f); hal_imu_read(&imu);
        if (u) ui_wizard_frame(u, d, &f);
        detector_period(d, &f, &imu);
        pi_record_t r; while (hal_adc_get_pi_record(&r)) detector_pi_record(d, &r);
        present += d->st.target_present;
    }
    return present;
}

/* ------------------------------------------------------------------ 1. waveform */
static void test_waveform(void)
{
    printf("[1] transmit waveform table\n");
    float dc = 0; for (int i = 0; i < MDX_SLOTS; i++) dc += MDX_TX_SLOTS[i];
    CHECK(fabsf(dc) < 0.5f, "zero DC (sum=%g)", dc);
    for (int l = 0; l < MDX_NLINES; l++) {
        int k = MDX_LINE_BIN[l]; float re = 0, im = 0;
        for (int i = 0; i < MDX_SLOTS; i++) { float th = 2 * (float)M_PI * k * i / MDX_SLOTS; re += MDX_TX_SLOTS[i] * cosf(th); im -= MDX_TX_SLOTS[i] * sinf(th); }
        float mag = sqrtf(re * re + im * im) / MDX_SLOTS;
        CHECK(fabsf(mag - waveform_line_rel_amplitude(l)) < 0.003f, "line %d rel amplitude %.4f (expected %.4f)", k, mag, waveform_line_rel_amplitude(l));
    }
    for (int M = MDX_M_MIN; M <= MDX_M_MAX; M++) {
        tx_table_t t; waveform_build(M, &t); uint32_t sum = 0; for (int i = 0; i < MDX_SLOTS; i++) sum += t.ticks[i];
        if (sum != (uint32_t)M * MDX_TICKS_PER_SAMPLE) { CHECK(0, "M=%d ticks sum %u", M, sum); }
    }
    CHECK(1, "slot tables for M=96..104 sum to M*448 ticks");
}

/* ------------------------------------------------------------------ 2. demod */
static void test_demod(void)
{
    printf("[2] coherent demodulation recovers the physical transfer impedance\n");
    sim_reset(); SIM.noise_v_rms = 0; SIM.g_mag = 5e-3f; SIM.g_salt = 1e-3f;
    demod_t dm; demod_init(&dm, MDX_M_DEFAULT); adc_frame_t f; demod_out_t o;
    for (int i = 0; i < 20; i++) hal_adc_get_frame(&f);   /* let the RL current settle */
    demod_period(&dm, f.rx, f.itx, &o);
    cpx S[MDX_NLINES]; sim_line_S(S, 0);
    float worst = 0;
    for (int k = 0; k < MDX_NLINES; k++) { float e = cpx_abs(cpx_sub(o.S[k], S[k])) / cpx_abs(S[k]); if (e > worst) worst = e; }
    CHECK(worst < 5e-3f, "worst relative error over 5 lines %.2e", worst);
    CHECK(o.itx_rms_a > 0.3f && o.itx_rms_a < 0.6f, "TX current %.3f A rms at 12 V (design 0.42)", o.itx_rms_a);
    coil_cal_t c; coil_default(&c);
    float ex = coil_expected_line_current(&c, 12.0f, dm.f_hz[0], waveform_line_rel_amplitude(0));
    CHECK(fabsf(cpx_abs(o.itx_a[0]) - ex) / ex < 0.03f, "5 kHz line current %.3f A vs expected %.3f A", cpx_abs(o.itx_a[0]), ex);
}

/* ------------------------------------------------------------------ 3. coil L/R */
static void test_coil_LR(void)
{
    printf("[3] coil wizard: TX inductance and resistance from the current waveform\n");
    sim_reset(); SIM.noise_v_rms = 0;
    adc_frame_t f; for (int i = 0; i < 20; i++) hal_adc_get_frame(&f);
    float L, R; int ok = coil_measure_LR(f.itx, f.M, &SIM.txtab, SIM.V_rail, &L, &R);
    CHECK(ok, "regression succeeded");
    CHECK(fabsf(L - SIM.L_tx_H) / SIM.L_tx_H < 0.03f, "L = %.4f mH (true %.4f)", L * 1e3f, SIM.L_tx_H * 1e3f);
    CHECK(fabsf(R - SIM.R_tx_ohm) / SIM.R_tx_ohm < 0.10f, "R = %.3f ohm (true %.3f)", R, SIM.R_tx_ohm);
    /* an off-the-shelf 5.6 mH / 25 ohm coil */
    SIM.L_tx_H = 5.6e-3f; SIM.R_tx_ohm = 25.0f; SIM.i_state = 0; for (int i = 0; i < 40; i++) hal_adc_get_frame(&f);
    ok = coil_measure_LR(f.itx, f.M, &SIM.txtab, SIM.V_rail, &L, &R);
    CHECK(ok && fabsf(L - 5.6e-3f) / 5.6e-3f < 0.05f, "OTS coil: L = %.3f mH (true 5.600)", L * 1e3f);
}

/* ------------------------------------------------------------------ 4. wizard rotation + null */
static void test_wizard(detector_t *d)
{
    printf("[4] coil wizard: null and magnetic-axis rotation\n");
    sim_reset(); SIM.noise_v_rms = 2e-6f;
    detector_init(d);
    run_periods(d, 8000, 0);
    cpx air[MDX_NLINES]; for (int k = 0; k < MDX_NLINES; k++) air[k] = d->S_slow[k];   /* raw (cal is identity) */
    coil_set_null(&d->coil.cal, air);
    sim_add_target(0, 0.05f, 1e9f, 0, 0.3e-3f);          /* ferrite rod: pure magnetic, ~13 mohm at the coil */
    run_periods(d, 8000, 0);
    cpx fer[MDX_NLINES]; for (int k = 0; k < MDX_NLINES; k++) fer[k] = cpx_add(cpx_div(d->S_slow[k], d->coil.cal.rot[k]), d->coil.cal.null[k]);
    /* note: S_slow already had the null subtracted by coil_apply since coil_set_null; undo above */
    coil_set_rotation(&d->coil.cal, fer, air);
    float worst = 0;
    for (int k = 0; k < MDX_NLINES; k++) {
        float found = -cpx_arg(d->coil.cal.rot[k]) * 180.0f / (float)M_PI;   /* rot = e^{+j phi} undoes e^{-j phi} */
        float err = fabsf(found - (-SIM.phys_rot_deg[k])); if (err > 180) err = 360 - err;
        if (err > worst) worst = err;
    }
    CHECK(worst < 2.0f, "rotation recovered on all lines, worst error %.2f deg", worst);
    CHECK(d->coil.cal.flags & 3, "calibration flags set (null + rotation)");
    coil_store(&d->coil); g_cal = d->coil.cal;
    coil_t c2; c2.has_chip = 1; int r = coil_load(&c2);
    CHECK(r == 1 && c2.cal.crc == d->coil.cal.crc, "record survives a round trip through the coil EEPROM (crc %04x)", c2.cal.crc);
}

static float expected_id(float zeta) { return target_cond_id_from_zeta(zeta); }

/* ------------------------------------------------------------------ 5. detection in mineralised ground */
static void test_detection(detector_t *d, ui_t *u)
{
    printf("[5] coin at 15 cm in mineralised ground, sweeping at 1.5 m/s\n");
    coil_cal_t cal = g_cal;
    sim_reset(); SIM.noise_v_rms = 20e-6f; SIM.g_mag = 30e-3f; SIM.g_salt = 3e-3f; SIM.sweeping = 1;
    detector_init(d); d->coil.cal = cal; d->coil.calibrated = 1;
    d->set.sensitivity = 24; d->set.recovery = 5; detector_apply_settings(d);
    run_periods(d, 8000, 0);                                  /* ~1.2 s of ground only: settle tracking */
    float ph = d->st.ground_phase_deg; int fp = run_periods(d, 4000, 0);
    CHECK(fp < 40, "no target: %d false triggers in 4000 periods (%.1f %%), ground phase %.1f deg, |G| %.1f mohm", fp, 100.0f * fp / 4000, ph, d->st.ground_strength * 1e3f);
    /* copper coin: zeta 2.5 kHz, eddy 2 mohm at 10 cm -> at 15 cm about 0.18 mohm */
    sim_add_target(0.0f, 0.15f, 2500.0f, 2.0e-3f, 0.0f);
    int hits = 0, accepted = 0; float idsum = 0, fesum = 0, depsum = 0; int n = 0;
    for (int i = 0; i < 40; i++) {                            /* 40 x 200 periods ~ 1.6 s = one full sweep */
        int p = run_periods(d, 200, 0);
        if (p) { hits += p; if (d->st.target_present && d->st.strength_db > 6) { idsum += d->st.est.cond_id; fesum += d->st.est.fe_index; depsum += d->st.est.depth_cm; n++; accepted += d->st.accepted; } }
    }
    float id = n ? idsum / n : -1, fe = n ? fesum / n : -1, dep = n ? depsum / n : -1;
    CHECK(hits > 100, "target detected (%d periods above threshold)", hits);
    CHECK(fabsf(id - expected_id(2500)) < 8, "ID %.0f (expected %.0f for zeta 2.5 kHz)", id, expected_id(2500));
    CHECK(fe < 30, "ferrous index %.0f (conductive target)", fe);
    CHECK(accepted > 0, "accepted by discrimination");
    CHECK(dep > 8 && dep < 24, "depth estimate %.0f cm (true 15 cm)", dep);
    printf("        threshold %.1f uohm, noise floor %.1f uohm, last confidence %.2f\n", d->thresh * 1e6f, d->noise_floor * 1e6f, d->st.est.confidence);
    /* render the home screen with the target on it */
    if (u) { detector_housekeeping(d); for (int i = 0; i < 400 && !(d->st.target_present && d->st.strength_db > 12); i++) run_periods(d, 20, 0); ui_init(u); ui_draw(u, d, hal_millis()); save_pbm("screen_home_target", (const uint8_t *)u->fb.px); }
}

/* ------------------------------------------------------------------ 6. ferrous rejection */
static void test_ferrous(detector_t *d)
{
    printf("[6] iron nail: magnetic + high-zeta eddy, must be flagged ferrous\n");
    coil_cal_t cal = g_cal;
    sim_reset(); SIM.noise_v_rms = 20e-6f; SIM.g_mag = 30e-3f; SIM.sweeping = 1;
    detector_init(d); d->coil.cal = cal; d->set.sensitivity = 24; detector_apply_settings(d);
    run_periods(d, 8000, 0);
    sim_add_target(0.0f, 0.10f, 60000.0f, 1.0e-3f, 3.0e-3f);
    float fesum = 0; int n = 0, rejected = 0;
    for (int i = 0; i < 40; i++) { run_periods(d, 200, 0); if (d->st.target_present && d->st.strength_db > 6) { fesum += d->st.est.fe_index; n++; rejected += !d->st.accepted; } }
    CHECK(n > 0, "detected (%d samples)", n);
    CHECK(n && fesum / n > 55, "ferrous index %.0f > 55", n ? fesum / n : 0);
    CHECK(rejected > n / 2, "rejected by ferrous limit in %d of %d", rejected, n);
    /* silver coin for contrast */
    memset(SIM.tgt, 0, sizeof SIM.tgt); sim_add_target(0.0f, 0.10f, 800.0f, 3.0e-3f, 0.0f);
    run_periods(d, 2000, 0); float id2 = 0; n = 0;
    for (int i = 0; i < 40; i++) { run_periods(d, 200, 0); if (d->st.target_present && d->st.strength_db > 6) { id2 += d->st.est.cond_id; n++; } }
    CHECK(n && fabsf(id2 / n - expected_id(800)) < 12, "silver ID %.0f (expected %.0f; +-12 at the saturated high-conductivity end)", n ? id2 / n : 0, expected_id(800));
}

/* ------------------------------------------------------------------ 7. noise cancel */
static float line_variance(detector_t *d, int line, int periods)
{
    cpx sum = cpx_make(0, 0); float sum2 = 0;
    for (int i = 0; i < periods; i++) { run_periods(d, 1, 0); sum = cpx_add(sum, d->T_fast[line]); sum2 += cpx_abs2(d->T_fast[line]); }
    float v = sum2 / periods - cpx_abs2(cpx_scale(sum, 1.0f / periods)); return v < 0 ? 0 : sqrtf(v);
}
static void test_noise_cancel(detector_t *d, ui_t *u)
{
    printf("[7] noise cancel: CW interferer 30 Hz from the 41 kHz line\n");
    sim_reset(); SIM.noise_v_rms = 20e-6f; SIM.interf_hz = 40960.0f + 30.0f; SIM.interf_v = 300e-6f;
    detector_init(d); d->set.recovery = 5; detector_apply_settings(d);
    run_periods(d, 2000, 0);
    float before = line_variance(d, 4, 2000);
    noisecancel_start(&d->nc);
    int guard = 0; while (d->nc.active && guard++ < 20000) run_periods(d, 1, 0);
    CHECK(!d->nc.active, "scan completed in %d periods (%.0f ms)", guard, guard * 0.195f);
    CHECK(d->nc.best_M != 100, "chose M=%d (41 kHz line moved to %.2f kHz)", d->nc.best_M, MDX_FS_HZ * 8 / d->nc.best_M / 1e3f);
    run_periods(d, 2000, 0);
    float after = line_variance(d, 4, 2000);
    CHECK(after < before / 4, "41 kHz fast-channel jitter %.1f uohm -> %.1f uohm", before * 1e6f, after * 1e6f);
    if (u) { ui_draw(u, d, hal_millis()); u->screen = SCR_NOISE_CANCEL; ui_draw(u, d, hal_millis()); save_pbm("screen_noise_cancel", (const uint8_t *)u->fb.px); u->screen = SCR_HOME; }
}

/* ------------------------------------------------------------------ 8. PI mode */
static void test_pi(detector_t *d)
{
    printf("[8] pulse induction: deep target in viscous ground\n");
    sim_reset(); SIM.noise_v_rms = 20e-6f; SIM.pi_ground = 2e-3f;
    detector_init(d); d->set.mode = MODE_PI; detector_apply_settings(d);
    pi_seq_t seq; waveform_pi_default(&seq);
    /* ground grab with no target */
    pi_result_t acc = {0}; int n = 0;
    for (int b = 0; b < 4; b++) { hal_tx_run_pi(&seq); pi_record_t r; while (hal_adc_get_pi_record(&r)) { pi_result_t o; pi_process(&d->pi, &r, &o); acc.early += o.early; acc.mid += o.mid; acc.late += o.late; n++; } }
    acc.early /= n; acc.mid /= n; acc.late /= n; pi_ground_grab(&d->pi, &acc);
    /* residual with ground only */
    float res = 0; n = 0;
    for (int b = 0; b < 4; b++) { hal_tx_run_pi(&seq); pi_record_t r; while (hal_adc_get_pi_record(&r)) { pi_result_t o; pi_process(&d->pi, &r, &o); res += fabsf(o.sig_early); n++; } }
    res /= n;
    CHECK(res < 0.2f * fabsf(acc.early), "ground-balanced residual %.2e V vs raw ground %.2e V", res, acc.early);
    sim_add_target(0.0f, 0.30f, 3000.0f, 5.0e-3f, 0.0f);        /* large deep target */
    int det = 0; n = 0; float tau = 0;
    for (int b = 0; b < 4; b++) { hal_tx_run_pi(&seq); pi_record_t r; while (hal_adc_get_pi_record(&r)) { pi_result_t o; pi_process(&d->pi, &r, &o); det += o.target; tau += o.tau_us; n++; } }
    CHECK(det > n / 2, "PI target detected in %d of %d bursts", det, n);
    CHECK(fabsf(tau / n - 53.0f) < 25.0f, "time constant %.0f us (true %.0f us)", tau / n, 1e6f / (2 * (float)M_PI * 3000));

    /* MODE_PI must schedule its own bursts.  Everything above drives hal_tx_run_pi() by hand, which
     * is why it never caught the detector failing to trigger any burst at all in pure PI. */
    sim_reset(); SIM.pi_ground = 2e-3f;
    detector_init(d); d->set.mode = MODE_PI; detector_apply_settings(d);
    sim_add_target(0.0f, 0.20f, 3000.0f, 5.0e-3f, 0.0f);
    uint32_t before = d->st.pi_records;
    run_periods(d, 200, 0);                       /* no hal_tx_run_pi() call here - the detector must do it */
    CHECK(d->st.pi_records > before, "MODE_PI schedules its own bursts (%u records in 200 periods)",
          (unsigned)(d->st.pi_records - before));
    CHECK(hal_tx_pi_busy(), "MODE_PI keeps the bridge pulsing continuously (never hands back to VLF)");
    d->set.mode = MODE_MULTI; detector_apply_settings(d);
    CHECK(!hal_tx_pi_busy(), "leaving MODE_PI stops the burst and returns the timer to the VLF table");
}

/* ------------------------------------------------------------------ 9. learned target + logging + audio */
static void test_learn_log_audio(detector_t *d)
{
    printf("[9] learn-a-target, find logging, audio path\n");
    coil_cal_t cal = g_cal;
    sim_reset(); SIM.sweeping = 1; SIM.g_mag = 10e-3f;
    detector_init(d); d->coil.cal = cal; detector_apply_settings(d);
    sim_add_target(0.0f, 0.08f, 5000.0f, 2.0e-3f, 0.0f);
    run_periods(d, 3000, 0);
    /* find a moment the target is present at the sweep centre and learn it */
    int learned = 0; for (int i = 0; i < 60 && !learned; i++) { run_periods(d, 100, 0); if (d->st.target_present && d->st.strength_db > 6) { detector_learn_target(d, "ring"); learned = 1; } }
    CHECK(learned, "signature stored");
    int matched = 0, seen = 0; for (int i = 0; i < 60; i++) { run_periods(d, 100, 0); if (d->st.target_present && d->st.strength_db > 6) { seen++; matched += d->st.match_idx >= 0; if (getenv("MDX_DEBUG")) printf("    ring: dist %.3f db %.1f id %.0f fe %.0f\n", d->st.match_dist, d->st.strength_db, d->st.est.cond_id, d->st.est.fe_index); } }
    CHECK(seen && matched > seen / 2, "re-detected as 'ring' %d of %d times", matched, seen);
    memset(SIM.tgt, 0, sizeof SIM.tgt); sim_add_target(0.0f, 0.08f, 400.0f, 2.0e-3f, 0.0f);   /* a very different target */
    run_periods(d, 3000, 0); int wrong = 0; seen = 0; for (int i = 0; i < 60; i++) { run_periods(d, 100, 0); if (d->st.target_present && d->st.strength_db > 6) { seen++; wrong += d->st.match_idx >= 0; } }
    CHECK(seen && wrong < seen / 4, "different target not confused with 'ring' (%d of %d)", wrong, seen);
    logger_t lg; log_init(&lg); detector_housekeeping(d);
    remove("build/sd_finds.csv"); log_tick(&lg, d, 1, hal_millis()); FILE *f = fopen("build/sd_finds.csv", "r"); char line[200] = "";
    if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
    CHECK(strlen(line) > 20 && strstr(line, "51.507"), "find log line: %s", line);
    int16_t buf[320]; sim_audio_pull(buf, 320); int nz = 0; for (int i = 0; i < 320; i++) nz += buf[i] != 0;
    CHECK(nz > 0 || d->audio.target_vol == 0, "audio callback produces samples (%d non-zero of 320)", nz);
}

/* ------------------------------------------------------------------ 10. screens */
static void test_screens(detector_t *d, ui_t *u)
{
    printf("[10] UI screens\n");
    sim_reset(); SIM.g_mag = 12e-3f; detector_init(d); d->set.program = PROG_FIELD; detector_housekeeping(d);
    run_periods(d, 500, 0);
    ui_init(u);
    ui_draw(u, d, hal_millis()); save_pbm("screen_home_idle", (const uint8_t *)u->fb.px);
    ui_update(u, d, KEY_MENU, hal_millis()); ui_update(u, d, 0, hal_millis()); ui_draw(u, d, hal_millis()); save_pbm("screen_menu", (const uint8_t *)u->fb.px);
    for (int i = 0; i < 2; i++) { ui_update(u, d, KEY_DOWN, hal_millis()); ui_update(u, d, 0, hal_millis()); }
    ui_update(u, d, KEY_RIGHT, hal_millis()); ui_update(u, d, 0, hal_millis()); ui_draw(u, d, hal_millis()); save_pbm("screen_menu_edit", (const uint8_t *)u->fb.px);
    CHECK(u->screen == SCR_MENU && u->editing && u->menu_idx == 2, "menu navigation: editing item %d", u->menu_idx);
    ui_update(u, d, KEY_RIGHT, hal_millis()); ui_update(u, d, 0, hal_millis());
    CHECK(d->set.sensitivity == 21, "sensitivity edited to %d", d->set.sensitivity);
    ui_update(u, d, KEY_BACK, hal_millis()); ui_update(u, d, 0, hal_millis()); ui_update(u, d, KEY_BACK, hal_millis()); ui_update(u, d, 0, hal_millis());
    CHECK(u->screen == SCR_HOME, "back to home");
    sim_add_target(0.0f, 0.10f, 5000.0f, 2.0e-3f, 0.0f); SIM.sweeping = 0; SIM.coil_x_m = 0; run_periods(d, 300, 0);
    ui_update(u, d, KEY_PINPOINT, hal_millis()); run_periods(d, 200, 0); ui_update(u, d, KEY_PINPOINT, hal_millis()); ui_draw(u, d, hal_millis()); save_pbm("screen_pinpoint", (const uint8_t *)u->fb.px);
    CHECK(u->screen == SCR_PINPOINT && d->st.pinpoint, "pinpoint screen active");
    ui_update(u, d, 0, hal_millis()); ui_update(u, d, 0, hal_millis());
    u->screen = SCR_DIAG; ui_draw(u, d, hal_millis()); save_pbm("screen_diag", (const uint8_t *)u->fb.px);
    u->screen = SCR_COIL_WIZARD; u->wizard_step = 2; u->wiz_L = 0.36e-3f; u->wiz_R = 0.63f; u->wiz_air[4] = cpx_make(1.2e-3f, 0); ui_draw(u, d, hal_millis()); save_pbm("screen_wizard", (const uint8_t *)u->fb.px);
    u->screen = SCR_LEARN; ui_draw(u, d, hal_millis()); save_pbm("screen_learn", (const uint8_t *)u->fb.px);
    CHECK(1, "screens rendered to build/*.pbm");
}

/* ------------------------------------------------------------------ 11. enhancements */
static void test_enhancements(detector_t *d, ui_t *u)
{
    printf("[11] enhancements: auto noise cancel, auto-save, spectrum, iron grunt, battery hours, log screen\n");
    sim_reset(); SIM.g_mag = 10e-3f; SIM.nvm_valid = 0; SIM.sweeping = 1; detector_init(d); d->coil.cal = g_cal; d->coil.calibrated = 1; detector_apply_settings(d); ui_init(u);
    run_periods(d, 6001, 0);
    CHECK(d->nc.active, "noise cancel started automatically 1.2 s after power-up");
    int guard = 0; while (d->nc.active && guard++ < 20000) run_periods(d, 1, 0);
    CHECK(!d->nc.active, "and completed");
    ui_update(u, d, KEY_MENU, hal_millis()); ui_update(u, d, 0, hal_millis());
    ui_update(u, d, KEY_DOWN, hal_millis()); ui_update(u, d, 0, hal_millis()); ui_update(u, d, KEY_DOWN, hal_millis()); ui_update(u, d, 0, hal_millis());
    ui_update(u, d, KEY_RIGHT, hal_millis()); ui_update(u, d, 0, hal_millis()); ui_update(u, d, KEY_RIGHT, hal_millis()); ui_update(u, d, 0, hal_millis());
    ui_update(u, d, KEY_BACK, hal_millis()); ui_update(u, d, 0, hal_millis()); ui_update(u, d, KEY_BACK, hal_millis()); ui_update(u, d, 0, hal_millis());
    settings_t saved; int ok = hal_nvm_read(&saved, sizeof saved);
    CHECK(ok && saved.sensitivity == d->set.sensitivity && saved.crc == settings_crc(&saved), "settings auto-saved on leaving the menu (sensitivity %d, crc ok)", saved.sensitivity);
    /* key auto-repeat: hold UP for 1 s on the home screen -> sensitivity climbs by more than one */
    int s0 = d->set.sensitivity; uint32_t t0 = hal_millis();
    for (int i = 0; i < 40; i++) { sim_set_time(SIM.t_s + 0.025); ui_update(u, d, KEY_UP, hal_millis()); }
    ui_update(u, d, 0, hal_millis());
    CHECK(d->set.sensitivity >= s0 + 3, "auto-repeat: sensitivity %d -> %d after holding UP for %.1f s", s0, d->set.sensitivity, (hal_millis() - t0) / 1000.0f);
    /* nail with iron grunt */
    memset(SIM.tgt, 0, sizeof SIM.tgt); sim_add_target(0.0f, 0.10f, 60000.0f, 0.8e-3f, 4.0e-3f); run_periods(d, 2000, 0);
    int grunt = 0, present = 0; for (int i = 0; i < 40; i++) { run_periods(d, 200, 0); if (d->st.target_present && d->st.strength_db > 10) { present++; grunt += d->audio.grunt; if (getenv("MDX_DEBUG")) printf("    fe %.0f accepted %d grunt %d iron_grunt %d fe_limit %d db %.1f\n", d->st.est.fe_index, d->st.accepted, d->audio.grunt, d->set.iron_grunt, d->set.fe_limit_lo, d->st.strength_db); } }
    CHECK(present && grunt > present / 2, "iron grunt engaged on a nail (%d of %d)", grunt, present);
    float ssum = 0; for (int k = 0; k < MDX_NLINES; k++) ssum += d->st.spectrum[k];
    CHECK(ssum > 1.0f && ssum <= 5.0f, "five-line signature populated (sum %.2f, max bar = 1)", ssum);
    detector_housekeeping(d);
    CHECK(d->st.batt_hours > 5 && d->st.batt_hours < 20, "battery estimate %.1f h at %d %%", d->st.batt_hours, d->st.batt_pct);
    SIM.batt_v = 3.40f; detector_housekeeping(d); CHECK(d->st.low_batt, "low-battery flag at 3.40 V"); SIM.batt_v = 3.9f;
    u->screen = SCR_HOME; ui_draw(u, d, hal_millis()); save_pbm("screen_home_iron", (const uint8_t *)u->fb.px);
    u->screen = SCR_LOG; ui_draw(u, d, hal_millis()); save_pbm("screen_log", (const uint8_t *)u->fb.px);
    ui_update(u, d, KEY_RIGHT, hal_millis()); CHECK(u->mark_find, "log screen: RIGHT marks a find");
    u->screen = SCR_HOME; u->last_keys = 0;
    /* 6-key pad: MENU short press opens the menu, MENU held 0.8 s acts as BACK */
    ui_update(u, d, 0, hal_millis());
    ui_update(u, d, KEY_MENU, hal_millis()); sim_set_time(SIM.t_s + 0.1); ui_update(u, d, 0, hal_millis());
    CHECK(u->screen == SCR_MENU, "MENU short press opens the menu");
    ui_update(u, d, KEY_MENU, hal_millis());
    for (int i = 0; i < 10; i++) { sim_set_time(SIM.t_s + 0.1); ui_update(u, d, KEY_MENU, hal_millis()); }
    ui_update(u, d, 0, hal_millis());
    CHECK(u->screen == SCR_HOME, "MENU held 1 s acts as BACK (menu closed, no re-open on release)");
    /* PINPOINT held 1.5 s = ground grab */
    float gb0 = d->ground.a_grab;
    ui_update(u, d, KEY_PINPOINT, hal_millis()); sim_set_time(SIM.t_s + 0.5); ui_update(u, d, KEY_PINPOINT, hal_millis());
    CHECK(u->screen == SCR_PINPOINT && !u->gb_active, "PINPOINT held 0.5 s: pinpoint mode, no ground grab yet");
    for (int i = 0; i < 12; i++) { sim_set_time(SIM.t_s + 0.1); run_periods(d, 50, 0); ui_update(u, d, KEY_PINPOINT, hal_millis()); }
    CHECK(u->gb_active, "PINPOINT held 1.7 s: ground grab active (a_grab %.4f -> %.4f)", gb0, d->ground.a_grab);
    ui_draw(u, d, hal_millis()); save_pbm("screen_pinpoint_gb", (const uint8_t *)u->fb.px);
    ui_update(u, d, 0, hal_millis()); CHECK(u->screen == SCR_HOME, "release returns home");
    u->last_keys = 0;
}

/* ------------------------------------------------------------------ 12. phone link */
static uint8_t lk_buf[4096]; static int lk_n;
static int lk_req(link_t *l, int type, int seq, const uint8_t *p, int n)
{   /* phone sends one request, detector processes it, we collect every byte it answered */
    uint8_t f[LINK_MAX_PAYLOAD + 6]; int m = link_build_frame(f, type, seq, p, n);
    sim_link_phone_send(f, m); uint8_t b[64]; int k; while ((k = hal_link_read(b, sizeof b)) > 0) link_rx_bytes(l, b, k);
    lk_n = sim_link_phone_take(lk_buf, sizeof lk_buf); return lk_n;
}
/* find the first frame of a given type in the collected bytes; returns payload pointer and length */
static const uint8_t *lk_frame(int type, int *len)
{
    for (int i = 0; i + 6 <= lk_n; i++) {
        if (lk_buf[i] != LINK_SOF) continue; int L = lk_buf[i + 1]; if (i + 6 + L > lk_n) break;
        if (link_crc16(lk_buf + i + 2, L + 2) != (uint16_t)((lk_buf[i + 4 + L] << 8) | lk_buf[i + 5 + L])) continue;
        if (lk_buf[i + 2] == type) { *len = L; return lk_buf + i + 4; }
        i += 5 + L;
    }
    *len = 0; return NULL;
}
static void test_link(detector_t *d, ui_t *u)
{
    printf("[12] phone link: framing, auth, item table, set, actions, status, finds, diag\n");
    static logger_t lg; static link_t l; int L; const uint8_t *r;
    sim_reset(); SIM.g_mag = 10e-3f; SIM.nvm_valid = 0; SIM.sweeping = 1; SIM.gnss_valid = 1; SIM.lat = 51.5074; SIM.lon = -0.1278;
    detector_init(d); d->coil.cal = g_cal; d->coil.calibrated = 1; d->set.auto_nc_at_boot = 0; detector_apply_settings(d); ui_init(u); log_init(&lg); link_init(&l, d, u, &lg);
    SIM.link_connected = 1; run_periods(d, 400, 0);
    /* not authenticated: everything but HELLO is refused */
    uint8_t p[8] = {0}; lk_req(&l, LM_VALUES, 1, p, 0); r = lk_frame(LM_ERR | 0x80, &L);
    CHECK(r && r[0] == LM_VALUES && r[1] == LE_NOT_AUTH, "request before HELLO is refused (not authenticated)");
    p[0] = LINK_PROTO_VER; p[1] = 0x39; p[2] = 0x05; lk_req(&l, LM_HELLO, 2, p, 3); r = lk_frame(LM_HELLO | 0x80, &L);   /* 0x0539 = 1337 */
    CHECK(r && L == 30 && r[1] == 0 && lk_frame(LM_ERR | 0x80, &L), "HELLO with the wrong PIN answers auth=0 plus an error");
    p[1] = 1234 & 0xFF; p[2] = 1234 >> 8; lk_req(&l, LM_HELLO, 3, p, 3); r = lk_frame(LM_HELLO | 0x80, &L);
    CHECK(r && r[1] == 1 && r[14] == ui_item_count() && !strcmp((const char *)r + 2, LINK_FW_VERSION), "HELLO with PIN 1234: authenticated, %d items, fw %s", r ? r[14] : -1, r ? (const char *)r + 2 : "");
    /* corrupt CRC */
    uint8_t f[16]; int m = link_build_frame(f, LM_VALUES, 4, p, 0); f[m - 1] ^= 0xFF; sim_link_phone_send(f, m);
    { uint8_t b[64]; int k; while ((k = hal_link_read(b, sizeof b)) > 0) link_rx_bytes(&l, b, k); lk_n = sim_link_phone_take(lk_buf, sizeof lk_buf); }
    r = lk_frame(LM_ERR | 0x80, &L); CHECK(r && r[1] == LE_BAD_CRC && l.frames_bad == 1, "corrupted frame answered with BAD_CRC and dropped");
    /* item table: every item decodes, names match the on-device menu */
    int n_items = ui_item_count(), ok_items = 0, n_actions = 0, n_labels = 0;
    for (int i = 0; i < n_items; i++) {
        p[0] = (uint8_t)i; lk_req(&l, LM_ITEM, 5, p, 1); r = lk_frame(LM_ITEM | 0x80, &L); ui_item_info_t it; ui_item_info(i, &it);
        if (r && r[0] == i && r[1] == it.is_action && !strcmp((const char *)r + 7, it.name)) ok_items++;
        if (r && r[1]) n_actions++; if (r) n_labels += r[6];
    }
    CHECK(ok_items == n_items, "item table: %d of %d items decode with matching names (%d actions, %d labelled values)", ok_items, n_items, n_actions, n_labels);
    /* values + set */
    lk_req(&l, LM_VALUES, 6, p, 0); r = lk_frame(LM_VALUES | 0x80, &L);
    CHECK(r && r[0] == n_items && (int16_t)(r[1 + 2 * 2] | (r[2 + 2 * 2] << 8)) == d->set.sensitivity, "VALUES: %d values, sensitivity slot reads %d", r ? r[0] : 0, d->set.sensitivity);
    p[0] = 2; p[1] = 27; p[2] = 0; lk_req(&l, LM_SET, 7, p, 3); r = lk_frame(LM_SET | 0x80, &L);
    CHECK(r && d->set.sensitivity == 27 && d->settings_dirty, "SET sensitivity=27 applied (dirty flag set)");
    p[0] = 2; p[1] = 99; p[2] = 0; lk_req(&l, LM_SET, 8, p, 3); r = lk_frame(LM_ERR | 0x80, &L);
    CHECK(r && r[1] == LE_BAD_VALUE && d->set.sensitivity == 27, "SET out of range refused");
    p[0] = 1; p[1] = 5; p[2] = 0; lk_req(&l, LM_SET, 9, p, 3); CHECK(d->set.mode == 5, "SET mode=PI via the item table");
    p[0] = 1; p[1] = 0; p[2] = 0; lk_req(&l, LM_SET, 9, p, 3);
    /* actions */
    p[0] = LA_GROUND_GRAB; lk_req(&l, LM_ACTION, 10, p, 1); r = lk_frame(LM_ACTION | 0x80, &L); CHECK(r && r[1] == 0 && d->ground.a_grab != 0, "ACTION ground grab (a_grab %.4f)", d->ground.a_grab);
    p[0] = LA_PINPOINT_ON; lk_req(&l, LM_ACTION, 11, p, 1); CHECK(d->st.pinpoint && u->screen == SCR_PINPOINT, "ACTION pinpoint on (device screen follows)");
    p[0] = LA_PINPOINT_OFF; lk_req(&l, LM_ACTION, 12, p, 1); CHECK(!d->st.pinpoint && u->screen == SCR_HOME, "ACTION pinpoint off");
    p[0] = LA_NOISE_CANCEL; lk_req(&l, LM_ACTION, 13, p, 1); CHECK(d->nc.active, "ACTION noise cancel started");
    int guard = 0; while (d->nc.active && guard++ < 20000) { run_periods(d, 1, 0); link_tick(&l, hal_millis()); }
    lk_n = sim_link_phone_take(lk_buf, sizeof lk_buf); r = lk_frame(LM_EVENT | 0x80, &L);
    CHECK(r && r[0] == LV_NC_DONE && r[1] == d->st.noise_M, "EVENT noise cancel done, M=%d", r ? r[1] : 0);
    /* status one-shot with a coin under the coil */
    sim_add_target(0.0f, 0.10f, 4000.0f, 12e-3f, 0.0f); run_periods(d, 1500, 0);
    int seen = 0; for (int i = 0; i < 40 && !seen; i++) { run_periods(d, 100, 0); seen = d->st.target_present; }
    detector_housekeeping(d);
    p[0] = 0xFF; p[1] = 0xFF; lk_req(&l, LM_STATUS, 14, p, 2); r = lk_frame(LM_STATUS | 0x80, &L);
    int id10 = r ? (int16_t)(r[1] | (r[2] << 8)) : 0; int32_t lat = r ? (int32_t)(r[29] | (r[30] << 8) | (r[31] << 16) | ((uint32_t)r[32] << 24)) : 0;
    CHECK(r && L == LINK_STATUS_LEN && (r[0] & LSF_PRESENT) && fabsf(id10 / 10.0f - d->st.est.cond_id) < 0.11f && r[28] == 1 && lat == 515074000,
          "STATUS one-shot: %d bytes, present, ID %.1f, lat %d", L, id10 / 10.0f, lat);
    FILE *fx = fopen("build/link_status_fixture.hex", "w"); if (fx) { for (int i = 0; i < lk_n; i++) fprintf(fx, "%02x", lk_buf[i]); fprintf(fx, "\n# cond_id=%.1f fe=%d batt_pct=%d finds=%lu sens=%d\n", d->st.est.cond_id, (int)d->st.est.fe_index, d->st.batt_pct, (unsigned long)d->st.finds, d->set.sensitivity); fclose(fx); }
    /* stream at 100 ms: expect ~10 frames per simulated second */
    p[0] = 100; p[1] = 0; lk_req(&l, LM_STATUS, 15, p, 2); uint32_t t0 = hal_millis(); int frames = 0;
    while (hal_millis() - t0 < 1000) { run_periods(d, 5, 0); link_tick(&l, hal_millis()); }
    lk_n = sim_link_phone_take(lk_buf, sizeof lk_buf); for (int i = 0; i + 6 <= lk_n; i++) if (lk_buf[i] == LINK_SOF && lk_buf[i + 2] == (LM_STATUS | 0x80)) { frames++; i += 5 + lk_buf[i + 1]; }
    CHECK(frames >= 9 && frames <= 11, "STATUS stream at 100 ms: %d frames in 1 s", frames);
    p[0] = 0; p[1] = 0; lk_req(&l, LM_STATUS, 16, p, 2);
    /* learn + learned list + forget */
    for (int i = 0; i < 600 && !(d->st.target_present && d->st.strength_db > 6); i++) run_periods(d, 20, 0);
    p[0] = LA_LEARN; memcpy(p + 1, "Coin", 5); lk_req(&l, LM_ACTION, 17, p, 6); lk_req(&l, LM_LEARNED, 18, p, 0); r = lk_frame(LM_LEARNED | 0x80, &L);
    CHECK(r && r[0] >= 1 && !strcmp((const char *)r + 2, "Coin"), "ACTION learn 'Coin' then LEARNED lists it (%d entries)", r ? r[0] : 0);
    p[0] = LA_FORGET; p[1] = r ? r[1] : 0; lk_req(&l, LM_ACTION, 19, p, 2); lk_req(&l, LM_LEARNED, 20, p, 0); r = lk_frame(LM_LEARNED | 0x80, &L);
    CHECK(r && r[0] == 0, "ACTION forget removes it");
    /* mark find -> ring -> FIND record with position */
    sim_set_time(SIM.t_s + 5.0);   /* the logger debounces finds to one per 3 s */
    p[0] = LA_MARK_FIND; lk_req(&l, LM_ACTION, 21, p, 1); log_tick(&lg, d, l.mark_find, hal_millis()); l.mark_find = 0; d->st.finds = lg.finds; link_tick(&l, hal_millis());
    lk_n = sim_link_phone_take(lk_buf, sizeof lk_buf); r = lk_frame(LM_EVENT | 0x80, &L); CHECK(r && r[0] == LV_FIND, "EVENT find after MARK_FIND");
    p[0] = 0; p[1] = 0; lk_req(&l, LM_FIND, 22, p, 2); r = lk_frame(LM_FIND | 0x80, &L);
    int32_t flon = r ? (int32_t)(r[10] | (r[11] << 8) | (r[12] << 16) | ((uint32_t)r[13] << 24)) : 0;
    CHECK(r && L == 31 && flon == -1278000 && r[16] <= 100, "FIND 0: lon %d, ID %d, depth %d cm", flon, r ? (int16_t)(r[14] | (r[15] << 8)) : 0, r ? r[17] : 0);
    /* diagnostics */
    lk_req(&l, LM_DIAG, 23, p, 0); r = lk_frame(LM_DIAG | 0x80, &L); float f0; if (r) memcpy(&f0, r + 64, 4);
    CHECK(r && L == 109 && fabsf(f0 - d->demod.f_hz[0]) < 1 && !strcmp((const char *)r + 97, d->coil.cal.name), "DIAG: %d bytes, line 0 = %.0f Hz, coil '%s'", L, r ? f0 : 0, r ? (const char *)r + 97 : "");
    /* save + defaults keep the PIN; disconnect forgets auth */
    p[0] = LA_SAVE; lk_req(&l, LM_ACTION, 24, p, 1); settings_t sv; CHECK(hal_nvm_read(&sv, sizeof sv) && sv.sensitivity == 27 && sv.link_pin == 1234, "ACTION save wrote sensitivity 27 and PIN to NVM");
    SIM.link_connected = 0; link_tick(&l, hal_millis()); SIM.link_connected = 1; lk_req(&l, LM_VALUES, 25, p, 0); r = lk_frame(LM_ERR | 0x80, &L);
    CHECK(r && r[1] == LE_NOT_AUTH, "after a BLE drop the phone must HELLO again");
    memset(SIM.tgt, 0, sizeof SIM.tgt); SIM.gnss_valid = 0;
}

/* ------------------------------------------------------------------ 13. headless (no UI board) */
static void test_headless(detector_t *d, ui_t *u)
{
    printf("[13] headless: UI board absent -> no LCD, main-board power key, cues, phone link still full\n");
    static logger_t lg; static link_t l; static sys_t sy; int L; const uint8_t *r;
    sim_reset(); SIM.g_mag = 10e-3f; SIM.nvm_valid = 0; SIM.sweeping = 1; SIM.ui_present = 0;
    detector_init(d); d->coil.cal = g_cal; d->coil.calibrated = 1; d->set.auto_nc_at_boot = 0; detector_apply_settings(d); ui_init(u); log_init(&lg); link_init(&l, d, u, &lg);
    sys_init(&sy, 2); CHECK(!sy.ui_on, "auto-detect: UI_DET open -> headless");
    sys_t sy1; SIM.ui_present = 1; sys_init(&sy1, 2); CHECK(sy1.ui_on, "auto-detect: UI_DET grounded -> UI on"); sys_init(&sy1, 0); CHECK(!sy1.ui_on, "MDX_UI_BOARD=0 forces headless even with a board"); SIM.ui_present = 0;
    int lcd0 = SIM.lcd_updates; SIM.keys = KEY_MENU | KEY_UP;
    for (int i = 0; i < 30; i++) { run_periods(d, 10, 0); sys_tick(&sy, d, u, &l, hal_millis()); }
    CHECK(SIM.lcd_updates == lcd0 && u->screen == SCR_HOME && d->set.sensitivity == 20, "no LCD writes, keypad ignored (sensitivity stays %d)", d->set.sensitivity);
    CHECK(audio_cue_active(&d->audio), "boot cue playing on headphones");
    int16_t buf[320]; for (int i = 0; i < 40; i++) sim_audio_pull(buf, 320);
    /* status LED blinks while searching */
    int seen_on = 0, seen_off = 0; for (int i = 0; i < 80; i++) { run_periods(d, 10, 0); sys_tick(&sy, d, u, &l, hal_millis()); seen_on |= SIM.led_on; seen_off |= !SIM.led_on; }
    CHECK(seen_on && seen_off, "status LED blinks while searching");
    /* phone link works and turns the LED solid; link-up cue plays */
    SIM.link_connected = 1; uint8_t p[4] = {LINK_PROTO_VER, 1234 & 0xFF, 1234 >> 8, 0}; lk_req(&l, LM_HELLO, 1, p, 3); r = lk_frame(LM_HELLO | 0x80, &L);
    CHECK(r && r[1] == 1, "HELLO authenticates headless");
    sys_tick(&sy, d, u, &l, hal_millis()); CHECK(SIM.led_on && audio_cue_active(&d->audio), "LED solid + link-up cue once the phone is on");
    p[0] = 2; p[1] = 25; p[2] = 0; lk_req(&l, LM_SET, 2, p, 3); CHECK(d->set.sensitivity == 25, "SET from the phone applies without a UI");
    p[0] = LA_PINPOINT_ON; lk_req(&l, LM_ACTION, 3, p, 1); CHECK(d->st.pinpoint, "pinpoint from the phone");
    p[0] = LA_PINPOINT_OFF; lk_req(&l, LM_ACTION, 4, p, 1);
    /* low battery cue */
    for (int i = 0; i < 40; i++) sim_audio_pull(buf, 320);
    SIM.batt_v = 3.40f; detector_housekeeping(d); sys_tick(&sy, d, u, &l, hal_millis()); CHECK(d->st.low_batt && audio_cue_active(&d->audio), "low-battery cue"); SIM.batt_v = 3.9f;
    /* power key: short press nothing, 1.5 s hold -> off */
    SIM.keys = KEY_POWER; int off = 0; for (int i = 0; i < 10; i++) { sim_set_time(SIM.t_s + 0.1); off |= sys_tick(&sy, d, u, &l, hal_millis()); }
    CHECK(!off, "POWER held 1.0 s: still on");
    for (int i = 0; i < 8; i++) { sim_set_time(SIM.t_s + 0.1); off |= sys_tick(&sy, d, u, &l, hal_millis()); }
    CHECK(off, "POWER held 1.8 s: power-off requested");
    SIM.keys = 0; SIM.link_connected = 0; SIM.ui_present = 1;
}

int main(void)
{
    static detector_t d; static ui_t u;
    test_waveform();
    test_demod();
    test_coil_LR();
    test_wizard(&d);
    test_detection(&d, &u);
    test_ferrous(&d);
    test_noise_cancel(&d, &u);
    test_pi(&d);
    test_learn_log_audio(&d);
    test_screens(&d, &u);
    test_enhancements(&d, &u);
    test_link(&d, &u);
    test_headless(&d, &u);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
