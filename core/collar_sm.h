/*
 * collar_sm.h - Collar behaviour state machine.
 *
 *   INSIDE  --(within WARN_DIST of edge)-->  WARNING  : audio cue
 *   any     --(outside the fence)--------->  BREACH   : vibration cue,
 *                                                        escalates every
 *                                                        ESCALATE_MS
 *   BREACH  --(back inside)--------------->  WARNING/INSIDE (no cue, the
 *                                                        animal is returning)
 *   reaching INSIDE resets the escalation level.
 *
 * Hysteresis: the new state must be seen on CONFIRM_FIXES consecutive valid
 * fixes before we switch, so one noisy GPS fix cannot flip the state.
 * Invalid fix: hold the current state and never emit a cue.
 */
#ifndef COLLAR_SM_H
#define COLLAR_SM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COLLAR_WARN_DIST_M 10.0f    /* warning band inside the fence edge     */
#define COLLAR_CONFIRM_FIXES 2u     /* consecutive fixes needed to change     */
#define COLLAR_ESCALATE_MS 10000u   /* re-cue (stronger) if still outside     */
#define COLLAR_MAX_VIB_LEVEL 3u     /* welfare cap: no more cues after this   */

typedef enum {
    COLLAR_STATE_INSIDE = 0,
    COLLAR_STATE_WARNING,
    COLLAR_STATE_BREACH
} collar_state_t;

typedef enum {
    COLLAR_CUE_NONE = 0,
    COLLAR_CUE_AUDIO,
    COLLAR_CUE_VIBRATION
} collar_cue_t;

typedef struct {
    bool fix_valid;        /* false: no usable GPS fix this time          */
    bool inside;           /* from geofence_contains()                    */
    float dist_to_edge_m;  /* from geofence_distance_to_edge()            */
    uint32_t now_ms;
} collar_input_t;

typedef struct {
    bool state_changed;
    collar_state_t prev_state;
    collar_state_t state;
    collar_cue_t cue;      /* what the app should play right now          */
    uint8_t vib_level;     /* 1..COLLAR_MAX_VIB_LEVEL when cue == VIBRATION */
} collar_output_t;

typedef struct {
    collar_state_t state;
    collar_state_t candidate;   /* state we might switch to               */
    uint8_t candidate_count;    /* consecutive fixes agreeing with it     */
    uint8_t confirm_fixes;      /* hysteresis depth (normally 2)          */
    uint8_t vib_level;          /* current escalation level, 0 = none     */
    uint32_t last_vib_ms;       /* when the last vibration was given      */
} collar_sm_t;

/* confirm_fixes = 0 selects the default COLLAR_CONFIRM_FIXES. */
void collar_sm_init(collar_sm_t *sm, uint8_t confirm_fixes);

collar_output_t collar_sm_update(collar_sm_t *sm, const collar_input_t *in);

const char *collar_state_name(collar_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* COLLAR_SM_H */
