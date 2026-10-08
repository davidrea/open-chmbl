/*
 * Status indicator — minimal DE-10 slice. Drives D13 (RED) / D14 (GRN) from
 * link health so DE-03's "never silently dark, never a latched fake BRAKE"
 * requirement is met *without* ever blinking the brake bar.
 *
 * Code table (link health only, for now):
 *
 *   LINK_UP       GRN steady          link healthy, bar tracks the TX
 *   LINK_WAITING  GRN slow blink      booted, no packet accepted yet
 *   LINK_LOST     RED slow blink      heartbeat gone; bar held steady OFF
 *
 * The blink half-period is CONFIG_CHMBL_STATUS_BLINK_MS (>= 150 ms by
 * Kconfig range), so the indicator itself stays well clear of a strobe
 * (BL-IND-4) and is visibly different from the steady bar.
 */
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "status.h"

static const char *TAG = "status";

#define STATUS_GPIO_GRN (CONFIG_CHMBL_STATUS_LED_GRN_GPIO)
#define STATUS_GPIO_RED (CONFIG_CHMBL_STATUS_LED_RED_GPIO)

static volatile link_status_t s_link = LINK_WAITING;
static bool s_grn_on;
static bool s_red_on;

static void led_init(int gpio, const char *which)
{
    if (gpio < 0) {
        ESP_LOGW(TAG, "status LED %s not fitted (GPIO = -1)", which);
        return;
    }
    gpio_reset_pin((gpio_num_t)gpio);
    gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)gpio, 0);
    ESP_LOGI(TAG, "status LED %s on GPIO%d (active high)", which, gpio);
}

static void led_drive(int gpio, bool on)
{
    if (gpio >= 0) {
        gpio_set_level((gpio_num_t)gpio, on ? 1 : 0);
    }
}

static void status_task(void *arg)
{
    (void)arg;
    bool phase = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CHMBL_STATUS_BLINK_MS));
        phase = !phase;

        switch (s_link) {
        case LINK_UP:
            s_grn_on = true;
            s_red_on = false;
            break;
        case LINK_WAITING:
            s_grn_on = phase;
            s_red_on = false;
            break;
        case LINK_LOST:
        default:
            s_grn_on = false;
            s_red_on = phase;
            break;
        }

        led_drive(STATUS_GPIO_GRN, s_grn_on);
        led_drive(STATUS_GPIO_RED, s_red_on);
    }
}

void status_init(void)
{
    led_init(STATUS_GPIO_GRN, "GRN (D14)");
    led_init(STATUS_GPIO_RED, "RED (D13)");
    xTaskCreate(status_task, "status", 2048, NULL, 3, NULL);
}

void status_set_link(link_status_t link)
{
    s_link = link;
}

void status_get_info(status_info_t *info)
{
    info->link     = s_link;
    info->grn_on   = s_grn_on;
    info->red_on   = s_red_on;
    info->gpio_grn = STATUS_GPIO_GRN;
    info->gpio_red = STATUS_GPIO_RED;
    info->blink_ms = CONFIG_CHMBL_STATUS_BLINK_MS;

    switch (s_link) {
    case LINK_UP:      info->pattern = "GRN steady";     break;
    case LINK_WAITING: info->pattern = "GRN slow blink"; break;
    case LINK_LOST:    info->pattern = "RED slow blink"; break;
    default:           info->pattern = "?";              break;
    }
}
