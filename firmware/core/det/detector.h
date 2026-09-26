/* The detector engine: settings, per-period processing, mode scheduling, audio decisions.
 * Called with each demodulated period (VLF) and each PI record; publishes a status block
 * that the UI reads. */
#ifndef MDX_DETECTOR_H
#define MDX_DETECTOR_H
#include "../dsp/demod.h"
#include "../dsp/ground.h"
#include "../dsp/target.h"
#include "../dsp/noisecancel.h"
#include "../dsp/pi.h"
#include "../dsp/classify.h"
#include "../coil/coil.h"
#include "../audio/audio.h"

enum { MODE_MULTI = 0, MODE_SINGLE_5K, MODE_SINGLE_10K, MODE_SINGLE_20K, MODE_SINGLE_40K, MODE_PI, MODE_HYBRID, MODE_COUNT };
enum { PROG_PARK = 0, PROG_FIELD, PROG_BEACH, PROG_GOLD, PROG_RELIC, PROG_COUNT };

typedef struct {
    uint32_t magic;
    uint8_t  mode, program;
    uint8_t  sensitivity;        /* 1..30                                                  */
    uint8_t  recovery;           /* 1..8  -> averaging periods 128 >> (recovery-1)... see code */
    uint8_t  tone_scheme, volume, threshold_vol, iron_volume;
    uint8_t  gb_mode;            /* 0 auto 1 manual 2 off                                  */
    uint8_t  salt_mode;
    uint8_t  noise_M;            /* period length after noise cancel                       */
    uint8_t  tx_level;           /* 1..10 -> VTX 8..30 V                                   */
    uint8_t  fe_limit_lo, fe_limit_hi;   /* ferrous limits (0..100) rejected above lo... */
    uint8_t  hybrid_ratio;       /* VLF periods per PI burst (0 = never)                   */
    uint8_t  frontlight, log_enable, units_in;
    uint8_t  auto_nc_at_boot;    /* run noise cancel automatically 1 s after power-up          */
    uint8_t  sens_auto;          /* 1 = threshold follows the noise floor (auto sensitivity)   */
    uint8_t  iron_grunt;         /* 1 = ferrous targets get a modulated low tone               */
    uint8_t  ll_audio;           /* 1 = route wireless audio via the low-latency nRF puck (opt) */
    uint16_t link_pin;           /* phone-link pairing code 0..9999 (shown in the menu)        */
    uint8_t  accept[13];         /* 100-bit accept pattern over cond_id 0..99              */
    float    depth_cal;
    uint16_t crc;
} settings_t;

typedef struct {
    /* live target */
    int   target_present;        /* above threshold this period                            */
    int   accepted;              /* passes discrimination                                  */
    target_est_t est;            /* last estimate                                          */
    int   match_idx; float match_dist;
    float strength_db;           /* 20 log10(|T|/threshold)                                */
    /* live environment */
    float ground_phase_deg, ground_strength, salt_ratio, hotrock;
    float noise_level; int noise_M; int nc_active;
    float itx_rms_a, vtx_v, vbridge_v;
    int   clipped, tx_fault, coil_present;
    /* PI */
    pi_result_t pi; int pi_target; float pi_depth_cm;
    /* housekeeping */
    float batt_v; int batt_pct, charging; float temp_c;
    gnss_fix_t gnss;
    uint32_t periods, pi_records, uptime_s;
    float swing_speed;
    int   pinpoint; float pinpoint_level;
    /* trace for the 2-D map: last 24 (cond, fe) points */
    float trace_cond[24], trace_fe[24]; int trace_n, trace_head;
    /* five-line signature of the current target (|T_k| normalised to the largest) for the spectrum bars */
    float spectrum[MDX_NLINES];
    float batt_hours;            /* estimated run time left                                     */
    int   low_batt;              /* below 3.45 V                                                */
    int   nc_done_boot;
    uint32_t finds;

} status_t;

typedef struct {
    settings_t  set;
    status_t    st;
    demod_t     demod;
    ground_t    ground;
    target_model_t tmodel;
    noisecancel_t nc;
    pi_state_t  pi;
    classifier_t cls;
    coil_t      coil;
    audio_t     audio;
    tx_table_t  txtab;
    pi_seq_t    piseq;
    /* internal filters */
    cpx   S_slow[MDX_NLINES];    /* ground tracker input (tau ~ 0.5 s)                    */
    cpx   T_fast[MDX_NLINES];    /* recovery-speed averaged, ground balanced               */
    cpx   T_ref[MDX_NLINES];     /* motion reference (slower) for the high-pass            */
    float thresh;                /* detection threshold in ohm                            */
    float noise_floor;           /* rms of |T| when nothing is present                    */
    int   periods_since_pi;
    int   pi_pending;
    uint32_t last_ms;
    float hold_timer;            /* seconds the current target has been held for the UI   */
    float gate_timer;            /* seconds left during which ground tracking is frozen   */
    int   settings_dirty;        /* changed since last save                               */
    int   want_M;
} detector_t;

void detector_init(detector_t *d);
void detector_default_settings(settings_t *s);
void detector_apply_settings(detector_t *d);         /* after any settings change            */
void detector_load_program(detector_t *d, int prog);
int  detector_period(detector_t *d, const adc_frame_t *f, const imu_sample_t *imu);   /* returns 1 if a target event happened */
void detector_pi_record(detector_t *d, const pi_record_t *r);
void detector_housekeeping(detector_t *d);           /* battery, rails, gnss (call at ~10 Hz)*/
void detector_ground_grab(detector_t *d);
void detector_set_pinpoint(detector_t *d, int on);
void detector_learn_target(detector_t *d, const char *name);
void detector_save_settings(detector_t *d);
void ground_set_freqs(ground_t *g, const float f_hz[MDX_NLINES]);
int  detector_accept_id(const settings_t *s, float cond_id);
void detector_set_accept(settings_t *s, int id_lo, int id_hi, int accept);
uint16_t settings_crc(const settings_t *s);
#endif
