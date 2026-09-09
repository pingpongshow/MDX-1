/* Coil identity, calibration record (stored in the coil's DS2431 or in MCU NVM for coils
 * without a chip) and the measurement routines used by the coil wizard. */
#ifndef MDX_COIL_H
#define MDX_COIL_H
#include "../dsp/cpx.h"
#include "../hal/hal.h"

#define COIL_MAGIC 0x4D445831u  /* 'MDX1' */
enum { COIL_TYPE_UNKNOWN = 0, COIL_TYPE_DD = 1, COIL_TYPE_MONO = 2, COIL_TYPE_CONCENTRIC = 3 };

typedef struct {                /* exactly 128 bytes = DS2431 user memory                  */
    uint32_t magic;
    uint8_t  type;
    uint8_t  size_mm;           /* nominal diameter / 2 mm units? no: diameter in cm        */
    uint8_t  version;
    uint8_t  flags;             /* bit0: calibrated null, bit1: rotation, bit2: depth ref   */
    char     name[12];
    float    L_tx_mH, R_tx_ohm, L_rx_mH, R_rx_ohm;      /* 16                              */
    cpx      null[MDX_NLINES];  /* residual TX->RX coupling, ohm (40)                       */
    cpx      rot[MDX_NLINES];   /* unit phasors rotating S so magnetic = +real (40)         */
    float    depth_ref_strength;/* |T| of the reference coin at 10 cm (4)                   */
    uint16_t serial;
    uint8_t  reserved[4];
    uint16_t crc;
} coil_cal_t;

typedef struct {
    coil_cal_t cal;
    int   present;              /* coil detected (TX current flows)                          */
    int   has_chip;             /* DS2431 answered                                           */
    int   calibrated;
    float V_rail;               /* rail during the L/R measurement                           */
} coil_t;

uint16_t coil_crc16(const uint8_t *d, int n);
void coil_default(coil_cal_t *c);
int  coil_load(coil_t *c);                 /* read the chip (or NVM fallback); 0 if none      */
int  coil_store(coil_t *c);
/* L and R of the TX winding from one period of transmit current and the slot table.
 * di/dt = (V_slot - R i)/L within each +/-V slot; a regression over all such slots gives both. */
int  coil_measure_LR(const int32_t *itx, int M, const tx_table_t *t, float V_rail, float *L_H, float *R_ohm);
/* Apply the calibration: S_cal = (S - null) * rot */
void coil_apply(const coil_cal_t *c, const cpx S[MDX_NLINES], cpx out[MDX_NLINES]);
/* Wizard steps */
void coil_set_null(coil_cal_t *c, const cpx S_air[MDX_NLINES]);
void coil_set_rotation(coil_cal_t *c, const cpx S_ferrite[MDX_NLINES], const cpx S_air[MDX_NLINES]);
void coil_set_depth_ref(coil_cal_t *c, float strength_at_10cm);
/* Expected line current amplitude for diagnostics: I_k = V*rel_k / |R + j w L| */
float coil_expected_line_current(const coil_cal_t *c, float V, float f_hz, float rel);
#endif
