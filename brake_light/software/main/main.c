/*
 * brake_light (helmet-side) firmware — entry point.
 *
 * Current stage: developer console (DE-00) + ESP-NOW link (DE-01) + a
 * link-loss placeholder (DE-03). The real helmet-side firmware (LED pattern
 * engine, DE-04) lands on top of this. The stand-in brake light, ESP-NOW
 * pairing/receive, and the link watchdog come up unconditionally — they're
 * the actual DE-01/DE-03 functionality, not a debug feature — and the dev
 * CLI (gated by CONFIG_CHMBL_CLI) is layered on top to fake/inspect them
 * over the console (see ../../../docs/cli.md).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "console.h"
#include "pairing.h"
#include "net.h"
#include "link.h"

static const char *TAG = "brake_light";

void app_main(void)
{
    ESP_LOGI(TAG, "brake_light starting");

    light_init();
    pairing_init();
    net_init();
    link_init();

#if CONFIG_CHMBL_CLI
    console_start();
#else
    ESP_LOGW(TAG, "developer CLI disabled (CONFIG_CHMBL_CLI=n)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
