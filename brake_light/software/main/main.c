/*
 * brake_light (helmet-side) firmware — entry point.
 *
 * Current stage: developer console (DE-00) + ESP-NOW link (DE-01) + the
 * link-loss failsafe (DE-03) + the binary brake-bar render stage (DE-04) and
 * a minimal link-health status indicator (DE-10 slice). The brake bar, the
 * status LEDs, ESP-NOW pairing/receive and the link watchdog all come up
 * unconditionally — they're the actual functionality, not a debug feature —
 * and the dev CLI (gated by CONFIG_CHMBL_CLI) is layered on top to
 * fake/inspect them over the console (see ../../../docs/cli.md).
 *
 * Order matters: render_init() and status_init() configure the outputs (and
 * drive them dark) before link_init() starts publishing into them.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "console.h"
#include "pairing.h"
#include "net.h"
#include "link.h"
#include "render.h"
#include "status.h"

static const char *TAG = "brake_light";

void app_main(void)
{
    ESP_LOGI(TAG, "brake_light starting");

    render_init();
    status_init();
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
