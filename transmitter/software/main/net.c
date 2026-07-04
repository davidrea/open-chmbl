/*
 * ESP-NOW heartbeat transmit (DE-01 / TX-NET).
 *
 * Broadcasts the current braking state (docs/protocol.md §2) to the paired
 * brake_light at a configurable rate. `net stop` pauses transmission
 * without tearing the task down, so the link stays pair-able and `net show`
 * stays live while exercising the brake_light's link-loss behaviour
 * (DE-03 placeholder) from this side.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_now.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "protocol.h"
#include "pairing.h"
#include "console.h"
#include "net.h"

static const char *TAG = "net";

static volatile bool s_running = true;
static volatile uint8_t s_rate_hz = CONFIG_CHMBL_NET_RATE_HZ;
static uint16_t s_seq;
static uint32_t s_sent_ok;
static uint32_t s_sent_fail;

static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    (void)mac_addr;
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_sent_ok++;
    } else {
        s_sent_fail++;
    }
}

static bool send_heartbeat(void)
{
    uint8_t peer[6];
    if (!pairing_get_peer(peer)) {
        return false;
    }
    chmbl_msg_t msg = {
        .version  = CHMBL_PROTOCOL_VERSION,
        .msg_type = MSG_HEARTBEAT,
        .seq      = (uint16_t)(s_seq + 1),
        .state    = (uint8_t)state_get(),
    };
    s_seq = msg.seq;
    return esp_now_send(peer, (const uint8_t *)&msg, sizeof(msg)) == ESP_OK;
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_running) {
            send_heartbeat();
        }
        uint8_t hz = s_rate_hz ? s_rate_hz : 1;
        vTaskDelay(pdMS_TO_TICKS(1000 / hz));
    }
}

void net_init(void)
{
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    xTaskCreate(heartbeat_task, "net_hb", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "heartbeat task started at %u Hz", s_rate_hz);
}

bool net_is_running(void)
{
    return s_running;
}

void net_start(void)
{
    s_running = true;
}

void net_stop(void)
{
    s_running = false;
}

uint8_t net_get_rate(void)
{
    return s_rate_hz;
}

void net_set_rate(uint8_t hz)
{
    if (hz >= 1 && hz <= 50) {
        s_rate_hz = hz;
    }
}

bool net_send_now(void)
{
    return send_heartbeat();
}

void net_get_stats(uint16_t *seq, uint32_t *sent_ok, uint32_t *sent_fail)
{
    if (seq)      *seq      = s_seq;
    if (sent_ok)  *sent_ok  = s_sent_ok;
    if (sent_fail) *sent_fail = s_sent_fail;
}
