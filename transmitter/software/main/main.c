/*
 * transmitter (bike-side) firmware — entry point.
 *
 * Current stage: developer console (DE-00). The real bike-side firmware
 * (TWAI/CAN listen-only + braking state machine + ESP-NOW TX) lands on top of
 * this; for now app_main just brings up the CLI so each later design element
 * can be exercised in isolation over the console (see ../../../docs/cli.md).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "console.h"

static const char *TAG = "transmitter";

void app_main(void)
{
    ESP_LOGI(TAG, "transmitter starting");

#if CONFIG_CHMBL_CLI
    console_start();
#else
    ESP_LOGW(TAG, "developer CLI disabled (CONFIG_CHMBL_CLI=n)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
