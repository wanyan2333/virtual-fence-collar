#include "power.h"
#include "unity.h"

static power_ctrl_t pc;

void setUp(void) { power_init(&pc); }
void tearDown(void) {}

static local_point_t P(float x, float y)
{
    local_point_t p;
    p.x = x;
    p.y = y;
    return p;
}

static uint32_t sample(float x, float y, uint32_t t_s)
{
    return power_update(&pc, true, P(x, y), COLLAR_STATE_INSIDE, t_s * 1000u);
}

static void test_starts_fast(void)
{
    TEST_ASSERT_EQUAL_UINT32(POWER_FAST_INTERVAL_MS, power_interval_ms(&pc));
    TEST_ASSERT_EQUAL(POWER_MODE_FAST, pc.mode);
}

static void test_stationary_for_60s_goes_slow(void)
{
    uint32_t t;
    for (t = 0; t < 60; t++) {
        /* small jitter, well under 2 m */
        TEST_ASSERT_EQUAL_UINT32(POWER_FAST_INTERVAL_MS, sample((t % 2) ? 0.5f : -0.5f, 0.3f, t));
    }
    TEST_ASSERT_EQUAL_UINT32(POWER_SLOW_INTERVAL_MS, sample(0.2f, 0.0f, 60));
    TEST_ASSERT_EQUAL_STRING("SLOW", power_mode_name(pc.mode));
}

static void test_movement_returns_to_fast_immediately(void)
{
    sample(0, 0, 0);
    sample(0, 0, 60);
    TEST_ASSERT_EQUAL(POWER_MODE_SLOW, pc.mode);
    TEST_ASSERT_EQUAL_UINT32(POWER_FAST_INTERVAL_MS, sample(2.5f, 0, 90));
}

static void test_slow_drift_counts_as_movement(void)
{
    uint32_t t;
    /* 0.1 m/s: each step is tiny, but after 20 s it is 2 m from the anchor,
     * so the anchor moves and we never reach 60 s "still". */
    for (t = 0; t <= 120; t++) {
        TEST_ASSERT_EQUAL_UINT32(POWER_FAST_INTERVAL_MS, sample(0.1f * (float)t, 0, t));
    }
}

static void test_warning_or_breach_forces_fast(void)
{
    sample(0, 0, 0);
    sample(0, 0, 60);
    TEST_ASSERT_EQUAL(POWER_MODE_SLOW, pc.mode);
    TEST_ASSERT_EQUAL_UINT32(POWER_FAST_INTERVAL_MS,
                             power_update(&pc, true, P(0, 0), COLLAR_STATE_WARNING, 90000u));
    sample(0, 0, 91);
    TEST_ASSERT_EQUAL(POWER_MODE_FAST, pc.mode); /* 60 s window restarted */
    TEST_ASSERT_EQUAL_UINT32(POWER_FAST_INTERVAL_MS,
                             power_update(&pc, true, P(0, 0), COLLAR_STATE_BREACH, 120000u));
}

static void test_invalid_fix_keeps_mode(void)
{
    sample(0, 0, 0);
    sample(0, 0, 60);
    TEST_ASSERT_EQUAL_UINT32(POWER_SLOW_INTERVAL_MS,
                             power_update(&pc, false, P(500, 500), COLLAR_STATE_INSIDE, 90000u));
    TEST_ASSERT_EQUAL(POWER_MODE_SLOW, pc.mode);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_fast);
    RUN_TEST(test_stationary_for_60s_goes_slow);
    RUN_TEST(test_movement_returns_to_fast_immediately);
    RUN_TEST(test_slow_drift_counts_as_movement);
    RUN_TEST(test_warning_or_breach_forces_fast);
    RUN_TEST(test_invalid_fix_keeps_mode);
    return UNITY_END();
}
