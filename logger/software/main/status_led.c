#include "status_led.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define LED_GPIO CONFIG_LOGGER_STATUS_LED_GPIO

/* Poll granularity: how quickly the task notices a status_led_set() call
 * while mid-pattern. 20ms is imperceptible but keeps the task from blocking
 * in one long vTaskDelay that would delay reacting to a new state. */
#define LED_POLL_MS 20

static volatile led_state_t s_state = LED_STATE_IDLE;

static inline void led_write(bool on)
{
#if CONFIG_LOGGER_STATUS_LED_ACTIVE_LOW
    gpio_set_level(LED_GPIO, on ? 0 : 1);
#else
    gpio_set_level(LED_GPIO, on ? 1 : 0);
#endif
}

/* Sleep up to `ms`, in LED_POLL_MS steps, bailing out early if the state has
 * changed since the caller's pattern step started. Returns true if it bailed
 * out early (caller should abandon the rest of its pattern and re-evaluate). */
static bool led_wait(int ms, led_state_t entry_state)
{
    for (int waited = 0; waited < ms; waited += LED_POLL_MS) {
        if (s_state != entry_state) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(LED_POLL_MS));
    }
    return s_state != entry_state;
}

static void status_led_task(void *arg)
{
    (void)arg;
    for (;;) {
        led_state_t state = s_state;
        switch (state) {
        case LED_STATE_RECORDING:
            /* Solid on; re-check periodically for a state change. */
            led_write(true);
            led_wait(200, state);
            break;

        case LED_STATE_ERROR:
            /* Fast blink: unmistakably different from the other two states. */
            led_write(true);
            if (led_wait(100, state)) {
                break;
            }
            led_write(false);
            led_wait(100, state);
            break;

        case LED_STATE_IDLE:
        default:
            /* Slow heartbeat: alive and ready, but not recording. */
            led_write(true);
            if (led_wait(60, state)) {
                break;
            }
            led_write(false);
            led_wait(1940, state);
            break;
        }
    }
}

void status_led_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    led_write(false);

    xTaskCreate(status_led_task, "status_led", 2048, NULL, 2, NULL);
}

void status_led_set(led_state_t state)
{
    s_state = state;
}
