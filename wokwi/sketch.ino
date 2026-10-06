/*
 * sketch.ino - ESP32 entry point (Arduino-ESP32, which runs on FreeRTOS).
 *
 * Two tasks connected by a queue:
 *
 *   gps_fence_task (prio 2)                    telemetry_task (prio 1)
 *   NMEA line -> geofence -> state machine     owns the ring buffer + radio
 *   -> cues, produces telemetry records  --->  queue -> ring -> flush when online
 *                                    xQueueSend
 *
 * The ring buffer is only touched by telemetry_task, so it needs no mutex.
 * Copied to wokwi/ by tools/sync_wokwi.py.
 */
#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "collar_app.h"
#include "hal.h"
#include "hal_esp32.h"

static QueueHandle_t s_telem_queue;
static collar_app_t s_app;         /* static: lives in .bss, not on a task stack */
static telem_service_t s_telem;

static void gps_fence_task(void *arg)
{
    char line[128];
    telem_record_t rec;
    TickType_t last_wake = xTaskGetTickCount();
    (void)arg;

    while (hal_gps_read_line(line, sizeof line)) {
        if (collar_app_handle_line(&s_app, line, &rec)) {
            /* Don't block the fence logic if the telemetry side is slow. */
            if (xQueueSend(s_telem_queue, &rec, 0) != pdTRUE) {
                hal_log("QUEUE_FULL", "seq=%lu", (unsigned long)rec.seq);
            }
        }
        /* Fixed-rate replay: wake exactly every line period. */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(hal_esp32_gps_line_period_ms()));
    }
    vTaskDelay(pdMS_TO_TICKS(500)); /* let telemetry drain the queue */
    collar_app_log_summary(&s_app, &s_telem);
    hal_log("TRACK_END", "msg=press the green button or restart the simulation");
    vTaskDelete(NULL);
}

static void telemetry_task(void *arg)
{
    telem_record_t rec;
    (void)arg;

    for (;;) {
        /* Wait up to 200 ms for a record, then service the radio anyway so
         * a button press (radio back online) is noticed quickly. */
        if (xQueueReceive(s_telem_queue, &rec, pdMS_TO_TICKS(200)) == pdTRUE) {
            telem_service_enqueue(&s_telem, &rec);
        }
        telem_service_poll(&s_telem);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    hal_esp32_init();
    hal_log("HELLO", "msg=virtual fence collar demo;speedup=%u", (unsigned)SIM_SPEEDUP);

    telem_service_init(&s_telem);
    if (!collar_app_init(&s_app, 0)) {
        for (;;) delay(1000);
    }

    s_telem_queue = xQueueCreate(8, sizeof(telem_record_t));
    xTaskCreate(gps_fence_task, "gps_fence", 6144, NULL, 2, NULL);
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 1, NULL);
}

void loop()
{
    /* All work happens in the FreeRTOS tasks above. */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
