/*
 * transmitter (bike-side) firmware — entry point.
 *
 * Current stage: developer console (DE-00) + ESP-NOW link (DE-01). The real
 * bike-side firmware (TWAI/CAN listen-only + braking state machine) lands on
 * top of this. The stand-in state indicator and the ESP-NOW pairing/
 * heartbeat link come up unconditionally — they're the actual DE-01
 * functionality, not a debug feature — and the dev CLI (gated by
 * CONFIG_CHMBL_CLI) is layered on top to fake/inspect them over the console
 * (see ../../../docs/cli.md).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "console.h"
#include "pairing.h"
#include "net.h"
#include "can_rx.h"

static const char *TAG = "transmitter";

void app_main(void)
{
    ESP_LOGI(TAG, "transmitter starting");

    state_init();
    pairing_init();
    net_init();
    can_rx_init();

#if CONFIG_CHMBL_CLI
    console_start();
#else
    ESP_LOGW(TAG, "developer CLI disabled (CONFIG_CHMBL_CLI=n)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
