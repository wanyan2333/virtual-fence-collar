/*
 * hal_host.h - Extra setup functions only the PC simulation needs.
 */
#ifndef HAL_HOST_H
#define HAL_HOST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *track_path;      /* NMEA file used as the GPS stream       */
    const char *log_path;        /* CSV event log to write                 */
    uint32_t radio_off_from_ms;  /* radio offline in [from, to) sim time   */
    uint32_t radio_off_to_ms;    /* from == to means always online         */
    bool quiet;                  /* don't echo events to stdout            */
} hal_host_config_t;

bool hal_host_open(const hal_host_config_t *cfg);
void hal_host_close(void);

#endif /* HAL_HOST_H */
