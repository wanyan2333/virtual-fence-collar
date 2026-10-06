/*
 * power.h - GPS duty cycling.
 *
 * The GPS receiver is the biggest power consumer on a collar. If the animal
 * has stayed within STILL_DIST_M of one spot for STILL_WINDOW_MS, sample GPS
 * every SLOW_INTERVAL_MS instead of every FAST_INTERVAL_MS. Go back to fast
 * sampling as soon as it moves, or whenever it is in WARNING/BREACH.
 */
#ifndef POWER_H
#define POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "collar_sm.h"
#include "geofence.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_FAST_INTERVAL_MS 1000u
#define POWER_SLOW_INTERVAL_MS 30000u
#define POWER_STILL_WINDOW_MS 60000u
#define POWER_STILL_DIST_M 2.0f

typedef enum {
    POWER_MODE_FAST = 0,
    POWER_MODE_SLOW
} power_mode_t;

typedef struct {
    power_mode_t mode;
    bool has_anchor;
    local_point_t anchor;  /* where the animal was when it last "moved" */
    uint32_t anchor_ms;    /* time of that position                     */
} power_ctrl_t;

void power_init(power_ctrl_t *p);

/* Feed one GPS sample; returns the interval (ms) until the next sample. */
uint32_t power_update(power_ctrl_t *p, bool fix_valid, local_point_t pos,
                      collar_state_t state, uint32_t now_ms);

uint32_t power_interval_ms(const power_ctrl_t *p);

const char *power_mode_name(power_mode_t m);

#ifdef __cplusplus
}
#endif

#endif /* POWER_H */
