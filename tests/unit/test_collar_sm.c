#include "collar_sm.h"
#include "unity.h"

static collar_sm_t sm;
static uint32_t now;

void setUp(void)
{
    collar_sm_init(&sm, 0);
    now = 0;
}

void tearDown(void) {}

/* One GPS fix, 1 s after the previous one. */
static collar_output_t fix_at(bool valid, bool inside, float dist)
{
    collar_input_t in;
    now += 1000;
    in.fix_valid = valid;
    in.inside = inside;
    in.dist_to_edge_m = dist;
    in.now_ms = now;
    return collar_sm_update(&sm, &in);
}

#define DEEP_INSIDE() fix_at(true, true, 50.0f)
#define NEAR_EDGE() fix_at(true, true, 5.0f)
#define OUTSIDE() fix_at(true, false, 3.0f)
#define NO_FIX() fix_at(false, false, 0.0f)

static void test_starts_inside(void)
{
    TEST_ASSERT_EQUAL(COLLAR_STATE_INSIDE, sm.state);
    TEST_ASSERT_EQUAL_UINT8(COLLAR_CONFIRM_FIXES, sm.confirm_fixes);
}

static void test_inside_stays_inside_no_cue(void)
{
    collar_output_t o = DEEP_INSIDE();
    TEST_ASSERT_FALSE(o.state_changed);
    TEST_ASSERT_EQUAL(COLLAR_CUE_NONE, o.cue);
}

static void test_warning_needs_two_fixes_then_audio(void)
{
    collar_output_t o = NEAR_EDGE();
    TEST_ASSERT_FALSE(o.state_changed); /* hysteresis: one fix is not enough */
    TEST_ASSERT_EQUAL(COLLAR_CUE_NONE, o.cue);
    o = NEAR_EDGE();
    TEST_ASSERT_TRUE(o.state_changed);
    TEST_ASSERT_EQUAL(COLLAR_STATE_INSIDE, o.prev_state);
    TEST_ASSERT_EQUAL(COLLAR_STATE_WARNING, o.state);
    TEST_ASSERT_EQUAL(COLLAR_CUE_AUDIO, o.cue);
    o = NEAR_EDGE();
    TEST_ASSERT_EQUAL(COLLAR_CUE_NONE, o.cue); /* audio once per entry */
}

static void test_warning_boundary_distance(void)
{
    fix_at(true, true, COLLAR_WARN_DIST_M);
    fix_at(true, true, COLLAR_WARN_DIST_M); /* exactly 10 m: still INSIDE */
    TEST_ASSERT_EQUAL(COLLAR_STATE_INSIDE, sm.state);
    fix_at(true, true, COLLAR_WARN_DIST_M - 0.1f);
    fix_at(true, true, COLLAR_WARN_DIST_M - 0.1f);
    TEST_ASSERT_EQUAL(COLLAR_STATE_WARNING, sm.state);
}

static void test_breach_gives_vibration_level_1(void)
{
    collar_output_t o;
    NEAR_EDGE();
    NEAR_EDGE();
    OUTSIDE();
    o = OUTSIDE();
    TEST_ASSERT_TRUE(o.state_changed);
    TEST_ASSERT_EQUAL(COLLAR_STATE_BREACH, o.state);
    TEST_ASSERT_EQUAL(COLLAR_CUE_VIBRATION, o.cue);
    TEST_ASSERT_EQUAL_UINT8(1, o.vib_level);
}

static void test_inside_straight_to_breach(void)
{
    collar_output_t o;
    OUTSIDE();
    o = OUTSIDE();
    TEST_ASSERT_EQUAL(COLLAR_STATE_BREACH, o.state);
    TEST_ASSERT_EQUAL(COLLAR_CUE_VIBRATION, o.cue);
}

static void test_breach_escalates_then_caps(void)
{
    collar_output_t o;
    int i, cues = 0;
    OUTSIDE();
    OUTSIDE(); /* level 1 at t = 2 s */
    for (i = 0; i < 9; i++) {
        o = OUTSIDE();
        TEST_ASSERT_EQUAL(COLLAR_CUE_NONE, o.cue); /* t = 3..11 s, < 10 s since cue */
    }
    o = OUTSIDE(); /* t = 12 s */
    TEST_ASSERT_EQUAL(COLLAR_CUE_VIBRATION, o.cue);
    TEST_ASSERT_EQUAL_UINT8(2, o.vib_level);
    for (i = 0; i < 60; i++) {
        o = OUTSIDE();
        if (o.cue == COLLAR_CUE_VIBRATION) {
            cues++;
            TEST_ASSERT_EQUAL_UINT8(3, o.vib_level);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, cues); /* level 3 once, then welfare cap */
}

static void test_return_from_breach_is_quiet_and_resets(void)
{
    collar_output_t o;
    OUTSIDE();
    OUTSIDE();
    NEAR_EDGE();
    o = NEAR_EDGE();
    TEST_ASSERT_EQUAL(COLLAR_STATE_WARNING, o.state);
    TEST_ASSERT_EQUAL(COLLAR_CUE_NONE, o.cue); /* no audio when walking back in */
    DEEP_INSIDE();
    o = DEEP_INSIDE();
    TEST_ASSERT_EQUAL(COLLAR_STATE_INSIDE, o.state);
    TEST_ASSERT_EQUAL_UINT8(0, sm.vib_level);
    OUTSIDE();
    o = OUTSIDE();
    TEST_ASSERT_EQUAL_UINT8(1, o.vib_level); /* escalation starts over */
}

static void test_no_fix_holds_state_and_never_cues(void)
{
    collar_output_t o;
    int i;
    OUTSIDE();
    OUTSIDE(); /* BREACH, level 1 */
    for (i = 0; i < 100; i++) {
        o = NO_FIX();
        TEST_ASSERT_FALSE(o.state_changed);
        TEST_ASSERT_EQUAL(COLLAR_CUE_NONE, o.cue); /* no escalation without a fix */
    }
    TEST_ASSERT_EQUAL(COLLAR_STATE_BREACH, sm.state);
}

static void test_no_fix_breaks_consecutive_streak(void)
{
    OUTSIDE();
    NO_FIX();
    OUTSIDE();
    TEST_ASSERT_EQUAL(COLLAR_STATE_INSIDE, sm.state);
    OUTSIDE();
    TEST_ASSERT_EQUAL(COLLAR_STATE_BREACH, sm.state);
}

static void test_jitter_does_not_flap(void)
{
    int i;
    NEAR_EDGE();
    NEAR_EDGE(); /* WARNING */
    for (i = 0; i < 50; i++) {
        TEST_ASSERT_FALSE(OUTSIDE().state_changed);
        TEST_ASSERT_FALSE(NEAR_EDGE().state_changed);
    }
    TEST_ASSERT_EQUAL(COLLAR_STATE_WARNING, sm.state);
}

static void test_candidate_switch_restarts_count(void)
{
    NEAR_EDGE(); /* candidate WARNING (1) */
    OUTSIDE();   /* candidate BREACH (1) - different, so count restarts */
    TEST_ASSERT_EQUAL(COLLAR_STATE_INSIDE, sm.state);
    OUTSIDE();
    TEST_ASSERT_EQUAL(COLLAR_STATE_BREACH, sm.state);
}

static void test_confirm_one_disables_hysteresis(void)
{
    collar_sm_init(&sm, 1);
    TEST_ASSERT_TRUE(OUTSIDE().state_changed);
}

static void test_escalation_survives_millis_wrap(void)
{
    collar_output_t o;
    now = 0xFFFFFFFFu - 2500u;
    OUTSIDE();
    OUTSIDE(); /* level 1 at 0xFFFFF63B */
    now += 9000; /* wrapped past zero; exactly 10 s later on next fix */
    o = OUTSIDE();
    TEST_ASSERT_EQUAL(COLLAR_CUE_VIBRATION, o.cue);
    TEST_ASSERT_EQUAL_UINT8(2, o.vib_level);
}

static void test_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("INSIDE", collar_state_name(COLLAR_STATE_INSIDE));
    TEST_ASSERT_EQUAL_STRING("WARNING", collar_state_name(COLLAR_STATE_WARNING));
    TEST_ASSERT_EQUAL_STRING("BREACH", collar_state_name(COLLAR_STATE_BREACH));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_inside);
    RUN_TEST(test_inside_stays_inside_no_cue);
    RUN_TEST(test_warning_needs_two_fixes_then_audio);
    RUN_TEST(test_warning_boundary_distance);
    RUN_TEST(test_breach_gives_vibration_level_1);
    RUN_TEST(test_inside_straight_to_breach);
    RUN_TEST(test_breach_escalates_then_caps);
    RUN_TEST(test_return_from_breach_is_quiet_and_resets);
    RUN_TEST(test_no_fix_holds_state_and_never_cues);
    RUN_TEST(test_no_fix_breaks_consecutive_streak);
    RUN_TEST(test_jitter_does_not_flap);
    RUN_TEST(test_candidate_switch_restarts_count);
    RUN_TEST(test_confirm_one_disables_hysteresis);
    RUN_TEST(test_escalation_survives_millis_wrap);
    RUN_TEST(test_state_names);
    return UNITY_END();
}
