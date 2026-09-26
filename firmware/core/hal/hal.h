/* Hardware abstraction layer for the MDX-1 core.
 * Two implementations: sim/hal_sim.c (host, physics simulator) and stm32/hal_stm32.c (target).
 * The core never touches hardware directly; it only calls these functions. */
#ifndef MDX_HAL_H
#define MDX_HAL_H
#include <stdint.h>
#include <stddef.h>
#include "../mdx_config.h"

/* ---- acquisition ------------------------------------------------------------------ */
typedef struct {
    int32_t rx[MDX_MAXM];     /* ADC A codes, one transmit period                     */
    int32_t itx[MDX_MAXM];    /* ADC B codes, same samples                            */
    int     M;                /* samples in this period                               */
    uint32_t t_ms;            /* time stamp of first sample                           */
    uint32_t seq;             /* period counter (detects drops)                       */
} adc_frame_t;

/* PI-mode decay record: samples following one current reversal */
#define MDX_PI_MAX_SAMPLES 128
typedef struct {
    int32_t rx[MDX_PI_MAX_SAMPLES];
    int     n;
    int     polarity;         /* +1 / -1: direction of the reversal that preceded it */
    uint32_t t_ms;
} pi_record_t;

/* Returns 1 and fills *f when a complete period is available, 0 otherwise. Non-blocking. */
int  hal_adc_get_frame(adc_frame_t *f);
int  hal_adc_get_pi_record(pi_record_t *r);
void hal_adc_set_period(int M);           /* reconfigures the DMA frame length            */

/* ---- transmit ------------------------------------------------------------------- */
typedef struct {
    uint16_t ticks[MDX_SLOTS];  /* timer ARR per slot                                 */
    int8_t   level[MDX_SLOTS];  /* -1, 0, +1 bridge state per slot                    */
} tx_table_t;
typedef struct {
    float    rail_v;            /* bridge rail for PI (VBRIDGE), e.g. 100 V           */
    float    current_a;         /* regulated hold current                             */
    uint16_t reversal_us;       /* time allowed for the reversal                      */
    uint16_t hold_us;           /* receive/hold period after each reversal            */
    uint16_t sample_start_us;   /* first sample after reversal                        */
    uint16_t bursts;            /* reversals per PI frame                             */
    uint8_t  continuous;        /* 1 = re-arm forever (pure PI); 0 = one frame, then VLF */
} pi_seq_t;
void hal_tx_set_table(const tx_table_t *t);
void hal_tx_enable(int on);
void hal_tx_set_rail(float volts);         /* VTX set-point via DAC1                        */
void hal_tx_run_pi(const pi_seq_t *seq);   /* switches the bridge to a PI burst, then back  */
int  hal_tx_pi_busy(void);                 /* 1 while a PI burst is still running            */
void hal_tx_pi_stop(void);                 /* ends a burst early and hands back to VLF       */
float hal_tx_rail_v(void);                 /* measured VTX                                  */
float hal_bridge_rail_v(void);             /* measured VBRIDGE                              */
int  hal_tx_fault(void);                   /* TIM1 break latched?                           */
void hal_tx_clear_fault(void);

/* ---- user interface ------------------------------------------------------------- */
#define LCD_W 400
#define LCD_H 240
#define LCD_STRIDE (LCD_W / 8)
void hal_lcd_update(const uint8_t *fb);    /* 1 bpp, 400x240, row-major, MSB left           */
void hal_lcd_frontlight(int pct);
/* physical keys (6-key pad + pinpoint trigger): MENU UP DOWN LEFT RIGHT POWER + PINPOINT.
 * KEY_BACK and KEY_GB are virtual: the UI synthesises BACK from a long press of MENU (0.8 s)
 * and ground grab from PINPOINT held for 1.5 s. hal_keys() never returns them. */
enum { KEY_MENU = 1, KEY_UP = 2, KEY_DOWN = 4, KEY_LEFT = 8, KEY_RIGHT = 16, KEY_PINPOINT = 32, KEY_POWER = 64, KEY_BACK = 128, KEY_GB = 256 };
uint32_t hal_keys(void);                   /* bitmask of pressed keys                       */
int  hal_ui_present(void);                 /* UI_DET pin: UI board plugged in (pin 8 of the link grounded) */
void hal_status_led(int on);               /* main-board status LED (parallel to the UI board one)     */

/* ---- audio ---------------------------------------------------------------------- */
#define AUDIO_FS 32000
typedef void (*audio_fill_cb)(int16_t *buf, int n, void *ctx);
void hal_audio_start(audio_fill_cb cb, void *ctx);
void hal_audio_route(int headphone, int speaker, int bluetooth);
int  hal_headphone_present(void);

/* ---- optional low-latency wireless audio (nRF52840 daughterboard via J14) ----------
 * The BM83 keeps the BLE app link and standard-BT audio; this is an *optional* add-on
 * module that transmits the shared I2S stream to a matching receiver puck at low latency.
 * All calls are safe no-ops when the daughterboard is absent. */
int  hal_ll_audio_present(void);   /* daughterboard fitted / responding on the control link */
void hal_ll_audio_enable(int on);  /* enable/disable the low-latency audio TX link            */
void hal_ll_audio_pair(void);      /* begin pairing to a receiver puck                        */
int  hal_ll_audio_linked(void);    /* 1 = a puck is currently connected (NRF_STATUS)          */

/* ---- expansion header (generic I2C/SPI/UART/GPIO breakout for add-on boards) ----------
 * I2C4 (PF14/PF15), SPI2 (PB13 SCK / PB14 MISO / PB15 MOSI, CS on PG8), UART5 (PB6/PB12),
 * and two GPIO (idx 0 = PE13, 1 = PA15). Behind the HAL so add-on code stays portable. */
int  hal_exp_i2c(uint8_t addr7, const uint8_t *wr, int wn, uint8_t *rd, int rn); /* write-then-read; 1 ok */
int  hal_exp_spi(const uint8_t *tx, uint8_t *rx, int n);   /* full-duplex (tx/rx may be NULL); 1 ok        */
void hal_exp_spi_cs(int level);                            /* drive the SPI chip-select (PG8), 0/1         */
int  hal_exp_uart_write(const uint8_t *b, int n);          /* bytes written                                */
int  hal_exp_uart_read(uint8_t *b, int max);               /* bytes read (0..max)                          */
void hal_exp_gpio_set(int idx, int level);                 /* drives idx (0=PE13, 1=PA15) as an output     */
int  hal_exp_gpio_get(int idx);                            /* reads idx                                    */

/* ---- sensors / peripherals ------------------------------------------------------ */
typedef struct { float gx, gy, gz; float ax, ay, az; } imu_sample_t;   /* rad/s, m/s^2 */
int  hal_imu_read(imu_sample_t *s);
typedef struct { int valid; double lat, lon; float alt_m; float hdop; uint32_t utc_s; int sats; } gnss_fix_t;
int  hal_gnss_read(gnss_fix_t *fix);
float hal_battery_v(void);
int   hal_battery_pct(void);
int   hal_charging(void);
float hal_rail_v(int rail);                /* 0 VSYS 1 +3V3 2 +5VA 3 -5VA 4 +12V 5 VTX 6 VBRIDGE */
float hal_temp_c(void);

/* ---- storage -------------------------------------------------------------------- */
int  hal_nvm_read(void *buf, size_t len);          /* settings block in MCU flash             */
int  hal_nvm_write(const void *buf, size_t len);
int  hal_coil_eeprom_read(uint8_t buf[128]);       /* DS2431 in the coil, 1-Wire; 0 = no coil */
int  hal_coil_eeprom_write(const uint8_t buf[128]);
int  hal_sd_append(const char *path, const char *line);   /* 0 ok, <0 no card             */
int  hal_sd_present(void);

/* ---- time / power --------------------------------------------------------------- */
uint32_t hal_millis(void);
void hal_delay_ms(uint32_t ms);
void hal_power_off(void);
void hal_analog_enable(int on);            /* TPS65131 / LT3757 enables                     */
void hal_hv_enable(int on);                /* 100 V boost                                   */
void hal_hv_bleed(int on);
void hal_log(const char *fmt, ...);        /* debug console                                 */

#endif
