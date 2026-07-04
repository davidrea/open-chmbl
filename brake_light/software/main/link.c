/*
 * Link watchdog (DE-01 timestamping/counters + DE-03 placeholder failsafe).
 *
 * Owns sequence validation and last-rx timestamping for packets accepted
 * from the paired peer (net.c only checks sender identity) and, on a
 * timer, decides link status from the age of the last accepted packet.
 * While the link is up it mirrors the received braking state onto the
 * stand-in brake light (cmd_light.c); otherwise it blinks that same LED as
 * a link-lost/waiting placeholder. The real DE-03 visual (running light +
 * a distinct fault blink) and the DE-10 status indicator land once there's
 * a second LED to carry them.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "console.h"
#include "link.h"

static bool s_has_rx;
static uint16_t s_last_seq;
static brake_state_t s_last_state = ST_OFF;
static int64_t s_last_rx_time_ms;
static uint32_t s_rx_count;
static uint32_t s_drop_stale;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static link_status_t compute_status(int64_t *age_ms_out)
{
    if (!s_has_rx) {
        if (age_ms_out) {
            *age_ms_out = -1;
        }
        return LINK_WAITING;
    }
    int64_t age = now_ms() - s_last_rx_time_ms;
    if (age_ms_out) {
        *age_ms_out = age;
    }
    return (age > CONFIG_CHMBL_LINK_TIMEOUT_MS) ? LINK_LOST : LINK_UP;
}

static void link_watchdog_task(void *arg)
{
    (void)arg;
    bool blink_phase = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CHMBL_LINK_BLINK_MS));

        link_status_t status = compute_status(NULL);
        if (status == LINK_UP) {
            light_set(s_last_state != ST_OFF);
        } else {
            blink_phase = !blink_phase;
            light_set(blink_phase);
        }
    }
}

void link_init(void)
{
    xTaskCreate(link_watchdog_task, "link_wd", 2048, NULL, 4, NULL);
}

void link_on_rx(uint16_t seq, brake_state_t state)
{
    if (s_has_rx) {
        int16_t diff = (int16_t)(seq - s_last_seq);
        if (diff <= 0) {
            s_drop_stale++;
            return;
        }
    }
    s_last_seq = seq;
    s_last_state = state;
    s_last_rx_time_ms = now_ms();
    s_has_rx = true;
    s_rx_count++;
}

void link_get_info(link_info_t *info)
{
    int64_t age;
    info->status = compute_status(&age);
    info->last_state = s_last_state;
    info->last_seq = s_last_seq;
    info->last_rx_age_ms = (age > INT32_MAX) ? INT32_MAX : (int32_t)age;
    info->rx_count = s_rx_count;
    info->drop_stale = s_drop_stale;
    info->timeout_ms = CONFIG_CHMBL_LINK_TIMEOUT_MS;
}
