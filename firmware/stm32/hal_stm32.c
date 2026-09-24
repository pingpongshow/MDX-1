/* STM32H743 implementation of core/hal/hal.h.
 * NOT YET COMPILED OR TESTED ON HARDWARE - written against STM32CubeH7 HAL 1.11 API names.
 * Every peripheral block is self-contained so it can be brought up one at a time. */
#include "stm32h7xx_hal.h"
#include "../core/hal/hal.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ handles */
static TIM_HandleTypeDef  htim1, htim6, htim4;
static DMA_HandleTypeDef  hdma_tim1_up, hdma_sai_b, hdma_dac2;
static SAI_HandleTypeDef  hsai_b, hsai_a;
static DAC_HandleTypeDef  hdac;
static ADC_HandleTypeDef  hadc1;
static SPI_HandleTypeDef  hspi4;
static I2C_HandleTypeDef  hi2c1;
static UART_HandleTypeDef huart4, huart1;
static SD_HandleTypeDef   hsd1;

/* ------------------------------------------------------------------ ADC ring (SAI1_B TDM) */
#define RING_FRAMES (4 * MDX_MAXM)
static uint32_t sai_ring[RING_FRAMES * 2] __attribute__((section(".dma_buf"), aligned(32)));  /* slot A, slot B */
static volatile uint32_t ring_wr;          /* frames written by DMA (derived from NDTR)          */
static uint32_t ring_rd;                   /* frames consumed                                    */
static int period_M = MDX_M_DEFAULT;
static volatile uint32_t period_seq, ms_ticks;
static volatile int pi_mode, pi_burst, pi_bursts_total; static pi_seq_t pi_seq;
static volatile uint32_t pi_rec_start[64]; static volatile int pi_rec_wr, pi_rec_rd;

static inline int32_t sext24(uint32_t w) { w = (w >> 8) & 0xFFFFFF; return (int32_t)(w << 8) >> 8; }  /* 24 data bits left-justified in the 32-bit slot */

static uint32_t ring_written(void)
{
    uint32_t remaining = __HAL_DMA_GET_COUNTER(&hdma_sai_b);        /* words left in this cycle */
    uint32_t idx = (RING_FRAMES * 2 - remaining) / 2;                /* frames written this cycle */
    static uint32_t cycles; static uint32_t last_idx;
    if (idx < last_idx) cycles++;                                    /* wrapped */
    last_idx = idx;
    return cycles * RING_FRAMES + idx;
}

int hal_adc_get_frame(adc_frame_t *f)
{
    if (pi_mode) return 0;
    uint32_t wr = ring_written();
    if (wr - ring_rd < (uint32_t)period_M) return 0;
    if (wr - ring_rd > RING_FRAMES - 8) { ring_rd = wr - (wr % period_M); }   /* overrun: resync to a period boundary */
    SCB_InvalidateDCache_by_Addr((uint32_t *)sai_ring, sizeof sai_ring);
    for (int n = 0; n < period_M; n++) {
        uint32_t i = (ring_rd + n) % RING_FRAMES;
        f->rx[n]  = sext24(sai_ring[2 * i]);
        f->itx[n] = sext24(sai_ring[2 * i + 1]);
    }
    f->M = period_M; f->seq = period_seq++; f->t_ms = ms_ticks;
    ring_rd += period_M;
    return 1;
}

int hal_adc_get_pi_record(pi_record_t *r)
{
    if (pi_rec_rd == pi_rec_wr) return 0;
    uint32_t start = pi_rec_start[pi_rec_rd % 64];
    int n = (int)((uint32_t)pi_seq.hold_us * 512u / 1000u); if (n > MDX_PI_MAX_SAMPLES) n = MDX_PI_MAX_SAMPLES;
    if (ring_written() - start < (uint32_t)n) return 0;             /* not complete yet */
    SCB_InvalidateDCache_by_Addr((uint32_t *)sai_ring, sizeof sai_ring);
    for (int i = 0; i < n; i++) r->rx[i] = sext24(sai_ring[2 * ((start + i) % RING_FRAMES)]);
    r->n = n; r->polarity = (pi_rec_rd & 1) ? -1 : 1; r->t_ms = ms_ticks;
    pi_rec_rd++;
    return 1;
}

void hal_adc_set_period(int M) { period_M = M; ring_rd = ring_written(); ring_rd -= ring_rd % M; }

/* ------------------------------------------------------------------ TX: TIM1 + DMA burst table */
/* burst layout per slot: ARR, RCR, CCR1, CCR2 (contiguous registers from ARR) */
static uint16_t tx_burst[MDX_SLOTS * 4] __attribute__((section(".dma_buf"), aligned(32)));
static uint16_t pi_burst_tab[2 * 2 * 4] __attribute__((section(".dma_buf"), aligned(32)));

static void tx_load(const tx_table_t *t)
{
    for (int i = 0; i < MDX_SLOTS; i++) {
        uint16_t arr = (uint16_t)(t->ticks[i] - 1);
        tx_burst[4 * i + 0] = arr;
        tx_burst[4 * i + 1] = 0;
        tx_burst[4 * i + 2] = t->level[i] > 0 ? (uint16_t)(arr + 1) : 0;    /* CH1 = bridge A high side */
        tx_burst[4 * i + 3] = t->level[i] < 0 ? (uint16_t)(arr + 1) : 0;    /* CH2 = bridge B high side */
    }
    SCB_CleanDCache_by_Addr((uint32_t *)tx_burst, sizeof tx_burst);
}

void hal_tx_set_table(const tx_table_t *t)
{
    tx_load(t);
    /* restart the circular burst so slot 0 coincides with the next update; then resync the ADC ring */
    HAL_TIM_DMABurst_WriteStop(&htim1, TIM_DMA_UPDATE);
    HAL_TIM_DMABurst_MultiWriteStart(&htim1, TIM_DMABASE_ARR, TIM_DMA_UPDATE, (uint32_t *)tx_burst, TIM_DMABURSTLENGTH_4TRANSFERS, MDX_SLOTS * 4);
    __HAL_TIM_SET_COUNTER(&htim1, 0);
    hal_adc_set_period(period_M);
}

void hal_tx_enable(int on)
{
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_2, on ? GPIO_PIN_SET : GPIO_PIN_RESET);      /* DRV_EN */
    if (on) { __HAL_TIM_MOE_ENABLE(&htim1); HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1); HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1); HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2); HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2); }
    else { __HAL_TIM_MOE_DISABLE(&htim1); }
}

void hal_tx_set_rail(float volts)
{
    /* VTX = 29.8 V - 6.65 * VDAC  (feedback injection, see power sheet) */
    float vdac = (29.8f - volts) / 6.65f; if (vdac < 0) vdac = 0; if (vdac > 3.25f) vdac = 3.25f;
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, (uint32_t)(vdac / 3.3f * 4095.0f));
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_4, GPIO_PIN_SET);                              /* VTX_EN */
}

int hal_tx_pi_busy(void) { return pi_mode; }

/* End a PI burst early and hand the timer back to the VLF table (mirrors the ISR completion path). */
void hal_tx_pi_stop(void)
{
    if (!pi_mode) return;
    pi_mode = 0; pi_burst = 0;
    __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
    hal_hv_enable(0);
    HAL_TIM_DMABurst_WriteStop(&htim1, TIM_DMA_UPDATE);
    HAL_TIM_DMABurst_MultiWriteStart(&htim1, TIM_DMABASE_ARR, TIM_DMA_UPDATE, (uint32_t *)tx_burst, TIM_DMABURSTLENGTH_4TRANSFERS, MDX_SLOTS * 4);
    hal_adc_set_period(period_M);
}

void hal_tx_run_pi(const pi_seq_t *seq)
{
    pi_seq = *seq; pi_bursts_total = seq->bursts; pi_burst = 0;
    hal_hv_enable(1);
    /* two slots per reversal: [reversal +/-V for reversal_us][hold: both low-side on for hold_us]; polarity alternates via RCR trick: two entries */
    uint16_t rev = (uint16_t)((uint32_t)seq->reversal_us * 229376u / 1000u - 1), hold = (uint16_t)((uint32_t)seq->hold_us * 229376u / 1000u - 1);
    uint16_t *b = pi_burst_tab;
    b[0] = rev;  b[1] = 0; b[2] = rev + 1; b[3] = 0;      /* +V */
    b[4] = hold; b[5] = 0; b[6] = 0;       b[7] = 0;      /* hold */
    b[8] = rev;  b[9] = 0; b[10] = 0;      b[11] = rev + 1; /* -V */
    b[12] = hold; b[13] = 0; b[14] = 0;    b[15] = 0;     /* hold */
    SCB_CleanDCache_by_Addr((uint32_t *)pi_burst_tab, sizeof pi_burst_tab);
    pi_mode = 1;
    HAL_TIM_DMABurst_WriteStop(&htim1, TIM_DMA_UPDATE);
    HAL_TIM_DMABurst_MultiWriteStart(&htim1, TIM_DMABASE_ARR, TIM_DMA_UPDATE, (uint32_t *)pi_burst_tab, TIM_DMABURSTLENGTH_4TRANSFERS, 16);
    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
}

/* TIM1 update: in PI mode, mark record starts and count bursts; then hand back to the VLF table */
void TIM1_UP_IRQHandler(void)
{
    __HAL_TIM_CLEAR_IT(&htim1, TIM_IT_UPDATE);
    if (!pi_mode) return;
    static int phase;                  /* 0 = entering hold after +V, 1 = entering hold after -V */
    uint32_t wr = ring_written();
    if ((phase & 1) == 0) { pi_rec_start[pi_rec_wr % 64] = wr + 3; pi_rec_wr++; }   /* ~5 us after the reversal */
    phase++;
    if (phase >= 4) { phase = 0; if (++pi_burst >= pi_bursts_total) { if (pi_seq.continuous) { pi_burst = 0; return; } pi_mode = 0; __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE); hal_hv_enable(0); HAL_TIM_DMABurst_WriteStop(&htim1, TIM_DMA_UPDATE); HAL_TIM_DMABurst_MultiWriteStart(&htim1, TIM_DMABASE_ARR, TIM_DMA_UPDATE, (uint32_t *)tx_burst, TIM_DMABURSTLENGTH_4TRANSFERS, MDX_SLOTS * 4); hal_adc_set_period(period_M); } }
}

static float adc1_read(uint32_t ch)
{
    ADC_ChannelConfTypeDef c = {0}; c.Channel = ch; c.Rank = ADC_REGULAR_RANK_1; c.SamplingTime = ADC_SAMPLETIME_64CYCLES_5; c.SingleDiff = ADC_SINGLE_ENDED; c.OffsetNumber = ADC_OFFSET_NONE;
    HAL_ADC_ConfigChannel(&hadc1, &c); HAL_ADC_Start(&hadc1); HAL_ADC_PollForConversion(&hadc1, 2);
    uint32_t v = HAL_ADC_GetValue(&hadc1); HAL_ADC_Stop(&hadc1);
    return (float)v * 3.3f / 65535.0f;
}
float hal_tx_rail_v(void)     { return adc1_read(ADC_CHANNEL_16) * (442.0f + 47.0f) / 47.0f; }     /* PA0, 442k/47k + 10k series */
float hal_bridge_rail_v(void) { return adc1_read(ADC_CHANNEL_17) * (1000.0f + 22.0f) / 22.0f; }    /* PA1, 1M/22k */
int  hal_tx_fault(void)       { return (htim1.Instance->SR & TIM_SR_BIF) != 0; }
void hal_tx_clear_fault(void) { htim1.Instance->SR &= ~TIM_SR_BIF; __HAL_TIM_MOE_ENABLE(&htim1); }

/* ------------------------------------------------------------------ LCD: Sharp LS027B7DH01 over SPI4 */
static uint8_t vcom;
static uint8_t bitrev(uint8_t b) { b = (b & 0xF0) >> 4 | (b & 0x0F) << 4; b = (b & 0xCC) >> 2 | (b & 0x33) << 2; return (b & 0xAA) >> 1 | (b & 0x55) << 1; }
void hal_lcd_update(const uint8_t *fb)
{
    static uint8_t line[2 + LCD_STRIDE + 2];
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);      /* SCS active high */
    uint8_t cmd[1] = {(uint8_t)(0x80 | (vcom ? 0x40 : 0))};
    HAL_SPI_Transmit(&hspi4, cmd, 1, 10);
    for (int y = 0; y < LCD_H; y++) {
        line[0] = bitrev((uint8_t)(y + 1));                    /* line address, LSB first */
        for (int x = 0; x < LCD_STRIDE; x++) line[1 + x] = (uint8_t)~fb[y * LCD_STRIDE + x];   /* 1 = white on the panel */
        line[1 + LCD_STRIDE] = 0;
        HAL_SPI_Transmit(&hspi4, line, 2 + LCD_STRIDE, 20);
    }
    uint8_t trailer[1] = {0}; HAL_SPI_Transmit(&hspi4, trailer, 1, 10);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
}
void hal_lcd_frontlight(int pct) { __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, (uint32_t)(pct * 10)); }

int hal_ui_present(void) { return (GPIOG->IDR & GPIO_PIN_15) == 0; }        /* UI_DET: UI board grounds link pin 8 */
void hal_status_led(int on) { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, on ? GPIO_PIN_RESET : GPIO_PIN_SET); }   /* LED_STAT, active low */
uint32_t hal_keys(void)
{
    uint32_t k = 0; uint16_t g = GPIOG->IDR;
    if (!(g & GPIO_PIN_9))  k |= KEY_MENU;  if (!(g & GPIO_PIN_10)) k |= KEY_UP;    if (!(g & GPIO_PIN_11)) k |= KEY_DOWN;
    if (!(g & GPIO_PIN_12)) k |= KEY_LEFT;  if (!(g & GPIO_PIN_13)) k |= KEY_RIGHT; if (!(g & GPIO_PIN_14)) k |= KEY_PINPOINT;
    if (!(GPIOC->IDR & GPIO_PIN_13)) k |= KEY_POWER;   /* BACK and ground grab are long presses, synthesised in ui_update() */
    return k;
}

/* ------------------------------------------------------------------ audio: DAC2 + TIM6 + DMA */
static int16_t audio_buf[2 * 320] __attribute__((section(".dma_buf"), aligned(32)));
static audio_fill_cb audio_cb; static void *audio_ctx;
static void audio_fill_half(int half)
{
    int16_t tmp[320]; if (audio_cb) audio_cb(tmp, 320, audio_ctx); else memset(tmp, 0, sizeof tmp);
    for (int i = 0; i < 320; i++) audio_buf[half * 320 + i] = (int16_t)((tmp[i] >> 4) + 2048);   /* 12-bit unsigned around mid-rail */
    SCB_CleanDCache_by_Addr((uint32_t *)&audio_buf[half * 320], 640);
}
void HAL_DAC_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef *h) { (void)h; audio_fill_half(0); }
void HAL_DAC_ConvCpltCallbackCh2(DAC_HandleTypeDef *h) { (void)h; audio_fill_half(1); }
void hal_audio_start(audio_fill_cb cb, void *ctx)
{
    audio_cb = cb; audio_ctx = ctx; audio_fill_half(0); audio_fill_half(1);
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_2, (uint32_t *)audio_buf, 2 * 320, DAC_ALIGN_12B_R);
    HAL_TIM_Base_Start(&htim6);
}
void hal_audio_route(int hp, int spk, int bt) { HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, hp ? GPIO_PIN_SET : GPIO_PIN_RESET); HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, spk ? GPIO_PIN_SET : GPIO_PIN_RESET); (void)bt; }
int hal_headphone_present(void) { return HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_6) == GPIO_PIN_SET; }

/* ------------------------------------------------------------------ I2C sensors */
static int i2c_rd(uint8_t addr, uint8_t reg, uint8_t *buf, int n) { return HAL_I2C_Mem_Read(&hi2c1, addr << 1, reg, 1, buf, n, 5) == HAL_OK; }
static int i2c_wr(uint8_t addr, uint8_t reg, uint8_t v) { return HAL_I2C_Mem_Write(&hi2c1, addr << 1, reg, 1, &v, 1, 5) == HAL_OK; }
int hal_imu_read(imu_sample_t *s)
{
    uint8_t d[12]; if (!i2c_rd(0x6A, 0x22, d, 12)) return 0;      /* OUTX_L_G .. OUTZ_H_XL */
    int16_t g[3], a[3]; for (int i = 0; i < 3; i++) { g[i] = (int16_t)(d[2 * i] | d[2 * i + 1] << 8); a[i] = (int16_t)(d[6 + 2 * i] | d[7 + 2 * i] << 8); }
    const float gs = 500.0f / 32768.0f * 3.14159265f / 180.0f, as = 4.0f * 9.81f / 32768.0f;   /* +-500 dps, +-4 g */
    s->gx = g[0] * gs; s->gy = g[1] * gs; s->gz = g[2] * gs; s->ax = a[0] * as; s->ay = a[1] * as; s->az = a[2] * as;
    return 1;
}
float hal_battery_v(void) { uint8_t d[2]; if (!i2c_rd(0x55, 0x04, d, 2)) return adc1_read(ADC_CHANNEL_14) * 2.0f; return (d[0] | d[1] << 8) * 1e-3f; }
int  hal_battery_pct(void) { uint8_t d[2]; if (!i2c_rd(0x55, 0x1C, d, 2)) return 50; return d[0] | d[1] << 8; }
int  hal_charging(void)    { return HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_4) == GPIO_PIN_RESET; }   /* CHG_n low = charging */
float hal_rail_v(int r)
{
    switch (r) { case 0: return hal_battery_v(); case 1: return 3.3f; case 2: return 5.0f; case 3: return -5.0f; case 4: return 12.0f; case 5: return hal_tx_rail_v(); default: return hal_bridge_rail_v(); }
}
float hal_temp_c(void) { uint8_t d[2]; if (!i2c_rd(0x6A, 0x20, d, 2)) return 25.0f; return 25.0f + (int16_t)(d[0] | d[1] << 8) / 256.0f; }

/* ------------------------------------------------------------------ GNSS: UART4 NMEA (GGA/RMC) */
static char nmea[96]; static int nmea_n; static gnss_fix_t g_fix;
static double nmea_coord(const char *s, const char *hemi) { double v = atof(s); int deg = (int)(v / 100); double m = v - deg * 100; double r = deg + m / 60.0; return (*hemi == 'S' || *hemi == 'W') ? -r : r; }
static void nmea_line(char *l)
{
    char *f[20]; int n = 0; for (char *p = strtok(l, ","); p && n < 20; p = strtok(NULL, ",")) f[n++] = p;
    if (n > 9 && strstr(f[0], "GGA")) { g_fix.valid = atoi(f[6]) > 0; g_fix.sats = atoi(f[7]); g_fix.hdop = (float)atof(f[8]); g_fix.alt_m = (float)atof(f[9]); if (g_fix.valid) { g_fix.lat = nmea_coord(f[2], f[3]); g_fix.lon = nmea_coord(f[4], f[5]); } }
    if (n > 9 && strstr(f[0], "RMC") && f[1][0]) { int hh = (f[1][0] - '0') * 10 + f[1][1] - '0', mm = (f[1][2] - '0') * 10 + f[1][3] - '0', ss = (f[1][4] - '0') * 10 + f[1][5] - '0'; g_fix.utc_s = (uint32_t)(hh * 3600 + mm * 60 + ss); }
}
void bm83_rx_byte(uint8_t c);            /* bm83.c: Bluetooth module packet parser */
void bm83_init(void);                    /* bm83.c: module power-on and BLE advertising     */

void hal_stm32_poll(void)
{
    uint8_t c;
    while (HAL_UART_Receive(&huart4, &c, 1, 0) == HAL_OK) {          /* GNSS NMEA on UART4 */
        if (c == '\n') { nmea[nmea_n] = 0; if (nmea[0] == '$') nmea_line(nmea); nmea_n = 0; }
        else if (nmea_n < 95 && c != '\r') nmea[nmea_n++] = (char)c;
    }
    while (HAL_UART_Receive(&huart1, &c, 1, 0) == HAL_OK) bm83_rx_byte(c);   /* BM83 on USART1 */
}
int hal_gnss_read(gnss_fix_t *fix) { *fix = g_fix; return 1; }

/* ------------------------------------------------------------------ 1-Wire coil EEPROM (DS2431) on PB5 */
static void ow_delay_us(uint32_t us) { uint32_t t0 = DWT->CYCCNT; while (DWT->CYCCNT - t0 < us * (SystemCoreClock / 1000000u)) ; }
static void ow_low(int low) { GPIOB->BSRR = low ? (GPIO_PIN_5 << 16) : GPIO_PIN_5; }
static int ow_reset(void) { ow_low(1); ow_delay_us(480); ow_low(0); ow_delay_us(70); int p = !(GPIOB->IDR & GPIO_PIN_5); ow_delay_us(410); return p; }
static void ow_wbit(int b) { __disable_irq(); ow_low(1); ow_delay_us(b ? 6 : 60); ow_low(0); ow_delay_us(b ? 64 : 10); __enable_irq(); }
static int ow_rbit(void) { __disable_irq(); ow_low(1); ow_delay_us(6); ow_low(0); ow_delay_us(9); int b = (GPIOB->IDR & GPIO_PIN_5) != 0; ow_delay_us(55); __enable_irq(); return b; }
static void ow_wbyte(uint8_t v) { for (int i = 0; i < 8; i++) ow_wbit((v >> i) & 1); }
static uint8_t ow_rbyte(void) { uint8_t v = 0; for (int i = 0; i < 8; i++) v |= (uint8_t)(ow_rbit() << i); return v; }
int hal_coil_eeprom_read(uint8_t buf[128])
{
    if (!ow_reset()) return 0;
    ow_wbyte(0xCC); ow_wbyte(0xF0); ow_wbyte(0x00); ow_wbyte(0x00);          /* skip ROM, read memory from 0 */
    for (int i = 0; i < 128; i++) buf[i] = ow_rbyte();
    return 1;
}
int hal_coil_eeprom_write(const uint8_t buf[128])
{
    for (int row = 0; row < 16; row++) {                                         /* 8-byte scratchpad rows */
        if (!ow_reset()) return 0;
        ow_wbyte(0xCC); ow_wbyte(0x0F); ow_wbyte((uint8_t)(row * 8)); ow_wbyte(0x00);
        for (int i = 0; i < 8; i++) ow_wbyte(buf[row * 8 + i]);
        if (!ow_reset()) return 0;
        ow_wbyte(0xCC); ow_wbyte(0xAA); uint8_t ta1 = ow_rbyte(), ta2 = ow_rbyte(), es = ow_rbyte();   /* read scratchpad for auth */
        if (!ow_reset()) return 0;
        ow_wbyte(0xCC); ow_wbyte(0x55); ow_wbyte(ta1); ow_wbyte(ta2); ow_wbyte(es);  /* copy scratchpad */
        HAL_Delay(13);
    }
    return 1;
}

/* ------------------------------------------------------------------ NVM: last flash sector */
#define NVM_ADDR 0x081E0000u
int hal_nvm_read(void *buf, size_t len) { memcpy(buf, (const void *)NVM_ADDR, len); return 1; }
int hal_nvm_write(const void *buf, size_t len)
{
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef e = {0}; uint32_t err; e.TypeErase = FLASH_TYPEERASE_SECTORS; e.Banks = FLASH_BANK_2; e.Sector = FLASH_SECTOR_7; e.NbSectors = 1; e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&e, &err) != HAL_OK) { HAL_FLASH_Lock(); return 0; }
    uint8_t word[32] __attribute__((aligned(32)));
    for (size_t off = 0; off < len; off += 32) { memset(word, 0xFF, 32); memcpy(word, (const uint8_t *)buf + off, len - off < 32 ? len - off : 32); if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, NVM_ADDR + off, (uint32_t)word) != HAL_OK) { HAL_FLASH_Lock(); return 0; } }
    HAL_FLASH_Lock(); return 1;
}

/* ------------------------------------------------------------------ SD card: FatFs */
#ifdef MDX_USE_FATFS
#include "ff.h"
static FATFS fs; static int sd_mounted;
int hal_sd_present(void) { return HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_7) == GPIO_PIN_RESET; }
int hal_sd_append(const char *path, const char *line)
{
    if (!hal_sd_present()) return -1;
    if (!sd_mounted) { if (f_mount(&fs, "", 1) != FR_OK) return -2; f_mkdir("MDX1"); sd_mounted = 1; }
    FIL f; if (f_open(&f, path, FA_OPEN_APPEND | FA_WRITE) != FR_OK) return -3;
    UINT w; f_write(&f, line, (UINT)strlen(line), &w); f_close(&f); return 0;
}
#else
int hal_sd_present(void) { return 0; }
int hal_sd_append(const char *path, const char *line) { (void)path; (void)line; return -1; }
#endif

/* ------------------------------------------------------------------ time / power / misc */
void SysTick_Handler(void) { HAL_IncTick(); ms_ticks++; static int vc; if (++vc >= 500) { vc = 0; HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_11); vcom ^= 1; } }
uint32_t hal_millis(void) { return ms_ticks; }
void hal_delay_ms(uint32_t ms) { HAL_Delay(ms); }
void hal_power_off(void) { hal_tx_enable(0); hal_analog_enable(0); HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN4_LOW);   /* PC13 = WKUP4 per DS12110 table 9 */ HAL_PWR_EnterSTANDBYMode(); }
void hal_analog_enable(int on) { HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void hal_hv_enable(int on) { HAL_GPIO_WritePin(GPIOG, GPIO_PIN_3, on ? GPIO_PIN_SET : GPIO_PIN_RESET); if (!on) { hal_hv_bleed(1); HAL_Delay(600); hal_hv_bleed(0); } }
void hal_hv_bleed(int on) { HAL_GPIO_WritePin(GPIOG, GPIO_PIN_5, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void hal_log(const char *fmt, ...) { char b[128]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap); for (int i = 0; i < n; i++) ITM_SendChar((uint32_t)b[i]); }

/* ------------------------------------------------------------------ initialisation */
static void clocks_init(void)
{
    RCC_OscInitTypeDef o = {0}; RCC_ClkInitTypeDef c = {0}; RCC_PeriphCLKInitTypeDef p = {0};
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) ;
    o.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_HSI48;
    o.HSEState = RCC_HSE_ON; o.LSEState = RCC_LSE_ON; o.HSI48State = RCC_HSI48_ON;
    o.PLL.PLLState = RCC_PLL_ON; o.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    o.PLL.PLLM = 1; o.PLL.PLLN = 56; o.PLL.PLLP = 2; o.PLL.PLLQ = 8; o.PLL.PLLR = 2;      /* 16.384 * 56 = 917.5 MHz VCO, /2 = 458.752 MHz */
    o.PLL.PLLRGE = RCC_PLL1VCIRANGE_3; o.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE; o.PLL.PLLFRACN = 0;
    HAL_RCC_OscConfig(&o);
    c.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    c.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; c.SYSCLKDivider = RCC_SYSCLK_DIV1; c.AHBCLKDivider = RCC_HCLK_DIV2;
    c.APB3CLKDivider = RCC_APB3_DIV2; c.APB1CLKDivider = RCC_APB1_DIV2; c.APB2CLKDivider = RCC_APB2_DIV2; c.APB4CLKDivider = RCC_APB4_DIV2;
    HAL_RCC_ClockConfig(&c, FLASH_LATENCY_4);
    /* PLL2: SAI1 32.768 MHz = 16.384 x 60 / 30 */
    p.PeriphClockSelection = RCC_PERIPHCLK_SAI1 | RCC_PERIPHCLK_ADC | RCC_PERIPHCLK_USB | RCC_PERIPHCLK_SDMMC | RCC_PERIPHCLK_UART4 | RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_SPI4;
    p.PLL2.PLL2M = 1; p.PLL2.PLL2N = 60; p.PLL2.PLL2P = 30; p.PLL2.PLL2Q = 12; p.PLL2.PLL2R = 12; p.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3; p.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE; p.PLL2.PLL2FRACN = 0;
    p.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL2; p.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2; p.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    p.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL; p.Uart4ClockSelection = RCC_UART4CLKSOURCE_D2PCLK1; p.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2; p.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1; p.Spi45ClockSelection = RCC_SPI45CLKSOURCE_D2PCLK1;
    HAL_RCCEx_PeriphCLKConfig(&p);
    HAL_RCC_MCOConfig(RCC_MCO2, RCC_MCO2SOURCE_HSE, RCC_MCODIV_1);       /* PC9 -> ADC CLK 16.384 MHz */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
}

static void gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE(); __HAL_RCC_GPIOE_CLK_ENABLE(); __HAL_RCC_GPIOF_CLK_ENABLE(); __HAL_RCC_GPIOG_CLK_ENABLE(); __HAL_RCC_GPIOH_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_OUTPUT_PP; g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6; HAL_GPIO_Init(GPIOG, &g);            /* DRV_EN HV_EN VTX_EN HV_BLEED ANA_EN */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3; HAL_GPIO_Init(GPIOF, &g);                          /* ADC straps: FILTER 00, OSR 00 */
    g.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_14 | GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_5; HAL_GPIO_Init(GPIOD, &g);   /* ADC START RESET, DISP, SPK_SD, HP_EN, GNSS_RST */
    g.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_0 | GPIO_PIN_1; HAL_GPIO_Init(GPIOB, &g);                        /* DISP_CS EXTCOM LEDs */
    g.Pin = GPIO_PIN_3 | GPIO_PIN_7; HAL_GPIO_Init(GPIOE, &g);                                                    /* BT_EN BT_RST */
    g.Mode = GPIO_MODE_OUTPUT_OD; g.Pin = GPIO_PIN_5; HAL_GPIO_Init(GPIOB, &g); GPIOB->BSRR = GPIO_PIN_5;           /* 1-Wire */
    g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;
    g.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15; HAL_GPIO_Init(GPIOG, &g);   /* keys: MENU UP DOWN LEFT RIGHT PINPOINT; PG15 = UI_DET */
    g.Pin = GPIO_PIN_13; HAL_GPIO_Init(GPIOC, &g);                                                                /* POWER (WKUP4) */
    g.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_10; HAL_GPIO_Init(GPIOF, &g);                                      /* CHG PGOOD BAT_INT */
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7; HAL_GPIO_Init(GPIOD, &g);                                                    /* HP_DET SD_DET */
    g.Pull = GPIO_PULLDOWN; g.Pin = GPIO_PIN_1; HAL_GPIO_Init(GPIOE, &g); g.Pull = GPIO_PULLUP;                   /* BT_WAKE: BM83 P0_0/UART_TX_IND, active high */
    /* alternate functions */
    g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF1_TIM1; g.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_15; HAL_GPIO_Init(GPIOE, &g);
    g.Alternate = GPIO_AF0_MCO; g.Pin = GPIO_PIN_9; HAL_GPIO_Init(GPIOC, &g);
    g.Alternate = GPIO_AF6_SAI1; g.Pin = GPIO_PIN_6 | GPIO_PIN_8 | GPIO_PIN_9; HAL_GPIO_Init(GPIOF, &g);         /* SAI1_B: SD, SCK, FS */
    g.Alternate = GPIO_AF6_SAI1; g.Pin = GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6; HAL_GPIO_Init(GPIOE, &g);   /* SAI1_A -> BT */
    g.Alternate = GPIO_AF5_SPI4; g.Pin = GPIO_PIN_12 | GPIO_PIN_14; HAL_GPIO_Init(GPIOE, &g);
    g.Alternate = GPIO_AF4_I2C1; g.Mode = GPIO_MODE_AF_OD; g.Pin = GPIO_PIN_8 | GPIO_PIN_9; HAL_GPIO_Init(GPIOB, &g);
    g.Mode = GPIO_MODE_AF_PP; g.Alternate = GPIO_AF8_UART4; g.Pin = GPIO_PIN_10 | GPIO_PIN_11; HAL_GPIO_Init(GPIOC, &g);
    g.Alternate = GPIO_AF7_USART1; g.Pin = GPIO_PIN_9 | GPIO_PIN_10; HAL_GPIO_Init(GPIOA, &g);
    g.Alternate = GPIO_AF2_TIM4; g.Pin = GPIO_PIN_15; HAL_GPIO_Init(GPIOD, &g);
    g.Mode = GPIO_MODE_ANALOG; g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5; HAL_GPIO_Init(GPIOA, &g);
}

static void tim1_init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE();
    htim1.Instance = TIM1; htim1.Init.Prescaler = 0; htim1.Init.CounterMode = TIM_COUNTERMODE_UP; htim1.Init.Period = 559; htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0; htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim1);
    TIM_OC_InitTypeDef oc = {0}; oc.OCMode = TIM_OCMODE_PWM1; oc.Pulse = 0; oc.OCPolarity = TIM_OCPOLARITY_HIGH; oc.OCNPolarity = TIM_OCNPOLARITY_HIGH; oc.OCIdleState = TIM_OCIDLESTATE_RESET; oc.OCNIdleState = TIM_OCNIDLESTATE_RESET; oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1); HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2);
    TIM_BreakDeadTimeConfigTypeDef bd = {0}; bd.OffStateRunMode = TIM_OSSR_ENABLE; bd.OffStateIDLEMode = TIM_OSSI_ENABLE; bd.LockLevel = TIM_LOCKLEVEL_OFF;
    bd.DeadTime = 23;                                                        /* 100 ns at 229 MHz */
    bd.BreakState = TIM_BREAK_ENABLE; bd.BreakPolarity = TIM_BREAKPOLARITY_HIGH; bd.BreakFilter = 0; bd.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&htim1, &bd);
    hdma_tim1_up.Instance = DMA1_Stream0; hdma_tim1_up.Init.Request = DMA_REQUEST_TIM1_UP; hdma_tim1_up.Init.Direction = DMA_MEMORY_TO_PERIPH; hdma_tim1_up.Init.PeriphInc = DMA_PINC_DISABLE; hdma_tim1_up.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim1_up.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD; hdma_tim1_up.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD; hdma_tim1_up.Init.Mode = DMA_CIRCULAR; hdma_tim1_up.Init.Priority = DMA_PRIORITY_VERY_HIGH; hdma_tim1_up.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_tim1_up); __HAL_LINKDMA(&htim1, hdma[TIM_DMA_ID_UPDATE], hdma_tim1_up);
    HAL_NVIC_SetPriority(TIM1_UP_IRQn, 1, 0); HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
}

static void sai_adc_init(void)
{
    __HAL_RCC_SAI1_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE();
    hsai_b.Instance = SAI1_Block_B;
    hsai_b.Init.AudioMode = SAI_MODEMASTER_RX; hsai_b.Init.Synchro = SAI_ASYNCHRONOUS; hsai_b.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE; hsai_b.Init.NoDivider = SAI_MASTERDIVIDER_DISABLE;
    hsai_b.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_HF; hsai_b.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV; hsai_b.Init.Mckdiv = 0;   /* bit clock = SAI kernel clock 32.768 MHz directly */
    hsai_b.Init.MonoStereoMode = SAI_STEREOMODE; hsai_b.Init.CompandingMode = SAI_NOCOMPANDING; hsai_b.Init.Protocol = SAI_FREE_PROTOCOL; hsai_b.Init.DataSize = SAI_DATASIZE_32; hsai_b.Init.FirstBit = SAI_FIRSTBIT_MSB; hsai_b.Init.ClockStrobing = SAI_CLOCKSTROBING_RISINGEDGE;
    hsai_b.FrameInit.FrameLength = 64; hsai_b.FrameInit.ActiveFrameLength = 1; hsai_b.FrameInit.FSDefinition = SAI_FS_STARTFRAME; hsai_b.FrameInit.FSPolarity = SAI_FS_ACTIVE_HIGH; hsai_b.FrameInit.FSOffset = SAI_FS_FIRSTBIT;
    hsai_b.SlotInit.FirstBitOffset = 0; hsai_b.SlotInit.SlotSize = SAI_SLOTSIZE_32B; hsai_b.SlotInit.SlotNumber = 2; hsai_b.SlotInit.SlotActive = SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_1;
    HAL_SAI_Init(&hsai_b);
    hdma_sai_b.Instance = DMA1_Stream1; hdma_sai_b.Init.Request = DMA_REQUEST_SAI1_B; hdma_sai_b.Init.Direction = DMA_PERIPH_TO_MEMORY; hdma_sai_b.Init.PeriphInc = DMA_PINC_DISABLE; hdma_sai_b.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai_b.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD; hdma_sai_b.Init.MemDataAlignment = DMA_MDATAALIGN_WORD; hdma_sai_b.Init.Mode = DMA_CIRCULAR; hdma_sai_b.Init.Priority = DMA_PRIORITY_VERY_HIGH; hdma_sai_b.Init.FIFOMode = DMA_FIFOMODE_ENABLE; hdma_sai_b.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_HALFFULL; hdma_sai_b.Init.MemBurst = DMA_MBURST_INC4; hdma_sai_b.Init.PeriphBurst = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&hdma_sai_b); __HAL_LINKDMA(&hsai_b, hdmarx, hdma_sai_b);
    /* ADS127L01: reset, straps, start */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET); HAL_Delay(1); HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET); HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, GPIO_PIN_RESET);   /* wideband-1, OSR 32 */
    HAL_SAI_Receive_DMA(&hsai_b, (uint8_t *)sai_ring, RING_FRAMES * 2);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);                                      /* START */
}

static void misc_init(void)
{
    __HAL_RCC_DAC12_CLK_ENABLE(); __HAL_RCC_TIM6_CLK_ENABLE(); __HAL_RCC_TIM4_CLK_ENABLE(); __HAL_RCC_ADC12_CLK_ENABLE(); __HAL_RCC_SPI4_CLK_ENABLE(); __HAL_RCC_I2C1_CLK_ENABLE(); __HAL_RCC_UART4_CLK_ENABLE(); __HAL_RCC_USART1_CLK_ENABLE();
    hdac.Instance = DAC1; HAL_DAC_Init(&hdac);
    DAC_ChannelConfTypeDef dc = {0}; dc.DAC_Trigger = DAC_TRIGGER_NONE; dc.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE; HAL_DAC_ConfigChannel(&hdac, &dc, DAC_CHANNEL_1); HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
    dc.DAC_Trigger = DAC_TRIGGER_T6_TRGO; HAL_DAC_ConfigChannel(&hdac, &dc, DAC_CHANNEL_2);
    htim6.Instance = TIM6; htim6.Init.Prescaler = 0; htim6.Init.Period = 7167; htim6.Init.CounterMode = TIM_COUNTERMODE_UP; HAL_TIM_Base_Init(&htim6);   /* 229.376 MHz / 7168 = 32 kHz */
    TIM_MasterConfigTypeDef m = {0}; m.MasterOutputTrigger = TIM_TRGO_UPDATE; HAL_TIMEx_MasterConfigSynchronization(&htim6, &m);
    hdma_dac2.Instance = DMA1_Stream2; hdma_dac2.Init.Request = DMA_REQUEST_DAC1_CH2; hdma_dac2.Init.Direction = DMA_MEMORY_TO_PERIPH; hdma_dac2.Init.MemInc = DMA_MINC_ENABLE; hdma_dac2.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD; hdma_dac2.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD; hdma_dac2.Init.Mode = DMA_CIRCULAR; hdma_dac2.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_dac2); __HAL_LINKDMA(&hdac, DMA_Handle2, hdma_dac2);
    htim4.Instance = TIM4; htim4.Init.Prescaler = 228; htim4.Init.Period = 999; htim4.Init.CounterMode = TIM_COUNTERMODE_UP; HAL_TIM_PWM_Init(&htim4);
    TIM_OC_InitTypeDef oc = {0}; oc.OCMode = TIM_OCMODE_PWM1; oc.Pulse = 300; oc.OCPolarity = TIM_OCPOLARITY_HIGH; HAL_TIM_PWM_ConfigChannel(&htim4, &oc, TIM_CHANNEL_4); HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);
    hadc1.Instance = ADC1; hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV8; hadc1.Init.Resolution = ADC_RESOLUTION_16B; hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE; hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV; hadc1.Init.ContinuousConvMode = DISABLE; hadc1.Init.NbrOfConversion = 1; hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR; hadc1.Init.OversamplingMode = DISABLE;
    HAL_ADC_Init(&hadc1); HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
    hspi4.Instance = SPI4; hspi4.Init.Mode = SPI_MODE_MASTER; hspi4.Init.Direction = SPI_DIRECTION_2LINES_TXONLY; hspi4.Init.DataSize = SPI_DATASIZE_8BIT; hspi4.Init.CLKPolarity = SPI_POLARITY_LOW; hspi4.Init.CLKPhase = SPI_PHASE_1EDGE; hspi4.Init.NSS = SPI_NSS_SOFT; hspi4.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64; hspi4.Init.FirstBit = SPI_FIRSTBIT_LSB;   /* Sharp LCD: ~1.8 MHz, LSB first */
    HAL_SPI_Init(&hspi4);
    hi2c1.Instance = I2C1; hi2c1.Init.Timing = 0x10707DBC; hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT; HAL_I2C_Init(&hi2c1);
    huart4.Instance = UART4; huart4.Init.BaudRate = 9600; huart4.Init.WordLength = UART_WORDLENGTH_8B; huart4.Init.StopBits = UART_STOPBITS_1; huart4.Init.Parity = UART_PARITY_NONE; huart4.Init.Mode = UART_MODE_TX_RX; HAL_UART_Init(&huart4);
    huart1.Instance = USART1; huart1.Init.BaudRate = 115200; huart1.Init.WordLength = UART_WORDLENGTH_8B; huart1.Init.StopBits = UART_STOPBITS_1; huart1.Init.Parity = UART_PARITY_NONE; huart1.Init.Mode = UART_MODE_TX_RX; HAL_UART_Init(&huart1);
    bm83_init();                      /* power up the Bluetooth module and start BLE advertising */
    /* IMU LSM6DSO32 (WHO_AM_I 0x6C): 104 Hz, +-500 dps, +-4 g.
     * NB: FS_XL encoding differs from the LSM6DSM - on the DSO32, CTRL1_XL FS_XL 00 = +-4 g
     * (DSM used 10 = +-4 g). CTRL2_G is unchanged (01 = +-500 dps). Scaling in hal_imu_read
     * stays +-4 g / +-500 dps. */
    i2c_wr(0x6A, 0x10, 0x40); i2c_wr(0x6A, 0x11, 0x44);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);      /* LCD DISP on */
    (void)hsai_a; (void)hsd1;
}

void hal_stm32_init(void)
{
    SCB_EnableICache(); SCB_EnableDCache();
    HAL_Init();
    clocks_init(); gpio_init(); tim1_init(); sai_adc_init(); misc_init();
}
