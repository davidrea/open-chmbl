/*
 * `id` command — report the chip's unique ID and basic identity.
 *
 * The factory base MAC (read from eFuse) is the chip's unique 48-bit ID; the
 * per-interface MACs (Wi-Fi STA, BT, ...) are derived from it. We also print
 * the chip model/revision/cores and the ESP-IDF version the image was built
 * against, which is handy when bringing up a new board over the console.
 */
#include <stdio.h>
#include <inttypes.h>

#include "esp_console.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_mac.h"

#include "console.h"

static const char *chip_model_str(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C2: return "ESP32-C2";
    case CHIP_ESP32C6: return "ESP32-C6";
    case CHIP_ESP32H2: return "ESP32-H2";
    default:           return "unknown";
    }
}

static int cmd_id(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_BASE);
    if (err != ESP_OK) {
        printf("id: failed to read base MAC (%s)\n", esp_err_to_name(err));
        return 1;
    }

    /* The 6-byte base MAC is the chip's unique ID; also fold it into a single
     * 64-bit value for convenience. */
    uint64_t uid = 0;
    for (int i = 0; i < 6; i++) {
        uid = (uid << 8) | mac[i];
    }

    esp_chip_info_t info;
    esp_chip_info(&info);

    printf("chip       : %s rev %d, %d core(s)\n",
           chip_model_str(info.model), info.revision, info.cores);
    printf("features   :%s%s%s\n",
           (info.features & CHIP_FEATURE_WIFI_BGN) ? " WiFi" : "",
           (info.features & CHIP_FEATURE_BLE)      ? " BLE"  : "",
           (info.features & CHIP_FEATURE_BT)       ? " BT"   : "");
    printf("base MAC   : %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("unique ID  : 0x%012" PRIx64 "\n", uid);
    printf("idf version: %s\n", esp_get_idf_version());
    return 0;
}

void cmd_system_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "id",
        .help = "Show chip unique ID (base MAC), model/revision, and IDF version",
        .hint = NULL,
        .func = &cmd_id,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
