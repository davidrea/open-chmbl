#include "fault_led.h"

#include "sdkconfig.h"

#if CONFIG_LOGGER_FAULT_LED_ENABLE

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FAULT_LED_GPIO CONFIG_LOGGER_FAULT_LED_GPIO

/* 2 Hz, 50% duty — unmistakably a fault and not an activity flicker. */
#define BLINK_HALF_MS 250

static volatile bool s_fault;

static void fault_led_task(void *arg)
{
    (void)arg;
    bool on = false;
    for (;;) {
        if (s_fault) {
            on = !on;
        } else {
            on = false;
        }
        /* Active-high: the GPIO sources into D6 through R21. */
        gpio_set_level(FAULT_LED_GPIO, on ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(BLINK_HALF_MS));
    }
}

void fault_led_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << FAULT_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(FAULT_LED_GPIO, 0);

    xTaskCreate(fault_led_task, "fault_led", 2048, NULL, 2, NULL);
}

void fault_led_set(bool fault)
{
    s_fault = fault;
}

#else  /* !CONFIG_LOGGER_FAULT_LED_ENABLE */

/* Disabled: no task, no GPIO touched. Callers stay unconditional. */
void fault_led_init(void) {}
void fault_led_set(bool fault) { (void)fault; }

#endif
