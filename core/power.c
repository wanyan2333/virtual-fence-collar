/*
 * power.c - see power.h.
 *
 * "Moved less than 2 m over the last 60 s" is implemented with an anchor:
 * remember a position; if a new fix is >= 2 m from it, the animal moved, so
 * re-anchor there. If no fix has left the 2 m circle for 60 s, go slow.
 * This needs O(1) memory instead of a 60-sample history buffer.
 */
#include "power.h"

#include <math.h>

void power_init(power_ctrl_t *p)
{
    p->mode = POWER_MODE_FAST;
    p->has_anchor = false;
    p->anchor.x = 0.0f;
    p->anchor.y = 0.0f;
    p->anchor_ms = 0;
}

static void set_anchor(power_ctrl_t *p, local_point_t pos, uint32_t now_ms)
{
    p->anchor = pos;
    p->anchor_ms = now_ms;
    p->has_anchor = true;
}

uint32_t power_update(power_ctrl_t *p, bool fix_valid, local_point_t pos,
                      collar_state_t state, uint32_t now_ms)
{
    if (!fix_valid) {
        /* No new information: keep whatever mode we are in. */
        return power_interval_ms(p);
    }

    if (state != COLLAR_STATE_INSIDE) {
        /* Near or over the fence: always track at full rate. */
        p->mode = POWER_MODE_FAST;
        set_anchor(p, pos, now_ms);
        return power_interval_ms(p);
    }

    if (!p->has_anchor) {
        set_anchor(p, pos, now_ms);
    } else {
        const float dx = pos.x - p->anchor.x;
        const float dy = pos.y - p->anchor.y;
        if (sqrtf(dx * dx + dy * dy) >= POWER_STILL_DIST_M) {
            p->mode = POWER_MODE_FAST; /* moved: fast again immediately */
            set_anchor(p, pos, now_ms);
        } else if ((uint32_t)(now_ms - p->anchor_ms) >= POWER_STILL_WINDOW_MS) {
            p->mode = POWER_MODE_SLOW;
        }
    }
    return power_interval_ms(p);
}

uint32_t power_interval_ms(const power_ctrl_t *p)
{
    return (p->mode == POWER_MODE_SLOW) ? POWER_SLOW_INTERVAL_MS : POWER_FAST_INTERVAL_MS;
}

const char *power_mode_name(power_mode_t m)
{
    return (m == POWER_MODE_SLOW) ? "SLOW" : "FAST";
}
