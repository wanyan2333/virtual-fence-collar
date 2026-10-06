/*
 * collar_sm.c - see collar_sm.h.
 */
#include "collar_sm.h"

void collar_sm_init(collar_sm_t *sm, uint8_t confirm_fixes)
{
    sm->state = COLLAR_STATE_INSIDE;
    sm->candidate = COLLAR_STATE_INSIDE;
    sm->candidate_count = 0;
    sm->confirm_fixes = (confirm_fixes == 0) ? (uint8_t)COLLAR_CONFIRM_FIXES : confirm_fixes;
    sm->vib_level = 0;
    sm->last_vib_ms = 0;
}

/* Which state does this single fix suggest? */
static collar_state_t classify(const collar_input_t *in)
{
    if (!in->inside) return COLLAR_STATE_BREACH;
    if (in->dist_to_edge_m < COLLAR_WARN_DIST_M) return COLLAR_STATE_WARNING;
    return COLLAR_STATE_INSIDE;
}

/* Actions on entering a new state. */
static void on_enter(collar_sm_t *sm, collar_state_t from, uint32_t now_ms,
                     collar_output_t *out)
{
    switch (sm->state) {
    case COLLAR_STATE_WARNING:
        sm->vib_level = 0;
        /* Audio only when approaching from inside. Coming back from BREACH
         * the animal is doing the right thing, so stay quiet. */
        if (from == COLLAR_STATE_INSIDE) out->cue = COLLAR_CUE_AUDIO;
        break;
    case COLLAR_STATE_BREACH:
        sm->vib_level = 1;
        sm->last_vib_ms = now_ms;
        out->cue = COLLAR_CUE_VIBRATION;
        out->vib_level = 1;
        break;
    case COLLAR_STATE_INSIDE:
    default:
        sm->vib_level = 0; /* back inside: reset escalation */
        break;
    }
}

collar_output_t collar_sm_update(collar_sm_t *sm, const collar_input_t *in)
{
    collar_output_t out;
    collar_state_t target;

    out.state_changed = false;
    out.prev_state = sm->state;
    out.state = sm->state;
    out.cue = COLLAR_CUE_NONE;
    out.vib_level = 0;

    if (!in->fix_valid) {
        /* No fix: we don't know where the animal is. Hold the state, never
         * cue, and break any "consecutive fixes" streak. */
        sm->candidate_count = 0;
        return out;
    }

    target = classify(in);
    if (target == sm->state) {
        sm->candidate_count = 0; /* fix agrees with current state */
    } else {
        if (target == sm->candidate && sm->candidate_count > 0) {
            sm->candidate_count++;
        } else {
            sm->candidate = target; /* new candidate, start counting again */
            sm->candidate_count = 1;
        }
        if (sm->candidate_count >= sm->confirm_fixes) {
            const collar_state_t from = sm->state;
            sm->state = target;
            sm->candidate_count = 0;
            out.state_changed = true;
            out.state = target;
            on_enter(sm, from, in->now_ms, &out);
            return out;
        }
    }

    /* Still outside: escalate every ESCALATE_MS, up to the welfare cap.
     * Unsigned subtraction handles the 49-day millis() wrap correctly. */
    if (sm->state == COLLAR_STATE_BREACH && sm->vib_level < COLLAR_MAX_VIB_LEVEL &&
        (uint32_t)(in->now_ms - sm->last_vib_ms) >= COLLAR_ESCALATE_MS) {
        sm->vib_level++;
        sm->last_vib_ms = in->now_ms;
        out.cue = COLLAR_CUE_VIBRATION;
        out.vib_level = sm->vib_level;
    }
    return out;
}

const char *collar_state_name(collar_state_t s)
{
    switch (s) {
    case COLLAR_STATE_INSIDE: return "INSIDE";
    case COLLAR_STATE_WARNING: return "WARNING";
    case COLLAR_STATE_BREACH: return "BREACH";
    default: return "?";
    }
}
