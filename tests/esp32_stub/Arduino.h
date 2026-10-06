/* Minimal stand-ins for the Arduino-ESP32 API, ONLY for a host-side
 * compile check of hal_esp32.cpp and sketch.ino (g++ -fsyntax-only).
 * This is not an emulator; nothing here is ever linked or run. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define FALLING 3
unsigned long millis(void);
void delay(unsigned long ms);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalPinToInterrupt(int pin);
void attachInterrupt(int irq, void (*fn)(void), int mode);
struct HardwareSerial {
    void begin(unsigned long baud);
    int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};
extern HardwareSerial Serial;
