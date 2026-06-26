/*
 * brake_light (helmet-side) firmware — bring-up blink.
 *
 * Minimal ESP-IDF baseline: toggles a single GPIO LED so we can confirm the
 * toolchain, board bring-up, and CI build all work end to end. The real
 * helmet-side firmware (ESP-NOW RX + LED pattern engine) lands on top of this.
 *
 * See ../../../docs/firmware.md §2 for the planned architecture.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "brake_light";

#define BLINK_GPIO      CONFIG_BLINK_GPIO
#define BLINK_PERIOD_MS CONFIG_BLINK_PERIOD_MS

void app_main(void)
{
    ESP_LOGI(TAG, "brake_light bring-up blink on GPIO%d", BLINK_GPIO);

    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    bool led_on = false;
    while (1) {
        led_on = !led_on;
        gpio_set_level(BLINK_GPIO, led_on);
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS / 2));
    }
}
