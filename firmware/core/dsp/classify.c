#include "classify.h"
#include <math.h>
#include <string.h>

static void sig_of(const cpx T[MDX_NLINES], float s[2 * MDX_NLINES])
{
    float n = sqrtf(cpx_norm2(T, MDX_NLINES)); if (n < 1e-20f) n = 1e-20f;
    for (int k = 0; k < MDX_NLINES; k++) { s[2 * k] = T[k].re / n; s[2 * k + 1] = T[k].im / n; }
}

void classify_init(classifier_t *c) { memset(c, 0, sizeof(*c)); c->match_threshold = 0.05f; }

int classify_learn(classifier_t *c, const char *name, const cpx T[MDX_NLINES], float cond_id, float fe_index)
{
    for (int i = 0; i < CLS_MAX; i++) if (!c->e[i].used) {
        c->e[i].used = 1;
        strncpy(c->e[i].name, name, CLS_NAME - 1); c->e[i].name[CLS_NAME - 1] = 0;
        sig_of(T, c->e[i].sig); c->e[i].cond_id = cond_id; c->e[i].fe_index = fe_index;
        return i;
    }
    return -1;
}

int classify_match(const classifier_t *c, const cpx T[MDX_NLINES], float *dist)
{
    float s[2 * MDX_NLINES]; sig_of(T, s);
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < CLS_MAX; i++) if (c->e[i].used) {
        float d = 0; for (int j = 0; j < 2 * MDX_NLINES; j++) d += s[j] * c->e[i].sig[j];
        d = 1.0f - d;                      /* cosine distance */
        if (d < bd) { bd = d; best = i; }
    }
    if (dist) *dist = bd;
    return (best >= 0 && bd <= c->match_threshold) ? best : -1;
}

void classify_forget(classifier_t *c, int idx) { if (idx >= 0 && idx < CLS_MAX) c->e[idx].used = 0; }
