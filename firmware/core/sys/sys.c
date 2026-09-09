#include "sys.h"
#include <string.h>

/* audio cue patterns: pairs of (Hz, ms), terminated by 0 */
static const uint16_t CUE_BOOT[]     = {660, 90, 880, 90, 1100, 140, 0};
static const uint16_t CUE_LINK_UP[]  = {880, 70, 1320, 120, 0};
static const uint16_t CUE_LINK_DN[]  = {660, 160, 0};
static const uint16_t CUE_LOW_BATT[] = {440, 120, 0, 100, 440, 120, 0, 100, 440, 120, 0};
static const uint16_t CUE_OFF[]      = {1100, 80, 880, 80, 660, 160, 0};

void sys_init(sys_t *s, int cfg)
{
    memset(s, 0, sizeof(*s));
    s->ui_on = (cfg == 1) || (cfg == 2 && hal_ui_present());
}

int sys_tick(sys_t *s, detector_t *d, ui_t *u, const link_t *l, uint32_t now)
{
    if (!s->pwr_down && !s->last_authed && s->led_ms == 0) { s->led_ms = 1; if (!s->ui_on) audio_cue(&d->audio, CUE_BOOT); }
    if (s->ui_on) {
        ui_update(u, d, hal_keys(), now);
        ui_draw(u, d, now);
        hal_lcd_update((const uint8_t *)u->fb.px);
        hal_lcd_frontlight(d->set.frontlight);
    } else {
        /* headless: the only key that matters is POWER (main-board button); hold 1.5 s = off */
        uint32_t k = hal_keys();
        if (k & KEY_POWER) { if (!s->pwr_down) { s->pwr_down = 1; s->pwr_t0 = now; } else if (now - s->pwr_t0 >= 1500) { audio_cue(&d->audio, CUE_OFF); return 1; } }
        else s->pwr_down = 0;
        hal_lcd_frontlight(0);
        /* status LED: slow blink searching, solid while a phone is linked, fast blink on low battery */
        uint32_t period = d->st.low_batt ? 250 : 2000;
        hal_status_led((l && l->authed) ? 1 : (int)((now % period) < (d->st.low_batt ? 125u : 100u)));
    }
    /* cues shared by both modes */
    int authed = l ? l->authed : 0;
    if (authed != s->last_authed) { audio_cue(&d->audio, authed ? CUE_LINK_UP : CUE_LINK_DN); s->last_authed = authed; }
    if (d->st.low_batt && (!s->last_low_batt || now - s->low_batt_cue_ms > 60000)) { audio_cue(&d->audio, CUE_LOW_BATT); s->low_batt_cue_ms = now; }
    s->last_low_batt = d->st.low_batt;
    return 0;
}
