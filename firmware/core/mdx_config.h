/* MDX-1 firmware: fixed system constants shared by every module.
 * All timing derives from the 16.384 MHz crystal:
 *   ADC sample rate      512 000 Hz   (ADS127L01, OSR 32, wideband-1 FIR)
 *   TIM1 clock           229.376 MHz  -> 448 timer ticks per ADC sample
 *   TX period            M samples    (M = 100 nominal -> 5.12 kHz fundamental; 96..104 for noise cancel)
 *   TX waveform          80 slots per period, 3-level (+V, 0, -V)
 *   Demodulated lines    bins 1,2,3,4,8 of the M-point DFT  (5.12/10.24/15.36/20.48/40.96 kHz at M=100)
 */
#ifndef MDX_CONFIG_H
#define MDX_CONFIG_H
#include <stdint.h>

#define MDX_FS_HZ            512000.0f
#define MDX_TICKS_PER_SAMPLE 448
#define MDX_SLOTS            80
#define MDX_NLINES           5
#define MDX_NNOISE           5
#define MDX_M_DEFAULT        100
#define MDX_M_MIN            96
#define MDX_M_MAX            104
#define MDX_MAXM             112

/* ADC scaling: 24-bit two's complement, +-2.5 V differential full scale */
#define MDX_ADC_V_PER_CODE   (2.5f / 8388608.0f)
#define MDX_RX_GAIN          20.0f                 /* OPA1612 in-amp differential gain            */
#define MDX_ITX_V_PER_A      0.4f                  /* INA241A2 (20 V/V) x 20 mOhm shunt           */
#define MDX_RX_V_PER_CODE    (MDX_ADC_V_PER_CODE / MDX_RX_GAIN)     /* coil volts per RX code    */
#define MDX_ITX_A_PER_CODE   (MDX_ADC_V_PER_CODE / MDX_ITX_V_PER_A) /* amps per ITX code         */

/* TX rail (LT3757 #1) programmable range; DAC steers the feedback node */
#define MDX_VTX_MIN_V        7.9f
#define MDX_VTX_MAX_V        29.8f
#define MDX_VBRIDGE_PI_V     100.0f

/* Firmware identity */
#define MDX_FW_VERSION       "0.5.0"
#define MDX_FW_NAME          "MDX-1"

/* UI board: 0 = headless build (phone app only), 1 = always fitted, 2 = auto-detect (UI_DET pin) */
#ifndef MDX_UI_BOARD
#define MDX_UI_BOARD         2
#endif

static const int MDX_LINE_BIN[MDX_NLINES]  = {1, 2, 3, 4, 8};
static const int MDX_NOISE_BIN[MDX_NNOISE] = {5, 6, 7, 9, 10};

static inline float mdx_line_hz(int line, int M) { return MDX_FS_HZ * (float)MDX_LINE_BIN[line] / (float)M; }

#endif
