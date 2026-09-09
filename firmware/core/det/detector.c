#include "detector.h"
#include "../tx/waveform.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define SET_MAGIC 0x53455431u

#include <stddef.h>
uint16_t settings_crc(const settings_t *s) { return coil_crc16((const uint8_t *)s, (int)offsetof(settings_t, crc)); }   /* never include the crc field or trailing padding */

int detector_accept_id(const settings_t *s, float cond_id)
{
    int id = (int)(cond_id + 0.5f); if (id < 0) id = 0; if (id > 99) id = 99;
    return (s->accept[id >> 3] >> (id & 7)) & 1;
}
void detector_set_accept(settings_t *s, int lo, int hi, int accept)
{
    for (int id = lo; id <= hi && id < 100; id++) {
        if (id < 0) continue;
        if (accept) s->accept[id >> 3] |= (uint8_t)(1 << (id & 7)); else s->accept[id >> 3] &= (uint8_t)~(1 << (id & 7));
    }
}

void detector_default_settings(settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->link_pin = 1234;
    s->magic = SET_MAGIC; s->mode = MODE_MULTI; s->program = PROG_FIELD;
    s->sensitivity = 20; s->recovery = 5; s->tone_scheme = TONE_SCHEME_PITCH; s->volume = 7; s->threshold_vol = 2; s->iron_volume = 3;
    s->gb_mode = 0; s->salt_mode = 0; s->noise_M = MDX_M_DEFAULT; s->tx_level = 4;
    s->fe_limit_lo = 55; s->fe_limit_hi = 100; s->hybrid_ratio = 0; s->frontlight = 30; s->log_enable = 1; s->units_in = 0;
    s->auto_nc_at_boot = 1; s->sens_auto = 0; s->iron_grunt = 1;
    detector_set_accept(s, 0, 99, 1);
    s->depth_cal = 1.0f;
}

void detector_load_program(detector_t *d, int prog)
{
    settings_t *s = &d->set;
    s->program = (uint8_t)prog;
    detector_set_accept(s, 0, 99, 1);
    switch (prog) {
    case PROG_PARK:  s->mode = MODE_MULTI; s->recovery = 6; s->fe_limit_lo = 50; detector_set_accept(s, 0, 8, 0); s->tone_scheme = TONE_SCHEME_5; s->hybrid_ratio = 0; break;
    case PROG_FIELD: s->mode = MODE_MULTI; s->recovery = 5; s->fe_limit_lo = 55; s->tone_scheme = TONE_SCHEME_PITCH; s->hybrid_ratio = 0; break;
    case PROG_BEACH: s->mode = MODE_MULTI; s->salt_mode = 1; s->recovery = 4; s->fe_limit_lo = 60; s->tone_scheme = TONE_SCHEME_2; s->hybrid_ratio = 0; break;
    case PROG_GOLD:  s->mode = MODE_HYBRID; s->recovery = 3; s->fe_limit_lo = 70; s->tone_scheme = TONE_SCHEME_VCO; s->hybrid_ratio = 8; s->sensitivity = 25; break;
    case PROG_RELIC: s->mode = MODE_HYBRID; s->recovery = 3; s->fe_limit_lo = 75; s->tone_scheme = TONE_SCHEME_PITCH; s->hybrid_ratio = 16; break;
    }
    detector_apply_settings(d);
}

static float vtx_for_level(int lvl) { if (lvl < 1) lvl = 1; if (lvl > 10) lvl = 10; return MDX_VTX_MIN_V + (MDX_VTX_MAX_V - MDX_VTX_MIN_V) * (float)(lvl - 1) / 9.0f; }

void detector_apply_settings(detector_t *d)
{
    settings_t *s = &d->set;
    int M = s->noise_M; if (M < MDX_M_MIN || M > MDX_M_MAX) M = MDX_M_DEFAULT;
    d->want_M = M;
    if (d->demod.M != M) { demod_init(&d->demod, M); ground_set_freqs(&d->ground, d->demod.f_hz); target_model_init(&d->tmodel, d->demod.f_hz); hal_adc_set_period(M); }
    switch (s->mode) {
    case MODE_SINGLE_5K:  waveform_build_single(M, 1, &d->txtab); break;
    case MODE_SINGLE_10K: waveform_build_single(M, 2, &d->txtab); break;
    case MODE_SINGLE_20K: waveform_build_single(M, 4, &d->txtab); break;
    case MODE_SINGLE_40K: waveform_build_single(M, 8, &d->txtab); break;
    default:              waveform_build(M, &d->txtab); break;
    }
    hal_tx_set_table(&d->txtab);
    hal_tx_set_rail(vtx_for_level(s->tx_level));
    /* The bridge and its gate drivers must stay live in PI mode too - PI drives the same H-bridge.
     * What distinguishes pure PI is that the burst re-arms itself instead of handing the timer back
     * to the VLF table, so the VLF waveform never runs at the 100 V PI rail. */
    d->piseq.continuous = (s->mode == MODE_PI);
    if (s->mode != MODE_PI) hal_tx_pi_stop();
    hal_tx_enable(1);
    d->ground.mode = s->gb_mode;
    if (s->salt_mode) { d->ground.rate_fast = 0.5f; }
    d->tmodel.fe_threshold = (float)s->fe_limit_lo;
    d->tmodel.depth_ref_strength = d->coil.cal.depth_ref_strength * s->depth_cal;
    d->audio.master = (float)s->volume / 10.0f;
    audio_set_threshold(&d->audio, s->threshold_vol ? 45.0f : 0.0f, (float)s->threshold_vol / 10.0f);
    /* sensitivity 1..30 -> threshold: 30 = 3 x noise floor, 1 = 60 x noise floor */
    float mult = 3.0f * powf(20.0f, (30.0f - (float)s->sensitivity) / 29.0f);
    d->thresh = d->noise_floor * mult;
    hal_lcd_frontlight(s->frontlight);
    d->settings_dirty = 1;
}

void detector_init(detector_t *d)
{
    memset(d, 0, sizeof(*d));
    detector_default_settings(&d->set);
    settings_t tmp;
    if (hal_nvm_read(&tmp, sizeof(tmp)) && tmp.magic == SET_MAGIC && tmp.crc == settings_crc(&tmp)) d->set = tmp;
    demod_init(&d->demod, MDX_M_DEFAULT);
    ground_init(&d->ground, d->demod.f_hz);
    target_model_init(&d->tmodel, d->demod.f_hz);
    noisecancel_init(&d->nc);
    pi_init(&d->pi);
    classify_init(&d->cls);
    audio_init(&d->audio, AUDIO_FS);
    waveform_pi_default(&d->piseq);
    coil_load(&d->coil);
    d->noise_floor = 20e-6f;     /* ohm; refined at run time */
    d->last_ms = hal_millis();
    detector_apply_settings(d);
    hal_audio_start(audio_fill, &d->audio);
}

static void trace_push(status_t *st, float c, float fe)
{
    st->trace_cond[st->trace_head] = c; st->trace_fe[st->trace_head] = fe;
    st->trace_head = (st->trace_head + 1) % 24; if (st->trace_n < 24) st->trace_n++;
}

int detector_period(detector_t *d, const adc_frame_t *f, const imu_sample_t *imu)
{
    settings_t *s = &d->set; status_t *st = &d->st;
    uint32_t now = hal_millis();
    float dt = (float)(now - d->last_ms) * 1e-3f; if (dt <= 0 || dt > 0.1f) dt = (float)f->M / MDX_FS_HZ; d->last_ms = now;
    /* Pure PI: the VLF transmitter is off, so the demodulated spectrum is noise and none of the
     * ground/target pipeline below means anything.  Keep the bridge pulsing back-to-back and let
     * detector_pi_record() drive the audio; everything else is skipped. */
    if (s->mode == MODE_PI) {
        st->periods++;
        if (!hal_tx_pi_busy()) hal_tx_run_pi(&d->piseq);
        return 0;
    }
    demod_out_t o; demod_period(&d->demod, f->rx, f->itx, &o);
    st->periods++; st->itx_rms_a = o.itx_rms_a; st->clipped = o.clipped;
    st->coil_present = o.itx_rms_a > 0.02f;
    /* noise cancel */
    int M = noisecancel_step(&d->nc, o.S, o.noise, d->demod.M);
    st->noise_level = sqrtf(d->nc.current_level); st->noise_M = d->demod.M; st->nc_active = d->nc.active;
    if (M != d->demod.M) { s->noise_M = (uint8_t)M; detector_apply_settings(d); return 0; }
    /* coil correction */
    cpx S[MDX_NLINES]; coil_apply(&d->coil.cal, o.S, S);
    /* swing speed from the gyro (coil at ~1.2 m from the wrist pivot) */
    float speed = imu ? 1.2f * sqrtf(imu->gx * imu->gx + imu->gz * imu->gz) : 0.5f;
    st->swing_speed = speed;
    /* slow average for the ground tracker; seeded directly at start-up so nothing has to ramp */
    float a_slow = dt / (0.5f + dt);
    if (st->periods <= 4) a_slow = 1.0f;
    for (int k = 0; k < MDX_NLINES; k++) d->S_slow[k] = cpx_add(d->S_slow[k], cpx_scale(cpx_sub(S[k], d->S_slow[k]), a_slow));
    if (st->periods < 1500) { float a, b; ground_fit(&d->ground, d->S_slow, &a, &b); d->ground.a = a; d->ground.b = b; ground_update(&d->ground, d->S_slow, dt, speed, 0); }
    else { if (d->gate_timer > 0) d->gate_timer -= dt; ground_update(&d->ground, d->S_slow, dt, speed, d->gate_timer > 0); }
    st->ground_phase_deg = d->ground.phase_deg; st->ground_strength = d->ground.strength; st->salt_ratio = d->ground.salt_ratio;
    st->hotrock = ground_misfit(&d->ground, d->S_slow);
    /* ground balance this period, recovery-speed average, motion high-pass */
    cpx T[MDX_NLINES]; ground_balance(&d->ground, S, T);
    int navg = 256 >> (s->recovery - 1); if (navg < 2) navg = 2;          /* recovery 1..8 -> 256 .. 2 periods (50 ms .. 0.4 ms) */
    float a_fast = 1.0f / (float)navg, a_ref = 1.0f / (float)(navg * 24);  /* motion high-pass ~24x slower */
    cpx Tm[MDX_NLINES];
    for (int k = 0; k < MDX_NLINES; k++) {
        d->T_fast[k] = cpx_add(d->T_fast[k], cpx_scale(cpx_sub(T[k], d->T_fast[k]), a_fast));
        if (!st->pinpoint) d->T_ref[k] = cpx_add(d->T_ref[k], cpx_scale(cpx_sub(d->T_fast[k], d->T_ref[k]), a_ref));
        Tm[k] = st->pinpoint ? cpx_sub(d->T_fast[k], d->T_ref[k]) : cpx_sub(d->T_fast[k], d->T_ref[k]);
    }
    float mag = sqrtf(cpx_norm2(Tm, MDX_NLINES));
    /* physical-sign test: eddy currents always give Im(T) < 0 and magnetic material Im ~ 0, so a
     * vector whose imaginary sum is clearly positive is the undershoot tail of the motion filter */
    float im_sum = 0, re_sum = 0; for (int k = 0; k < MDX_NLINES; k++) { im_sum += Tm[k].im; re_sum += Tm[k].re; }
    /* rule 2: a negative real part (eddy) always comes with a negative imaginary part; negative-real with
     * positive-imaginary is the sign-flipped tail of a magnetic (iron) target, the classic "iron falsing" */
    int physical = im_sum <= 0.15f * mag && !(re_sum < 0 && im_sum > 0.02f * mag);
    /* noise floor tracks the quiet level; threshold follows sensitivity */
    if (st->periods < 1500) d->noise_floor += 0.02f * (mag - d->noise_floor);
    else if (mag < 3.0f * d->noise_floor) d->noise_floor += 0.002f * (mag - d->noise_floor);
    float mult = s->sens_auto ? 5.0f : 3.0f * powf(20.0f, (30.0f - (float)s->sensitivity) / 29.0f);   /* auto: 5x the tracked floor */
    d->thresh = d->noise_floor * mult;
    if (mag > 2.0f * d->thresh) d->gate_timer = 1.5f;     /* freeze ground tracking while metal is in the field and until the 0.5 s averager has flushed it */
    int present = mag > d->thresh && physical;
    st->target_present = present;
    st->strength_db = 20.0f * log10f((mag + 1e-12f) / (d->thresh + 1e-12f));
    int event = 0;
    if (present) {
        if (st->strength_db > 3.0f || !st->est.valid) target_fit(&d->tmodel, Tm, &st->est);   /* hold the last good estimate on weak edges */
        st->match_idx = classify_match(&d->cls, Tm, &st->match_dist);
        int fe_ok = st->est.fe_index < (float)s->fe_limit_lo;
        st->accepted = fe_ok && detector_accept_id(s, st->est.cond_id);
        trace_push(st, st->est.cond_id, st->est.fe_index);
        { float mx = 0; for (int k = 0; k < MDX_NLINES; k++) { float a = cpx_abs(Tm[k]); if (a > mx) mx = a; } for (int k = 0; k < MDX_NLINES; k++) st->spectrum[k] = mx > 0 ? cpx_abs(Tm[k]) / mx : 0; }
        float str01 = st->strength_db / 40.0f; if (str01 > 1) str01 = 1; if (str01 < 0) str01 = 0;
        float tone = audio_tone_for_id(s->tone_scheme, st->est.cond_id, !fe_ok, str01);
        float vol = st->accepted ? 0.3f + 0.7f * str01 : (fe_ok ? 0.0f : (float)s->iron_volume / 10.0f * (0.2f + 0.5f * str01));
        if (st->pinpoint) { tone = 300.0f + 700.0f * str01; vol = 0.2f + 0.8f * str01; }
        audio_set(&d->audio, tone, vol);
        audio_set_grunt(&d->audio, (!fe_ok && s->iron_grunt) ? 1 : 0);
        d->hold_timer += dt; event = 1;
    } else {
        audio_set(&d->audio, d->audio.target_freq, 0.0f);
        d->hold_timer = 0;
    }
    if (st->pinpoint) st->pinpoint_level = mag / (d->thresh * 20.0f);
    /* automatic noise cancel shortly after power-up, once the ground model has settled */
    if (s->auto_nc_at_boot && !st->nc_done_boot && st->periods == 6000) { noisecancel_start(&d->nc); st->nc_done_boot = 1; }
    /* hybrid scheduling: interleave PI bursts */
    if (s->mode == MODE_HYBRID && s->hybrid_ratio) {
        if (++d->periods_since_pi >= s->hybrid_ratio * 8 && !hal_tx_pi_busy()) { d->periods_since_pi = 0; hal_tx_run_pi(&d->piseq); }
    }
    return event;
}

void detector_pi_record(detector_t *d, const pi_record_t *r)
{
    pi_result_t res; pi_process(&d->pi, r, &res);
    d->st.pi = res; d->st.pi_records++;
    d->st.pi_target = res.target;
    if (res.target) {
        /* PI depth: signal falls ~1/d^6 like VLF; reference set at ground grab time */
        /* PI depth from the 1/d^6 law against the PI reference (coin at 10 cm) stored in the coil record;
         * until the wizard has stored one, a nominal 20 mV reference is used */
        float ref; memcpy(&ref, d->coil.cal.reserved, sizeof ref); if (!(ref > 0)) ref = 20e-3f;
        d->st.pi_depth_cm = 10.0f * powf(ref / (fabsf(res.sig_early) + 1e-12f), 1.0f / 6.0f); if (d->st.pi_depth_cm > 60) d->st.pi_depth_cm = 60;
        if (d->set.mode == MODE_PI) audio_set(&d->audio, 300.0f + 500.0f * (res.snr > 20 ? 1.0f : res.snr / 20.0f), 0.3f + 0.7f * (res.snr > 20 ? 1.0f : res.snr / 20.0f));
    } else if (d->set.mode == MODE_PI) audio_set(&d->audio, 300.0f, 0.0f);
}

void detector_housekeeping(detector_t *d)
{
    status_t *st = &d->st;
    st->batt_v = hal_battery_v(); st->batt_pct = hal_battery_pct(); st->charging = hal_charging(); st->temp_c = hal_temp_c();
    /* run-time estimate: 5 Ah cell; average current by mode (VLF ~350 mA, hybrid/PI ~600 mA, plus 60 mA front light at full) */
    float i_ma = (d->set.mode == MODE_PI || d->set.mode == MODE_HYBRID) ? 600.0f : 350.0f; i_ma += 0.6f * (float)d->set.frontlight;
    st->batt_hours = (float)st->batt_pct / 100.0f * 5000.0f / i_ma;
    st->low_batt = st->batt_v < 3.45f && !st->charging;
    st->vtx_v = hal_tx_rail_v(); st->vbridge_v = hal_bridge_rail_v(); st->tx_fault = hal_tx_fault();
    hal_gnss_read(&st->gnss);
    st->uptime_s = hal_millis() / 1000;
}

void detector_save_settings(detector_t *d)
{
    d->set.crc = settings_crc(&d->set); hal_nvm_write(&d->set, sizeof(d->set)); d->settings_dirty = 0;
}

void detector_ground_grab(detector_t *d) { ground_grab(&d->ground, d->S_slow); d->set.gb_mode = 1; d->ground.mode = 1; pi_ground_grab(&d->pi, &d->st.pi); }
void detector_set_pinpoint(detector_t *d, int on) { d->st.pinpoint = on; if (on) { for (int k = 0; k < MDX_NLINES; k++) d->T_ref[k] = d->T_fast[k]; } }
void detector_learn_target(detector_t *d, const char *name)
{
    cpx Tm[MDX_NLINES]; for (int k = 0; k < MDX_NLINES; k++) Tm[k] = cpx_sub(d->T_fast[k], d->T_ref[k]);
    classify_learn(&d->cls, name, Tm, d->st.est.cond_id, d->st.est.fe_index);
}
