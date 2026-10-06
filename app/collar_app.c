/*
 * collar_app.c - see collar_app.h.
 */
#include "collar_app.h"

#include "fence_config.h"
#include "hal.h"

bool collar_app_init(collar_app_t *app, uint8_t confirm_fixes)
{
    const nmea_stats_t zero_stats = { 0, 0, 0 };

    collar_sm_init(&app->sm, confirm_fixes);
    power_init(&app->power);
    app->nmea_stats = zero_stats;
    app->sampled_once = false;
    app->next_sample_ms = 0;
    app->interval_ms = power_interval_ms(&app->power);
    app->telem_started = false;
    app->last_telem_ms = 0;
    app->next_seq = 0;
    app->gps_samples = 0;
    app->audio_cues = 0;
    app->vibration_cues = 0;
    app->transitions = 0;
    app->last_satellites = 0;
    app->last_position = FENCE_VERTICES[0];

    if (!geofence_init(&app->fence, FENCE_VERTICES, FENCE_VERTEX_COUNT)) {
        hal_log("ERROR", "msg=invalid fence");
        return false;
    }
    hal_log("BOOT", "fence_vertices=%u;confirm_fixes=%u;warn_dist_m=%.1f",
            (unsigned)FENCE_VERTEX_COUNT, (unsigned)app->sm.confirm_fixes,
            (double)COLLAR_WARN_DIST_M);
    return true;
}

static int32_t deg_to_e7(double deg)
{
    const double scaled = deg * 1e7;
    return (int32_t)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

/* Run one GPS sample through geofence -> state machine -> cues -> power. */
static collar_output_t process_fix(collar_app_t *app, const nmea_fix_t *fix, uint32_t now)
{
    collar_input_t in;
    collar_output_t out;
    local_point_t pos = { 0.0f, 0.0f };
    uint32_t new_interval;

    in.fix_valid = fix->fix_valid;
    in.inside = false;
    in.dist_to_edge_m = 0.0f;
    in.now_ms = now;

    if (fix->fix_valid) {
        geo_point_t g;
        g.lat_deg = fix->lat_deg;
        g.lon_deg = fix->lon_deg;
        app->last_position = g;
        pos = geofence_to_local(&app->fence, g);
        in.inside = geofence_contains(&app->fence, pos);
        in.dist_to_edge_m = geofence_distance_to_edge(&app->fence, pos);
    }

    out = collar_sm_update(&app->sm, &in);

    if (fix->fix_valid) {
        hal_log("FIX", "lat=%.7f;lon=%.7f;x=%.2f;y=%.2f;inside=%d;dist=%.2f;state=%s",
                fix->lat_deg, fix->lon_deg, (double)pos.x, (double)pos.y, in.inside ? 1 : 0,
                (double)in.dist_to_edge_m, collar_state_name(out.state));
    } else {
        hal_log("NO_FIX", "state=%s", collar_state_name(out.state));
    }

    if (out.state_changed) {
        app->transitions++;
        hal_log("STATE", "from=%s;to=%s", collar_state_name(out.prev_state),
                collar_state_name(out.state));
    }
    if (out.cue == COLLAR_CUE_AUDIO) {
        app->audio_cues++;
        hal_audio_cue();
    } else if (out.cue == COLLAR_CUE_VIBRATION) {
        app->vibration_cues++;
        hal_vibration_cue(out.vib_level);
    }

    new_interval = power_update(&app->power, fix->fix_valid, pos, out.state, now);
    if (new_interval != app->interval_ms) {
        app->interval_ms = new_interval;
        hal_log("GPS_INTERVAL", "ms=%lu;mode=%s", (unsigned long)new_interval,
                power_mode_name(app->power.mode));
    }
    return out;
}

bool collar_app_handle_line(collar_app_t *app, const char *line, telem_record_t *rec)
{
    const uint32_t now = hal_time_ms();
    nmea_fix_t fix;
    collar_output_t out;
    nmea_result_t r = nmea_parse(line, &fix, &app->nmea_stats);

    if (r != NMEA_OK) {
        if (r != NMEA_ERR_UNSUPPORTED) {
            hal_log("NMEA_REJECT", "reason=%s;rejected_total=%lu", nmea_result_name(r),
                    (unsigned long)app->nmea_stats.rejected);
        }
        return false;
    }
    if (fix.type == NMEA_SENTENCE_GGA) {
        app->last_satellites = fix.satellites; /* RMC drives the logic */
        return false;
    }

    /* Duty cycling: between samples the GPS would be powered down, so ignore
     * the fixes it would not have produced. Signed difference = wrap-safe. */
    if (app->sampled_once && (int32_t)(now - app->next_sample_ms) < 0) {
        return false;
    }
    app->sampled_once = true;
    app->gps_samples++;

    out = process_fix(app, &fix, now);
    app->next_sample_ms = now + app->interval_ms;

    /* Telemetry: periodic, plus immediately on any state change. */
    if (!out.state_changed && app->telem_started &&
        (uint32_t)(now - app->last_telem_ms) < APP_TELEM_PERIOD_MS) {
        return false;
    }
    app->telem_started = true;
    app->last_telem_ms = now;
    rec->seq = app->next_seq++;
    rec->t_ms = now;
    rec->lat_e7 = deg_to_e7(app->last_position.lat_deg);
    rec->lon_e7 = deg_to_e7(app->last_position.lon_deg);
    rec->state = (uint8_t)out.state;
    rec->fix_valid = fix.fix_valid ? 1u : 0u;
    rec->gps_fixes = (uint16_t)app->gps_samples;
    return true;
}

void collar_app_log_summary(const collar_app_t *app, const telem_service_t *ts)
{
    hal_log("SUMMARY",
            "gps_samples=%lu;nmea_ok=%lu;nmea_rejected=%lu;transitions=%lu;audio=%lu;"
            "vibration=%lu;telem_sent=%lu;telem_dropped=%lu;telem_queued=%u;final_state=%s",
            (unsigned long)app->gps_samples, (unsigned long)app->nmea_stats.accepted,
            (unsigned long)app->nmea_stats.rejected, (unsigned long)app->transitions,
            (unsigned long)app->audio_cues, (unsigned long)app->vibration_cues,
            (unsigned long)ts->sent, (unsigned long)ts->ring.dropped,
            (unsigned)telem_count(&ts->ring), collar_state_name(app->sm.state));
}

/* ---- telemetry service ---------------------------------------------------- */

static bool radio_send_record(const telem_record_t *rec, void *ctx)
{
    uint8_t frame[TELEM_ENCODED_SIZE];
    const size_t n = telem_encode(rec, frame, sizeof frame);
    (void)ctx;
    return n > 0 && hal_radio_send(frame, n);
}

void telem_service_init(telem_service_t *ts)
{
    telem_init(&ts->ring);
    ts->radio_online = true;
    ts->sent = 0;
}

void telem_service_enqueue(telem_service_t *ts, const telem_record_t *rec)
{
    if (!telem_push(&ts->ring, rec)) {
        hal_log("TELEM_DROP", "dropped_total=%lu;queued=%u", (unsigned long)ts->ring.dropped,
                (unsigned)telem_count(&ts->ring));
    }
}

void telem_service_poll(telem_service_t *ts)
{
    const bool online = hal_radio_is_online();
    telem_record_t first;
    uint16_t sent;

    if (online != ts->radio_online) {
        ts->radio_online = online;
        hal_log("RADIO", "online=%d;queued=%u", online ? 1 : 0, (unsigned)telem_count(&ts->ring));
    }
    if (!online || !telem_peek(&ts->ring, &first)) return;

    sent = telem_flush(&ts->ring, radio_send_record, 0, TELEM_CAPACITY);
    if (sent > 0) {
        ts->sent += sent;
        hal_log("TELEM_FLUSH", "sent=%u;first_seq=%lu;remaining=%u", (unsigned)sent,
                (unsigned long)first.seq, (unsigned)telem_count(&ts->ring));
    }
}
