#include "audio.h"
#include <math.h>
#include <string.h>

void audio_init(audio_t *a, int fs) { memset(a, 0, sizeof(*a)); a->fs = fs; a->master = 0.6f; a->freq_hz = a->target_freq = 600; }
void audio_set(audio_t *a, float f, float v) { a->target_freq = f; a->target_vol = v < 0 ? 0 : (v > 1 ? 1 : v); }
void audio_set_threshold(audio_t *a, float f, float v) { a->threshold_hz = f; a->threshold_vol = v; }
void audio_set_grunt(audio_t *a, int on) { a->grunt = on; }
void audio_cue(audio_t *a, const uint16_t *seq) { a->cue = seq; a->cue_i = 0; a->cue_left = 0; }
int  audio_cue_active(const audio_t *a) { return a->cue != 0; }

void audio_fill(int16_t *buf, int n, void *ctx)
{
    audio_t *a = (audio_t *)ctx;
    float ka = 1.0f / (0.002f * (float)a->fs);         /* 2 ms attack: response stays crisp     */
    float kr = 1.0f / (0.020f * (float)a->fs);         /* 20 ms release: no click on target loss */
    for (int i = 0; i < n; i++) {
        float k = a->target_vol > a->vol ? ka : kr;
        a->vol += (a->target_vol - a->vol) * k;
        a->freq_hz += (a->target_freq - a->freq_hz) * ka;
        a->phase += 2.0f * (float)M_PI * a->freq_hz / (float)a->fs;
        if (a->phase > 2.0f * (float)M_PI) a->phase -= 2.0f * (float)M_PI;
        float s = sinf(a->phase) * a->vol;
        if (a->grunt) { a->grunt_phase += 2.0f * (float)M_PI * 25.0f / (float)a->fs; if (a->grunt_phase > 2.0f * (float)M_PI) a->grunt_phase -= 2.0f * (float)M_PI; s *= 0.55f + 0.45f * sinf(a->grunt_phase); }
        if (a->threshold_hz > 0 && a->vol < 0.02f) {
            static float th = 0; th += 2.0f * (float)M_PI * a->threshold_hz / (float)a->fs; if (th > 2.0f * (float)M_PI) th -= 2.0f * (float)M_PI;
            s += sinf(th) * a->threshold_vol * 0.15f;
        }
        if (a->cue) {                                   /* cue: replaces the tone while it plays */
            if (a->cue_left <= 0) { if (a->cue[a->cue_i + 1] == 0) a->cue = 0; else { a->cue_left = (int)a->cue[a->cue_i + 1] * a->fs / 1000; a->cue_i += 2; } }
            if (a->cue) { float f = (float)a->cue[a->cue_i - 2]; a->cue_left--;
                static float cph = 0; cph += 2.0f * (float)M_PI * f / (float)a->fs; if (cph > 2.0f * (float)M_PI) cph -= 2.0f * (float)M_PI;
                s = f > 0 ? 0.6f * sinf(cph) : 0; }
        }
        s *= a->master;
        if (s > 1) s = 1; if (s < -1) s = -1;
        buf[i] = (int16_t)(s * 32000.0f);
    }
}

float audio_tone_for_id(int scheme, float cond_id, int ferrous, float strength01)
{
    switch (scheme) {
    case TONE_SCHEME_2: return ferrous ? 180.0f : 750.0f;
    case TONE_SCHEME_5:
        if (ferrous) return 180.0f;
        if (cond_id < 25) return 350.0f;
        if (cond_id < 50) return 500.0f;
        if (cond_id < 75) return 700.0f;
        return 950.0f;
    case TONE_SCHEME_PITCH: return ferrous ? 180.0f : 250.0f + 7.0f * cond_id;    /* 250..943 Hz */
    default: return 300.0f + 600.0f * strength01;                                  /* VCO: pitch follows strength */
    }
}
