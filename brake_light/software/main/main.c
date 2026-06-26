/*
 * brake_light (helmet-side) firmware — entry point.
 *
 * Current stage: developer console (DE-00). The real helmet-side firmware
 * (ESP-NOW RX + LED pattern engine) lands on top of this; for now app_main
 * just brings up the CLI so each later design element can be exercised in
 * isolation over the console (see ../../../docs/cli.md).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "console.h"

static const char *TAG = "brake_light";

void app_main(void)
{
    ESP_LOGI(TAG, "brake_light starting");

#if CONFIG_CHMBL_CLI
    console_start();
#else
    ESP_LOGW(TAG, "developer CLI disabled (CONFIG_CHMBL_CLI=n)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
