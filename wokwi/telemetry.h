/*
 * telemetry.h - Fixed-size ring buffer of telemetry records.
 *
 * Records queue up while the radio is offline. When the buffer is full the
 * OLDEST record is overwritten (newest data is most useful to the farmer)
 * and the drop is counted. When the radio is back, flush oldest-first.
 *
 * Not thread-safe by design: on the ESP32 only the telemetry task touches
 * the ring; other tasks hand records over through a FreeRTOS queue.
 */
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEM_CAPACITY 32u
#define TELEM_ENCODED_SIZE 20u

typedef struct {
    uint32_t seq;        /* increasing sequence number, shows gaps/drops */
    uint32_t t_ms;       /* collar uptime when recorded                  */
    int32_t lat_e7;      /* latitude  * 1e7 (integer, ~1 cm resolution)  */
    int32_t lon_e7;      /* longitude * 1e7                              */
    uint8_t state;       /* collar_state_t                               */
    uint8_t fix_valid;   /* 1 if position is from a valid fix            */
    uint16_t gps_fixes;  /* GPS samples taken: "battery-ish" energy proxy */
} telem_record_t;

typedef struct {
    telem_record_t items[TELEM_CAPACITY];
    uint16_t head;       /* index of the oldest record */
    uint16_t count;      /* records currently stored   */
    uint32_t dropped;    /* total records overwritten  */
} telem_ring_t;

/* Callback used by telem_flush; return true if the record was sent. */
typedef bool (*telem_send_fn)(const telem_record_t *rec, void *ctx);

void telem_init(telem_ring_t *r);

/* Add a record. Returns false if the buffer was full and the oldest
 * record had to be dropped to make room (the new record is always kept). */
bool telem_push(telem_ring_t *r, const telem_record_t *rec);

/* Copy the oldest record without removing it. False if empty. */
bool telem_peek(const telem_ring_t *r, telem_record_t *out);

/* Remove the oldest record (optionally copying it out). False if empty. */
bool telem_pop(telem_ring_t *r, telem_record_t *out);

uint16_t telem_count(const telem_ring_t *r);

/* Send up to max_records, oldest first. A record is only removed after
 * send() succeeds; on failure we stop and keep it. Returns records sent. */
uint16_t telem_flush(telem_ring_t *r, telem_send_fn send, void *ctx, uint16_t max_records);

/* Serialise to a fixed little-endian byte layout (independent of the CPU's
 * endianness and struct padding). Returns bytes written, 0 if buf too small. */
size_t telem_encode(const telem_record_t *rec, uint8_t *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_H */
