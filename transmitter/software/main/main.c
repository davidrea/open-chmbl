/*
 * transmitter (bike-side) firmware — entry point.
 *
 * Runs on the logger PCB (ESP32-S3-WROOM-1-N4, logger/hardware/), which is
 * also the transmitter's hardware: the same board with the microSD slot (J5)
 * and, in the final build, the remote pod connector (J4) unpopulated. See
 * docs/hardware.md §1. While J4 *is* populated on the bench board, its LED
 * output (IO18) stands in as the brake light.
 *
 * Bring-up order matters:
 *   status_led   first, so a fault found later has somewhere to show up;
 *   can_rx       parks the transceiver's silent pin before anything can
 *                touch the bus, then installs TWAI listen-only;
 *   brake_ctl    starts the 50 Hz DE-09 tick that drives the light;
 *   console      last, and only when CONFIG_CHMBL_CLI is set — everything
 *                above is real functionality and comes up regardless.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "brake_ctl.h"
#include "can_rx.h"
#include "console.h"
#include "net.h"
#include "pairing.h"
#include "status_led.h"

static const char *TAG = "transmitter";

void app_main(void)
{
    ESP_LOGI(TAG, "transmitter starting — logger PCB (ESP32-S3)");

    status_led_init();
    pairing_init();
    net_init();
    can_rx_init();
    brake_ctl_init();

#if CONFIG_CHMBL_CLI
    console_start();
#else
    ESP_LOGW(TAG, "developer CLI disabled (CONFIG_CHMBL_CLI=n)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
