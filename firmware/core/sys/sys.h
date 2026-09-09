/* System supervisor: decides whether the UI board (LCD + keypad) is driven or the detector runs
 * headless (phone app + headphones/Bluetooth + the main-board power button), and owns the few
 * things that differ between the two: power-key handling, status LED and audio cues.
 *
 * MDX_UI_BOARD (mdx_config.h / -D at build time):
 *   0  headless build: never touch the LCD or keypad, even if a UI board is plugged in
 *   1  UI board always present (skip the detect pin)
 *   2  auto: read the UI_DET pin at boot (pin 8 of the UI link is grounded on the UI board)   */
#ifndef MDX_SYS_H
#define MDX_SYS_H
#include "../det/detector.h"
#include "../ui/screens.h"
#include "../link/link.h"

typedef struct {
    int ui_on;                    /* 1 = drive LCD/keypad, 0 = headless                           */
    uint32_t pwr_t0; int pwr_down;
    int last_authed, last_low_batt; uint32_t low_batt_cue_ms;
    uint32_t led_ms;
} sys_t;

void sys_init(sys_t *s, int ui_board_cfg);
/* Call at the UI rate (~30 Hz). Runs the UI when fitted; otherwise handles the power key and
 * audio/LED cues. Returns 1 when the user asked to power off (hold POWER 1.5 s).               */
int  sys_tick(sys_t *s, detector_t *d, ui_t *u, const link_t *l, uint32_t now_ms);
#endif
