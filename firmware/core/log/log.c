#include "log.h"
#include <stdio.h>
#include <string.h>
static void ring_push(logger_t *l, const detector_t *d);

void log_init(logger_t *l) { memset(l, 0, sizeof(*l)); l->enabled = 1; }

void log_find_line(char *b, int n, const detector_t *d)
{
    const status_t *s = &d->st;
    snprintf(b, n, "%lu,%d,%.6f,%.6f,%.1f,%d,%d,%.0f,%d,%d,%.1f,%.0f,%s\n",
             (unsigned long)s->gnss.utc_s, s->gnss.valid, s->gnss.lat, s->gnss.lon, s->gnss.hdop,
             (int)(s->est.cond_id + 0.5f), (int)s->est.fe_index, s->est.depth_cm, (int)(100 * s->est.confidence),
             d->set.mode, s->ground_phase_deg, s->strength_db, s->match_idx >= 0 ? d->cls.e[s->match_idx].name : "");
}

void log_trail_line(char *b, int n, const detector_t *d)
{
    const status_t *s = &d->st;
    snprintf(b, n, "%lu,%.6f,%.6f,%.1f,%.1f,%.3f,%.1f\n", (unsigned long)s->gnss.utc_s, s->gnss.lat, s->gnss.lon, s->gnss.hdop, s->ground_phase_deg, s->ground_strength * 1e3f, s->noise_level * 1e6f);
}

void log_tick(logger_t *l, const detector_t *d, int mark_find, uint32_t now)
{
    /* the RAM ring (phone link, log screen) fills regardless of the card; CSV only when logging is on and a card is in */
    if (mark_find && now - l->last_find_ms > 3000 && !(l->enabled && d->set.log_enable && hal_sd_present())) { l->last_find_ms = now; l->finds++; ring_push(l, d); return; }
    if (!l->enabled || !d->set.log_enable || !hal_sd_present()) return;
    if (mark_find && now - l->last_find_ms > 3000) ring_push(l, d);
    char line[160];
    if (now - l->last_trail_ms >= 2000 && d->st.gnss.valid) {
        l->last_trail_ms = now; log_trail_line(line, sizeof line, d); hal_sd_append("MDX1/trail.csv", line);
    }
    if (mark_find && now - l->last_find_ms > 3000) {
        l->last_find_ms = now; l->finds++; log_find_line(line, sizeof line, d); hal_sd_append("MDX1/finds.csv", line);
    }
}

int log_find_count(const logger_t *l) { return l->ring_n; }
int log_find_get(const logger_t *l, int idx, find_rec_t *o)
{
    if (idx < 0 || idx >= l->ring_n) return 0;
    *o = l->ring[(l->ring_head - 1 - idx + LOG_RING) % LOG_RING]; return 1;
}
static void ring_push(logger_t *l, const detector_t *d)
{
    const status_t *s = &d->st; find_rec_t *f = &l->ring[l->ring_head];
    f->utc_s = s->gnss.utc_s; f->lat = s->gnss.valid ? s->gnss.lat : 0; f->lon = s->gnss.valid ? s->gnss.lon : 0;
    f->cond_id = (int16_t)(s->est.cond_id + 0.5f); f->fe = (uint8_t)(s->est.fe_index < 0 ? 0 : (s->est.fe_index > 100 ? 100 : s->est.fe_index));
    f->depth_cm = (uint8_t)(s->est.depth_cm < 0 ? 0 : (s->est.depth_cm > 255 ? 255 : s->est.depth_cm)); f->conf = (uint8_t)(100 * s->est.confidence);
    memset(f->name, 0, sizeof f->name); if (s->match_idx >= 0) strncpy(f->name, d->cls.e[s->match_idx].name, sizeof f->name - 1);
    l->ring_head = (l->ring_head + 1) % LOG_RING; if (l->ring_n < LOG_RING) l->ring_n++;
}
