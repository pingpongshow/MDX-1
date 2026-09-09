/* Physics simulator driving the HAL on the host. */
#ifndef MDX_SIM_H
#define MDX_SIM_H
#include "../core/hal/hal.h"
#include "../core/dsp/cpx.h"

#define SIM_MAX_TARGETS 4
typedef struct {
    float x_m;          /* position along the sweep axis                      */
    float depth_m;      /* below the coil plane                               */
    float zeta_hz;      /* eddy relaxation frequency                          */
    float eddy;         /* eddy strength (ohm) at 10 cm directly under coil    */
    float mag;          /* magnetic strength (ohm) at 10 cm                   */
    int   active;
} sim_target_t;

typedef struct {
    /* electrical */
    float L_tx_H, R_tx_ohm, V_rail;
    cpx   null[MDX_NLINES];      /* residual coupling (ohm) in the coil's own frame         */
    float phys_rot_deg[MDX_NLINES];   /* the coil's phase per line that the wizard must find  */
    /* environment */
    float g_mag, g_salt;         /* ohm at 10.24 kHz reference                              */
    float noise_v_rms;           /* white noise at the coil (V)                             */
    float interf_hz, interf_v;   /* one CW interferer                                       */
    sim_target_t tgt[SIM_MAX_TARGETS];
    /* sweep */
    float sweep_amp_m, sweep_period_s;
    int   sweeping;
    float coil_x_m;              /* explicit position when not sweeping                     */
    /* PI */
    float pi_ground;             /* viscous ground amplitude for PI (V at 10 us)            */
    /* state */
    double t_s;
    uint32_t seq;
    int   M;
    tx_table_t txtab;
    int   tx_on;
    float rail_set;
    float i_state;               /* RL integrator state                                     */
    /* peripherals */
    uint32_t keys;
    uint8_t  eeprom[128]; int eeprom_present;
    uint8_t  nvm[256]; int nvm_valid;
    float batt_v; int charging; int gnss_valid; double lat, lon;
    int   pi_pending; pi_seq_t piseq; int pi_burst_idx; int pi_avail;   /* records still to hand out this frame */
    unsigned rng;
    /* phone link loopback: bytes the phone sent (to_det) and bytes the detector sent (from_det) */
    int ui_present, lcd_updates, led_on;
    uint8_t link_to_det[1024]; int link_to_n, link_to_rd; uint8_t link_from_det[8192]; int link_from_n; int link_connected;
} sim_t;

extern sim_t SIM;
void sim_reset(void);
void sim_add_target(float x, float depth, float zeta, float eddy, float mag);
void sim_line_S(cpx S[MDX_NLINES], float coil_x);       /* physical S per line (ohm)          */
float sim_coil_x(void);
float sim_coil_speed(void);
void sim_set_time(double t);
void sim_link_phone_send(const uint8_t *b, int n);      /* phone -> detector */
int  sim_link_phone_take(uint8_t *b, int max);          /* detector -> phone (drains) */
#endif
