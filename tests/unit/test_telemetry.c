#include "telemetry.h"
#include "unity.h"

static telem_ring_t ring;

void setUp(void) { telem_init(&ring); }
void tearDown(void) {}

static telem_record_t rec(uint32_t seq)
{
    telem_record_t r = { 0 };
    r.seq = seq;
    r.t_ms = seq * 1000u;
    return r;
}

static void push_range(uint32_t from, uint32_t to_exclusive)
{
    uint32_t s;
    for (s = from; s < to_exclusive; s++) {
        telem_record_t r = rec(s);
        telem_push(&ring, &r);
    }
}

/* Fake radio: accepts `budget` records, then fails. */
typedef struct {
    int budget;
    uint32_t seqs[TELEM_CAPACITY];
    int n;
} fake_radio_t;

static bool fake_send(const telem_record_t *r, void *ctx)
{
    fake_radio_t *radio = (fake_radio_t *)ctx;
    if (radio->budget <= 0) return false;
    radio->budget--;
    radio->seqs[radio->n++] = r->seq;
    return true;
}

static void test_empty(void)
{
    telem_record_t out;
    TEST_ASSERT_EQUAL_UINT16(0, telem_count(&ring));
    TEST_ASSERT_FALSE(telem_pop(&ring, &out));
    TEST_ASSERT_FALSE(telem_peek(&ring, &out));
}

static void test_fifo_order(void)
{
    telem_record_t out;
    push_range(1, 4);
    TEST_ASSERT_EQUAL_UINT16(3, telem_count(&ring));
    TEST_ASSERT_TRUE(telem_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(1, out.seq);
    TEST_ASSERT_TRUE(telem_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(2, out.seq);
}

static void test_wraps_around_end_of_array(void)
{
    telem_record_t out;
    uint32_t s;
    push_range(0, 20);
    for (s = 0; s < 20; s++) telem_pop(&ring, &out);
    push_range(100, 120); /* head is at 20, so these wrap past index 31 */
    for (s = 100; s < 120; s++) {
        TEST_ASSERT_TRUE(telem_pop(&ring, &out));
        TEST_ASSERT_EQUAL_UINT32(s, out.seq);
    }
    TEST_ASSERT_EQUAL_UINT32(0, ring.dropped);
}

static void test_overflow_drops_oldest(void)
{
    telem_record_t out;
    telem_record_t r = rec(999);
    push_range(0, TELEM_CAPACITY);
    TEST_ASSERT_FALSE(telem_push(&ring, &r) == true); /* full: reports a drop */
    push_range(1000, 1004);
    TEST_ASSERT_EQUAL_UINT16(TELEM_CAPACITY, telem_count(&ring));
    TEST_ASSERT_EQUAL_UINT32(5, ring.dropped);
    TEST_ASSERT_TRUE(telem_peek(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(5, out.seq); /* seq 0..4 were dropped */
}

static void test_flush_all_when_online(void)
{
    fake_radio_t radio = { 100, { 0 }, 0 };
    push_range(10, 15);
    TEST_ASSERT_EQUAL_UINT16(5, telem_flush(&ring, fake_send, &radio, TELEM_CAPACITY));
    TEST_ASSERT_EQUAL_UINT16(0, telem_count(&ring));
    TEST_ASSERT_EQUAL_UINT32(10, radio.seqs[0]);
    TEST_ASSERT_EQUAL_UINT32(14, radio.seqs[4]);
}

static void test_flush_keeps_record_on_send_failure(void)
{
    telem_record_t out;
    fake_radio_t radio = { 3, { 0 }, 0 };
    push_range(0, 10);
    TEST_ASSERT_EQUAL_UINT16(3, telem_flush(&ring, fake_send, &radio, TELEM_CAPACITY));
    TEST_ASSERT_EQUAL_UINT16(7, telem_count(&ring));
    telem_peek(&ring, &out);
    TEST_ASSERT_EQUAL_UINT32(3, out.seq); /* failed record is still first */
}

static void test_flush_respects_max(void)
{
    fake_radio_t radio = { 100, { 0 }, 0 };
    push_range(0, 10);
    TEST_ASSERT_EQUAL_UINT16(4, telem_flush(&ring, fake_send, &radio, 4));
    TEST_ASSERT_EQUAL_UINT16(6, telem_count(&ring));
}

static void test_flush_after_overflow_sends_newest_capacity(void)
{
    fake_radio_t radio = { 100, { 0 }, 0 };
    push_range(0, 50);
    TEST_ASSERT_EQUAL_UINT16(TELEM_CAPACITY, telem_flush(&ring, fake_send, &radio, 100));
    TEST_ASSERT_EQUAL_UINT32(50 - TELEM_CAPACITY, radio.seqs[0]);
    TEST_ASSERT_EQUAL_UINT32(49, radio.seqs[TELEM_CAPACITY - 1]);
}

static void test_encode_little_endian(void)
{
    uint8_t buf[TELEM_ENCODED_SIZE];
    telem_record_t r = rec(0x01020304u);
    r.lat_e7 = -1; /* 0xFFFFFFFF */
    r.state = 2;
    r.gps_fixes = 0xABCD;
    TEST_ASSERT_EQUAL_UINT32(TELEM_ENCODED_SIZE, telem_encode(&r, buf, sizeof buf));
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[11]);
    TEST_ASSERT_EQUAL_HEX8(2, buf[16]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[18]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[19]);
    TEST_ASSERT_EQUAL_UINT32(0, telem_encode(&r, buf, TELEM_ENCODED_SIZE - 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_wraps_around_end_of_array);
    RUN_TEST(test_overflow_drops_oldest);
    RUN_TEST(test_flush_all_when_online);
    RUN_TEST(test_flush_keeps_record_on_send_failure);
    RUN_TEST(test_flush_respects_max);
    RUN_TEST(test_flush_after_overflow_sends_newest_capacity);
    RUN_TEST(test_encode_little_endian);
    return UNITY_END();
}
