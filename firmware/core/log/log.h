/* Find and coverage logging to the SD card (CSV, one directory per day). */
#ifndef MDX_LOG_H
#define MDX_LOG_H
#include "../det/detector.h"
#define LOG_RING 32
typedef struct { uint32_t utc_s; double lat, lon; int16_t cond_id; uint8_t fe, depth_cm, conf; char name[12]; } find_rec_t;
typedef struct { uint32_t last_trail_ms; uint32_t finds; uint32_t last_find_ms; int enabled; find_rec_t ring[LOG_RING]; int ring_n, ring_head; } logger_t;
int  log_find_count(const logger_t *l);                   /* finds kept in RAM (last LOG_RING) */
int  log_find_get(const logger_t *l, int idx, find_rec_t *out);   /* 0 = newest */
void log_init(logger_t *l);
void log_find_line(char *buf, int n, const detector_t *d);
void log_trail_line(char *buf, int n, const detector_t *d);
/* call at ~10 Hz; writes a trail point every 2 s and a find line on each dig-mark (KEY_GB long press or accepted target held > 1.5 s) */
void log_tick(logger_t *l, const detector_t *d, int mark_find, uint32_t now_ms);
#endif
