/* Two-component ground model across the five lines.
 *   G_k = a * g_mag_k + b * g_salt_k
 * g_mag : viscous-remanent-magnetisation response, nearly constant with frequency, small
 *         positive phase lag (real axis = magnetic/"ferrite" direction after coil rotation).
 * g_salt: conductive (salt water / wet sand) response, rises with frequency, quadrature.
 * a and b are tracked slowly and gated so that targets do not pull the balance. */
#ifndef MDX_GROUND_H
#define MDX_GROUND_H
#include "cpx.h"
#include "../mdx_config.h"

typedef struct {
    cpx   gmag[MDX_NLINES], gsalt[MDX_NLINES];
    float a, b;                 /* current model coefficients (ohm units)              */
    float a_grab, b_grab;       /* manual grab values                                  */
    int   mode;                 /* 0 auto-track, 1 manual (grabbed), 2 off             */
    float rate_slow;            /* tracking time constant when stationary (s)          */
    float rate_fast;            /* tracking time constant when covering ground (s)     */
    float phase_deg;            /* reported ground phase (for the display)             */
    float strength;             /* |G| at 5 kHz, for the mineralisation bar            */
    float salt_ratio;           /* b / (a + b)                                         */
    float f_hz[MDX_NLINES];
} ground_t;

void  ground_init(ground_t *g, const float f_hz[MDX_NLINES]);
void  ground_fit(const ground_t *g, const cpx S[MDX_NLINES], float *a, float *b);
/* S: slow-averaged, coil-corrected transfer impedance. dt seconds since last call.
 * speed: coil speed m/s from the IMU. target_present: 1 suppresses tracking. */
void  ground_update(ground_t *g, const cpx S[MDX_NLINES], float dt, float speed, int target_present);
void  ground_grab(ground_t *g, const cpx S[MDX_NLINES]);
void  ground_balance(const ground_t *g, const cpx S[MDX_NLINES], cpx T[MDX_NLINES]);
/* hot-rock channel: residual of the two-component fit, normalised */
float ground_misfit(const ground_t *g, const cpx S[MDX_NLINES]);
void  ground_set_freqs(ground_t *g, const float f_hz[MDX_NLINES]);   /* period change: keep a,b, update bases */
#endif
