/* MDX-1 target main loop (STM32H743). See README.md for status. */
#include "stm32h7xx_hal.h"
#include "../core/det/detector.h"
#include "../core/ui/screens.h"
#include "../core/log/log.h"
#include "../core/link/link.h"
#include "../core/sys/sys.h"

void hal_stm32_init(void);          /* clocks, peripherals, DMA rings (hal_stm32.c) */
void hal_stm32_poll(void);          /* non-blocking housekeeping for the HAL (GNSS parser etc.) */

static detector_t det;
static ui_t ui;
static logger_t lg;
static link_t link;
static sys_t sys;

int main(void)
{
    hal_stm32_init();
    detector_init(&det);
    ui_init(&ui);
    log_init(&lg);
    link_init(&link, &det, &ui, &lg);
    sys_init(&sys, MDX_UI_BOARD);              /* UI board fitted, headless, or auto-detected */
    hal_analog_enable(1);
    hal_delay_ms(50);
    hal_tx_enable(1);

    uint32_t t_ui = 0, t_hk = 0, t_log = 0;
    adc_frame_t frame; pi_record_t rec; imu_sample_t imu = {0};
    int find_mark = 0;
    for (;;) {
        hal_stm32_poll();
        while (hal_adc_get_frame(&frame)) {
            if (sys.ui_on) ui_wizard_frame(&ui, &det, &frame);
            if (detector_period(&det, &frame, &imu)) {
                /* an accepted target held for > 1.5 s = "dig" mark for the log */
                if (det.st.accepted && det.hold_timer > 1.5f) find_mark = 1;
            }
        }
        while (hal_adc_get_pi_record(&rec)) detector_pi_record(&det, &rec);
        uint32_t now = hal_millis();
        if (now - t_ui >= 33) {
            t_ui = now;
            hal_imu_read(&imu);
            if (sys_tick(&sys, &det, &ui, &link, now)) { detector_save_settings(&det); hal_tx_enable(0); hal_delay_ms(400); hal_power_off(); }
        }
        if (now - t_hk >= 100) {
            t_hk = now;
            detector_housekeeping(&det);
            hal_audio_route(hal_headphone_present(), !hal_headphone_present(), 1);
            if (det.st.batt_v < 3.3f && !det.st.charging) { hal_tx_enable(0); hal_power_off(); }
        }
        if (ui.mark_find) { find_mark = 1; ui.mark_find = 0; }
        { uint8_t lb[64]; int ln; while ((ln = hal_link_read(lb, sizeof lb)) > 0) link_rx_bytes(&link, lb, ln); }
        link_tick(&link, now);
        if (link.mark_find) { find_mark = 1; link.mark_find = 0; }
        if (link.want_power_off) { hal_tx_enable(0); hal_power_off(); }
        if (now - t_log >= 100) { t_log = now; log_tick(&lg, &det, find_mark, now); det.st.finds = lg.finds; find_mark = 0; }
        __WFI();   /* sleep until the next DMA / timer interrupt */
    }
}
