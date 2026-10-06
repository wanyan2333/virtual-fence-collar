/*
 * hal_esp32.cpp - HAL implementation for Arduino-ESP32 (runs on FreeRTOS).
 *
 * - GPS:   replays the built-in NMEA track (track_data.h), no GPS part needed
 * - Audio: yellow LED pulse;  Vibration: red LED pulse (longer = stronger)
 * - Radio: button toggles online/offline (interrupt + volatile flag)
 * - Log:   Serial, protected by a mutex because two tasks print
 */
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "hal.h"
#include "hal_esp32.h"
#include "track_data.h"

/* Written by the button ISR, read by the telemetry task. `volatile` tells
 * the compiler the value can change outside normal program flow, so it must
 * re-read it from memory every time instead of caching it in a register. */
static volatile bool s_radio_online = true;
static volatile uint32_t s_last_press_ms = 0;

static size_t s_track_index = 0;
static SemaphoreHandle_t s_log_mutex;
static TimerHandle_t s_audio_timer;
static TimerHandle_t s_vib_timer;

static void IRAM_ATTR on_button(void)
{
    const uint32_t now = millis();
    if (now - s_last_press_ms > 250u) { /* simple debounce */
        s_radio_online = !s_radio_online;
        s_last_press_ms = now;
    }
}

/* One-shot software timers switch the LEDs off again, so a cue never
 * blocks the GPS task with delay(). */
static void audio_off(TimerHandle_t t)
{
    (void)t;
    digitalWrite(PIN_LED_AUDIO, LOW);
}

static void vib_off(TimerHandle_t t)
{
    (void)t;
    digitalWrite(PIN_LED_VIBRATION, LOW);
}

void hal_esp32_init(void)
{
    pinMode(PIN_LED_AUDIO, OUTPUT);
    pinMode(PIN_LED_VIBRATION, OUTPUT);
    pinMode(PIN_BUTTON_RADIO, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_RADIO), on_button, FALLING);

    s_log_mutex = xSemaphoreCreateMutex();
    s_audio_timer = xTimerCreate("aud", pdMS_TO_TICKS(300), pdFALSE, NULL, audio_off);
    s_vib_timer = xTimerCreate("vib", pdMS_TO_TICKS(300), pdFALSE, NULL, vib_off);
}

uint32_t hal_esp32_gps_line_period_ms(void)
{
    /* GGA + RMC per 1 s epoch = 2 lines per simulated second. */
    return 500u / SIM_SPEEDUP;
}

uint32_t hal_time_ms(void)
{
    return (uint32_t)(millis() * SIM_SPEEDUP);
}

bool hal_gps_read_line(char *buf, size_t buf_len)
{
    if (s_track_index >= TRACK_LINE_COUNT || buf_len == 0) return false;
    strncpy(buf, TRACK_LINES[s_track_index++], buf_len - 1);
    buf[buf_len - 1] = '\0';
    return true;
}

void hal_audio_cue(void)
{
    digitalWrite(PIN_LED_AUDIO, HIGH);
    xTimerStart(s_audio_timer, 0);
    hal_log("AUDIO_CUE", "tone=1");
}

void hal_vibration_cue(uint8_t level)
{
    digitalWrite(PIN_LED_VIBRATION, HIGH);
    xTimerChangePeriod(s_vib_timer, pdMS_TO_TICKS(300u * level), 0); /* also starts it */
    hal_log("VIBRATION_CUE", "level=%u", (unsigned)level);
}

bool hal_radio_is_online(void)
{
    return s_radio_online;
}

bool hal_radio_send(const uint8_t *data, size_t len)
{
    char hex[2 * 32 + 1];
    size_t i;

    if (!s_radio_online) return false;
    for (i = 0; i < len && i < 32; i++) snprintf(&hex[2 * i], 3, "%02X", data[i]);
    hex[2 * i] = '\0';
    hal_log("RADIO_TX", "bytes=%u;frame=%s", (unsigned)len, hex);
    return true;
}

void hal_log(const char *event, const char *fmt, ...)
{
    char detail[192];
    va_list args;

    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);

    if (s_log_mutex) xSemaphoreTake(s_log_mutex, portMAX_DELAY);
    Serial.printf("[%8lu ms] %-14s %s\n", (unsigned long)hal_time_ms(), event, detail);
    if (s_log_mutex) xSemaphoreGive(s_log_mutex);
}
