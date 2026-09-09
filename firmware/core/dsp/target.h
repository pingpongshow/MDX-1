/* Target identification from the ground-balanced five-line vector.
 * Model per line k (after coil rotation, real axis = magnetic):
 *   T_k = a * 1  +  b * M(f_k; zeta),      M(f) = -(j x)/(1 + j x),  x = f / zeta
 * a >= 0 : magnetic (ferrous) part, b >= 0 : eddy-current part, zeta : relaxation frequency.
 * zeta is found by a log grid search; a,b by 2x2 least squares with non-negativity.
 * Outputs a conductivity index 0..99 (log zeta), a ferrous index 0..100, confidence,
 * signal strength and a depth estimate calibrated against a reference coin. */
#ifndef MDX_TARGET_H
#define MDX_TARGET_H
#include "cpx.h"
#include "../mdx_config.h"

#define TARGET_ZETA_MIN_HZ   200.0f
#define TARGET_ZETA_MAX_HZ   500000.0f
#define TARGET_ZETA_STEPS    48

typedef struct {
    float zeta_hz;        /* relaxation frequency of the eddy response               */
    float a_mag, b_eddy;  /* fitted amplitudes (ohm)                                  */
    float strength;       /* |T| L2 norm over lines (ohm)                             */
    float cond_id;        /* 0..99, high = high conductivity (silver, copper)         */
    float fe_index;       /* 0 pure conductive .. 100 pure magnetic                   */
    float confidence;     /* 0..1, 1 = perfect model fit                              */
    float depth_cm;       /* estimate for a coin-sized target                         */
    int   ferrous;        /* classified ferrous                                       */
    int   valid;
} target_est_t;

typedef struct {
    float f_hz[MDX_NLINES];
    float zeta[TARGET_ZETA_STEPS];
    cpx   m[TARGET_ZETA_STEPS][MDX_NLINES];   /* precomputed eddy basis                 */
    float depth_ref_strength;                 /* |T| of the reference coin at depth_ref  */
    float depth_ref_cm;
    float fe_threshold;                       /* fe_index above which = ferrous          */
} target_model_t;

void  target_model_init(target_model_t *tm, const float f_hz[MDX_NLINES]);
void  target_fit(const target_model_t *tm, const cpx T[MDX_NLINES], target_est_t *e);
float target_cond_id_from_zeta(float zeta_hz);
#endif
