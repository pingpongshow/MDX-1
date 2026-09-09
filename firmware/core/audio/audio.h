/* Audio synthesis for the DAC / I2S path: one or two tones with a fast envelope so that
 * the response to a target is under 5 ms. The detector sets the target parameters; the
 * fill callback runs from the DMA half-buffer interrupt. */
#ifndef MDX_AUDIO_H
#define MDX_AUDIO_H
#include <stdint.h>

enum { TONE_SCHEME_VCO = 0, TONE_SCHEME_2 = 1, TONE_SCHEME_5 = 2, TONE_SCHEME_PITCH = 3 };

typedef struct {
    float freq_hz, target_freq;
    float vol, target_vol;      /* 0..1 */
    float phase;
    float threshold_hz;         /* threshold hum frequency (0 = off) */
    float threshold_vol;
    float master;               /* 0..1 */
    int   iron_low;             /* ferrous tone flag */
    int   grunt;                /* amplitude-modulate the tone at 25 Hz (iron "grunt") */
    float grunt_phase;
    int   fs;
    const uint16_t *cue; int cue_i; int cue_left;   /* audio cue player: (Hz, ms) pairs, 0-terminated */
} audio_t;

void  audio_init(audio_t *a, int fs);
void  audio_set(audio_t *a, float freq_hz, float vol);      /* vol 0 = silence */
void  audio_set_threshold(audio_t *a, float freq_hz, float vol);
void  audio_set_grunt(audio_t *a, int on);
void audio_cue(audio_t *a, const uint16_t *seq);        /* play a short (Hz, ms) pattern, 0-terminated, over whatever is sounding */
int  audio_cue_active(const audio_t *a);
void  audio_fill(int16_t *buf, int n, void *ctx);            /* hal audio callback */
/* map an ID to a tone frequency per scheme */
float audio_tone_for_id(int scheme, float cond_id, int ferrous, float strength01);
#endif
