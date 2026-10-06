/*
 * hal_host.c - HAL implementation for the PC simulator.
 *
 * Simulated GPS receiver: the NMEA file is replayed line by line. The
 * receiver is assumed to output GGA then RMC once per second, so the
 * simulated clock advances 1 s every time an RMC line is read (RMC ends
 * the epoch). Nothing here sleeps: a 10-minute track runs in milliseconds.
 *
 * Every hal_log() call becomes one CSV row: t_ms,event,detail
 */
#include "hal_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hal.h"

#define GPS_EPOCH_MS 1000u

static FILE *s_track;
static FILE *s_log;
static uint32_t s_now_ms;
static hal_host_config_t s_cfg;

bool hal_host_open(const hal_host_config_t *cfg)
{
    s_cfg = *cfg;
    s_now_ms = 0;
    s_track = fopen(cfg->track_path, "r");
    if (s_track == NULL) {
        fprintf(stderr, "cannot open track %s\n", cfg->track_path);
        return false;
    }
    s_log = fopen(cfg->log_path, "w");
    if (s_log == NULL) {
        fprintf(stderr, "cannot open log %s\n", cfg->log_path);
        fclose(s_track);
        return false;
    }
    fprintf(s_log, "t_ms,event,detail\n");
    return true;
}

void hal_host_close(void)
{
    if (s_track) fclose(s_track);
    if (s_log) fclose(s_log);
    s_track = NULL;
    s_log = NULL;
}

uint32_t hal_time_ms(void)
{
    return s_now_ms;
}

bool hal_gps_read_line(char *buf, size_t buf_len)
{
    while (s_track != NULL && fgets(buf, (int)buf_len, s_track) != NULL) {
        size_t n = strlen(buf);
        if (n > 0 && buf[n - 1] != '\n' && !feof(s_track)) {
            /* Line longer than buf: drop the rest of it. The truncated part
             * is still returned and the parser will reject it. */
            int c;
            while ((c = fgetc(s_track)) != EOF && c != '\n') {
            }
        }
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        if (n == 0) continue; /* skip blank lines */

        if (strstr(buf, "RMC") != NULL) s_now_ms += GPS_EPOCH_MS;
        return true;
    }
    return false;
}

void hal_audio_cue(void)
{
    hal_log("AUDIO_CUE", "tone=1");
}

void hal_vibration_cue(uint8_t level)
{
    hal_log("VIBRATION_CUE", "level=%u", (unsigned)level);
}

bool hal_radio_is_online(void)
{
    if (s_cfg.radio_off_from_ms == s_cfg.radio_off_to_ms) return true;
    return !(s_now_ms >= s_cfg.radio_off_from_ms && s_now_ms < s_cfg.radio_off_to_ms);
}

bool hal_radio_send(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return hal_radio_is_online(); /* a real radio could also fail randomly */
}

void hal_log(const char *event, const char *fmt, ...)
{
    char detail[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);

    if (s_log) fprintf(s_log, "%lu,%s,%s\n", (unsigned long)s_now_ms, event, detail);
    if (!s_cfg.quiet) printf("[%8lu ms] %-14s %s\n", (unsigned long)s_now_ms, event, detail);
}
