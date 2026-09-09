/* User-taught target library. A signature is the normalised five-line target vector
 * (10 floats); matching uses cosine distance, which is independent of depth/size. */
#ifndef MDX_CLASSIFY_H
#define MDX_CLASSIFY_H
#include "cpx.h"
#include "../mdx_config.h"

#define CLS_MAX 16
#define CLS_NAME 12
typedef struct {
    char  name[CLS_NAME];
    float sig[2 * MDX_NLINES];
    float cond_id, fe_index;
    int   used;
} cls_entry_t;

typedef struct {
    cls_entry_t e[CLS_MAX];
    float match_threshold;      /* max cosine distance to report a match (0.05 typical) */
} classifier_t;

void  classify_init(classifier_t *c);
int   classify_learn(classifier_t *c, const char *name, const cpx T[MDX_NLINES], float cond_id, float fe_index);
/* returns index of best match or -1; *dist receives the cosine distance */
int   classify_match(const classifier_t *c, const cpx T[MDX_NLINES], float *dist);
void  classify_forget(classifier_t *c, int idx);
#endif
