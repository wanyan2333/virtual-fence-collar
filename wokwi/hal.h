/*
 * hal.h - Hardware Abstraction Layer.
 *
 * The app only talks to hardware through these functions. There are two
 * implementations:
 *   hal/host/hal_host.c   - PC simulation (NMEA file in, CSV event log out)
 *   hal/esp32/hal_esp32.cpp - Arduino-ESP32 (Serial, LEDs, button)
 * Swapping the HAL is how the same app code runs in both places.
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Milliseconds since boot. Wraps after ~49 days; compare with subtraction. */
uint32_t hal_time_ms(void);

/* Copy the next NMEA line (without CR/LF) into buf, NUL-terminated.
 * Returns false when no line is available (host: end of file). */
bool hal_gps_read_line(char *buf, size_t buf_len);

/* Short sound: "you are near the fence". */
void hal_audio_cue(void);

/* Vibration pulse, level 1 (gentle) .. 3 (strongest). */
void hal_vibration_cue(uint8_t level);

bool hal_radio_is_online(void);

/* Send one encoded telemetry frame. Returns false if it was not sent. */
bool hal_radio_send(const uint8_t *data, size_t len);

/* Structured event log: event name + "key=value;key=value" details. */
void hal_log(const char *event, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#ifdef __cplusplus
}
#endif

#endif /* HAL_H */
