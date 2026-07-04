/*
 * ESP-NOW pairing (DE-01) — encrypted-peer setup between transmitter and
 * brake_light.
 *
 * The button-hold pairing ritual in docs/protocol.md §1 needs a button
 * neither dev board has wired up yet, so pairing is CLI-triggered instead:
 * running `pair start` on both boards within the same window has each side
 * broadcast an unencrypted MSG_PAIR announcement and listen for the other's;
 * whichever MAC shows up first becomes the peer, registered as an
 * *encrypted* ESP-NOW peer and persisted to NVS so later boots re-pair
 * silently, per the protocol doc.
 *
 * This file owns the process-wide esp_now recv callback (ESP-NOW allows
 * only one) and demuxes MSG_PAIR locally vs. forwarding data messages to
 * whatever the device registers via pairing_set_data_cb().
 */
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "protocol.h"
#include "pairing.h"

static const char *TAG = "pairing";

#define NVS_NAMESPACE "chmbl"
#define NVS_KEY_PEER  "peer_mac"

/* Extra broadcasts sent (200 ms apart) after discovering a peer, purely so
 * the other side has more chances to discover us back — see the comment in
 * pairing_start() where this is used. */
#define PAIR_GRACE_SENDS 8

/* Placeholder pairing key: a compiled-in constant so any two boards running
 * this firmware complete DE-01's encrypted-peer setup without a real
 * key-exchange step. Fine for bench bring-up; a per-pair random key
 * (exchanged during the pairing ritual) is a follow-up before this leaves
 * the bench — see docs/design/de-01-espnow-link.md open items. Exactly 16
 * bytes including the implicit NUL, matching esp_now_set_pmk()/peer.lmk. */
static const uint8_t CHMBL_PMK[16] = "CHMBL-DEV-PMK01";
static const uint8_t CHMBL_LMK[16] = "CHMBL-DEV-LMK01";

static const uint8_t s_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint8_t s_own_mac[6];
static uint8_t s_peer_mac[6];
static bool s_has_peer;

static volatile bool s_pairing_mode;
static QueueHandle_t s_pair_found_q;

static pairing_data_cb_t s_data_cb;

static void ensure_broadcast_peer(void)
{
    if (esp_now_is_peer_exist(s_broadcast_mac)) {
        return;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_broadcast_mac, 6);
    peer.channel = CONFIG_CHMBL_NET_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
}

static void register_encrypted_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        ESP_ERROR_CHECK(esp_now_del_peer(mac));
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    memcpy(peer.lmk, CHMBL_LMK, sizeof(peer.lmk));
    peer.channel = CONFIG_CHMBL_NET_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
}

static void save_peer_to_nvs(const uint8_t mac[6])
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed, peer not persisted");
        return;
    }
    nvs_set_blob(h, NVS_KEY_PEER, mac, 6);
    nvs_commit(h);
    nvs_close(h);
}

static void adopt_peer(const uint8_t mac[6], bool persist)
{
    register_encrypted_peer(mac);
    memcpy(s_peer_mac, mac, 6);
    s_has_peer = true;
    if (persist) {
        save_peer_to_nvs(mac);
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (info == NULL || data == NULL || len != (int)sizeof(chmbl_msg_t)) {
        return;
    }
    const chmbl_msg_t *msg = (const chmbl_msg_t *)data;

    if (msg->msg_type == MSG_PAIR) {
        if (s_pairing_mode && memcmp(info->src_addr, s_own_mac, 6) != 0) {
            uint8_t mac_copy[6];
            memcpy(mac_copy, info->src_addr, 6);
            xQueueOverwrite(s_pair_found_q, mac_copy);
        }
        return;
    }

    if (s_data_cb != NULL) {
        s_data_cb(info->src_addr, msg);
    }
}

static void wifi_bring_up(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_CHMBL_NET_CHANNEL, WIFI_SECOND_CHAN_NONE));
    /* Power-save adds tens-of-ms latency to ESP-NOW delivery; the protocol's
     * end-to-end budget (docs/protocol.md §5) can't afford it. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

void pairing_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_bring_up();
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, s_own_mac));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_set_pmk(CHMBL_PMK));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    s_pair_found_q = xQueueCreate(1, 6);

    /* Silently restore a peer paired on a previous boot (protocol.md §1:
     * "subsequent boots are silent and automatic"). */
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint8_t mac[6];
        size_t len = sizeof(mac);
        if (nvs_get_blob(h, NVS_KEY_PEER, mac, &len) == ESP_OK && len == sizeof(mac)) {
            adopt_peer(mac, false);
            ESP_LOGI(TAG, "restored peer %02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        nvs_close(h);
    }
}

void pairing_set_data_cb(pairing_data_cb_t cb)
{
    s_data_cb = cb;
}

bool pairing_has_peer(void)
{
    return s_has_peer;
}

bool pairing_get_peer(uint8_t out[6])
{
    if (!s_has_peer) {
        return false;
    }
    memcpy(out, s_peer_mac, 6);
    return true;
}

bool pairing_start(void)
{
    ensure_broadcast_peer();
    xQueueReset(s_pair_found_q);
    s_pairing_mode = true;

    printf("pairing: broadcasting, waiting up to %ds for a peer "
           "(run 'pair start' on the other board too)...\n",
           CONFIG_CHMBL_PAIR_TIMEOUT_S);

    chmbl_msg_t announce = {
        .version = CHMBL_PROTOCOL_VERSION,
        .msg_type = MSG_PAIR,
    };

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS((uint32_t)CONFIG_CHMBL_PAIR_TIMEOUT_S * 1000);
    TickType_t last_send = 0;
    uint8_t found_mac[6];
    bool paired = false;

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        if ((xTaskGetTickCount() - last_send) >= pdMS_TO_TICKS(200)) {
            esp_now_send(s_broadcast_mac, (const uint8_t *)&announce, sizeof(announce));
            last_send = xTaskGetTickCount();
            printf(".");
            fflush(stdout);
        }
        if (xQueueReceive(s_pair_found_q, found_mac, pdMS_TO_TICKS(50)) == pdTRUE) {
            paired = true;
            break;
        }
    }
    printf("\n");

    if (!paired) {
        s_pairing_mode = false;
        printf("pairing: timed out, no peer found\n");
        return false;
    }

    /* We likely just discovered the other side from its very first
     * announcement — often within milliseconds of it starting, well before
     * our own next scheduled broadcast was due. If we stopped right here,
     * we might never send another packet, and the other side (started
     * slightly after us, still listening for up to its own
     * CHMBL_PAIR_TIMEOUT_S) could time out having never heard from us even
     * though we succeeded. Keep announcing for a short grace period so it
     * gets a fair chance to discover us back. */
    printf("pairing: found a peer, exchanging a few more announcements...\n");
    for (int i = 0; i < PAIR_GRACE_SENDS; i++) {
        esp_now_send(s_broadcast_mac, (const uint8_t *)&announce, sizeof(announce));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    s_pairing_mode = false;

    adopt_peer(found_mac, true);
    printf("pairing: paired with %02x:%02x:%02x:%02x:%02x:%02x\n",
           found_mac[0], found_mac[1], found_mac[2], found_mac[3], found_mac[4], found_mac[5]);
    return true;
}

void pairing_clear(void)
{
    if (s_has_peer) {
        esp_now_del_peer(s_peer_mac);
        s_has_peer = false;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_PEER);
        nvs_commit(h);
        nvs_close(h);
    }
}
