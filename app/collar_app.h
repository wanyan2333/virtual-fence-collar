/*
 * collar_app.h - Glue between core logic and the HAL.
 *
 * Two independent pieces, so on the ESP32 they can live in two FreeRTOS
 * tasks connected by a queue:
 *   collar_app_*     : GPS line -> NMEA -> geofence -> state machine -> cues,
 *                      power management, and producing telemetry records.
 *   telem_service_*  : owns the telemetry ring buffer and the radio.
 */
#ifndef COLLAR_APP_H
#define COLLAR_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "collar_sm.h"
#include "geofence.h"
#include "nmea.h"
#include "power.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_TELEM_PERIOD_MS 5000u /* one telemetry record every 5 s */

typedef struct {
    geofence_t fence;
    collar_sm_t sm;
    power_ctrl_t power;
    nmea_stats_t nmea_stats;

    bool sampled_once;        /* false until the first fix is processed  */
    uint32_t next_sample_ms;  /* GPS is "off" until this time             */
    uint32_t interval_ms;     /* current GPS sampling interval            */

    bool telem_started;
    uint32_t last_telem_ms;
    uint32_t next_seq;

    /* counters for the end-of-run summary */
    uint32_t gps_samples;     /* fixes actually processed (energy proxy)  */
    uint32_t audio_cues;
    uint32_t vibration_cues;
    uint32_t transitions;
    uint8_t last_satellites;
    geo_point_t last_position;  /* last valid fix, reported in telemetry  */
} collar_app_t;

typedef struct {
    telem_ring_t ring;
    bool radio_online;
    uint32_t sent;
} telem_service_t;

/* confirm_fixes: hysteresis depth for the state machine (0 = default). */
bool collar_app_init(collar_app_t *app, uint8_t confirm_fixes);

/* Process one NMEA line read at hal_time_ms(). Returns true and fills *rec
 * when a telemetry record should be queued. */
bool collar_app_handle_line(collar_app_t *app, const char *line, telem_record_t *rec);

void collar_app_log_summary(const collar_app_t *app, const telem_service_t *ts);

void telem_service_init(telem_service_t *ts);
void telem_service_enqueue(telem_service_t *ts, const telem_record_t *rec);
/* Check the radio; if online, flush queued records oldest-first. */
void telem_service_poll(telem_service_t *ts);

#ifdef __cplusplus
}
#endif

#endif /* COLLAR_APP_H */
