/*
 * telemetry.c - see telemetry.h.
 */
#include "telemetry.h"

void telem_init(telem_ring_t *r)
{
    r->head = 0;
    r->count = 0;
    r->dropped = 0;
}

bool telem_push(telem_ring_t *r, const telem_record_t *rec)
{
    bool kept_all = true;

    if (r->count == TELEM_CAPACITY) {
        /* Full: drop the oldest by moving head forward one slot. */
        r->head = (uint16_t)((r->head + 1u) % TELEM_CAPACITY);
        r->count--;
        r->dropped++;
        kept_all = false;
    }
    /* Tail (next free slot) is head + count, wrapped around. */
    r->items[(r->head + r->count) % TELEM_CAPACITY] = *rec;
    r->count++;
    return kept_all;
}

bool telem_peek(const telem_ring_t *r, telem_record_t *out)
{
    if (r->count == 0) return false;
    *out = r->items[r->head];
    return true;
}

bool telem_pop(telem_ring_t *r, telem_record_t *out)
{
    if (r->count == 0) return false;
    if (out != 0) *out = r->items[r->head];
    r->head = (uint16_t)((r->head + 1u) % TELEM_CAPACITY);
    r->count--;
    return true;
}

uint16_t telem_count(const telem_ring_t *r)
{
    return r->count;
}

uint16_t telem_flush(telem_ring_t *r, telem_send_fn send, void *ctx, uint16_t max_records)
{
    uint16_t sent = 0;
    telem_record_t rec;

    while (sent < max_records && telem_peek(r, &rec)) {
        if (!send(&rec, ctx)) break; /* radio failed: keep record for later */
        (void)telem_pop(r, 0);
        sent++;
    }
    return sent;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

size_t telem_encode(const telem_record_t *rec, uint8_t *buf, size_t buf_len)
{
    if (buf_len < TELEM_ENCODED_SIZE) return 0;
    put_u32(&buf[0], rec->seq);
    put_u32(&buf[4], rec->t_ms);
    put_u32(&buf[8], (uint32_t)rec->lat_e7);
    put_u32(&buf[12], (uint32_t)rec->lon_e7);
    buf[16] = rec->state;
    buf[17] = rec->fix_valid;
    buf[18] = (uint8_t)(rec->gps_fixes);
    buf[19] = (uint8_t)(rec->gps_fixes >> 8);
    return TELEM_ENCODED_SIZE;
}
