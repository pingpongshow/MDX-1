/* Screen state machine: home, menu tree, pinpoint, coil wizard, noise cancel, diagnostics, learn. */
#ifndef MDX_SCREENS_H
#define MDX_SCREENS_H
#include "gfx.h"
#include "../det/detector.h"

enum { SCR_HOME = 0, SCR_MENU, SCR_PINPOINT, SCR_COIL_WIZARD, SCR_NOISE_CANCEL, SCR_DIAG, SCR_LEARN, SCR_LOG, SCR_CHARGE, SCR_OFF_CONFIRM };

typedef struct {
    int   screen;
    int   menu_idx, menu_top, editing;
    int   wizard_step; uint32_t wizard_t0; cpx wiz_air[MDX_NLINES]; cpx wiz_acc[MDX_NLINES]; int wiz_n;
    float wiz_L, wiz_R;
    int   learn_idx;
    uint32_t last_keys, key_t0, repeat_t, hold_t0;
    uint32_t menu_t0, pp_t0, gb_t; int menu_down, menu_long, pp_down, gb_active;   /* long-press synthesis: MENU->BACK, PINPOINT->ground grab */
    int   mark_find;             /* set by the log screen: main loop writes a find line */
    uint32_t last_draw_ms;
    int   dirty;
    fb_t  fb;
} ui_t;

void ui_init(ui_t *u);
/* process keys, run wizard logic, redraw when needed. Call at ~30 Hz. */
void ui_update(ui_t *u, detector_t *d, uint32_t keys, uint32_t now_ms);
void ui_draw(ui_t *u, const detector_t *d, uint32_t now_ms);   /* draws into u->fb */
/* feed raw frames while the coil wizard measures L/R (step 1) */
void ui_wizard_frame(ui_t *u, detector_t *d, const adc_frame_t *f);
void ui_wizard_advance(ui_t *u, detector_t *d);          /* same as RIGHT on the wizard screen */
/* menu item table, exposed for the phone link so the app builds the same menu */
typedef struct { const char *name; int lo, hi; int nlabels; const char *const *labels; int is_action; } ui_item_info_t;
int  ui_item_count(void);
int  ui_item_info(int idx, ui_item_info_t *out);          /* 1 ok, 0 bad index */
int  ui_item_get(const detector_t *d, int idx);           /* current value, 0 for actions */
int  ui_item_set(ui_t *u, detector_t *d, int idx, int v); /* 0 ok, 1 bad index, 2 bad value; applies settings; actions run */
#endif
