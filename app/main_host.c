/*
 * main_host.c - PC simulator entry point.
 *
 * usage: collar_sim <track.nmea> <events.csv> [--radio-offline FROM_S:TO_S]
 *                   [--confirm N] [--quiet]
 *
 * A simple single-threaded loop stands in for the two FreeRTOS tasks used
 * on the ESP32: each GPS line is processed, then the telemetry service runs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collar_app.h"
#include "hal.h"
#include "hal_host.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: collar_sim <track.nmea> <events.csv> [--radio-offline FROM_S:TO_S]"
            " [--confirm N] [--quiet]\n");
}

int main(int argc, char **argv)
{
    hal_host_config_t cfg;
    collar_app_t app;
    telem_service_t telem;
    uint8_t confirm = 0;
    char line[128];
    int i;

    if (argc < 3) {
        usage();
        return 2;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.track_path = argv[1];
    cfg.log_path = argv[2];

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) {
            cfg.quiet = true;
        } else if (strcmp(argv[i], "--confirm") == 0 && i + 1 < argc) {
            confirm = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--radio-offline") == 0 && i + 1 < argc) {
            unsigned long from_s, to_s;
            if (sscanf(argv[++i], "%lu:%lu", &from_s, &to_s) != 2 || to_s < from_s) {
                usage();
                return 2;
            }
            cfg.radio_off_from_ms = (uint32_t)(from_s * 1000u);
            cfg.radio_off_to_ms = (uint32_t)(to_s * 1000u);
        } else {
            usage();
            return 2;
        }
    }

    if (!hal_host_open(&cfg)) return 1;
    telem_service_init(&telem);
    if (!collar_app_init(&app, confirm)) {
        hal_host_close();
        return 1;
    }

    while (hal_gps_read_line(line, sizeof line)) {
        telem_record_t rec;
        if (collar_app_handle_line(&app, line, &rec)) {
            telem_service_enqueue(&telem, &rec);
        }
        telem_service_poll(&telem);
    }

    collar_app_log_summary(&app, &telem);
    hal_host_close();
    return 0;
}
