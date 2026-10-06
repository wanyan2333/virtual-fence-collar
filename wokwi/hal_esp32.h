/*
 * hal_esp32.h - ESP32 (Arduino framework) specific HAL setup.
 */
#ifndef HAL_ESP32_H
#define HAL_ESP32_H

#include <stdint.h>

/* Wokwi wiring (see wokwi/diagram.json) */
#define PIN_LED_AUDIO 26     /* yellow LED: stands in for the speaker      */
#define PIN_LED_VIBRATION 27 /* red LED: stands in for the vibration motor */
#define PIN_BUTTON_RADIO 14  /* push button to GND: toggles radio online   */

/* Simulated time runs this many times faster than real time, so a
 * 4-minute GPS track plays back in about a minute. */
#define SIM_SPEEDUP 4u

void hal_esp32_init(void);

/* Next line of the built-in NMEA track is "due" every this many real ms. */
uint32_t hal_esp32_gps_line_period_ms(void);

#endif /* HAL_ESP32_H */
