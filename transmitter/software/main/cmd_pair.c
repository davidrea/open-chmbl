/*
 * `pair` command — manage the ESP-NOW peer.
 *
 *     pair start     broadcast + listen for a peer (run on both boards
 *                    within the same window); on success, registers an
 *                    encrypted peer and persists it to NVS
 *     pair status    show the current peer, if any
 *     pair clear     forget the paired peer
 */
#include <stdio.h>
#include <string.h>

#include "esp_console.h"

#include "pairing.h"
#include "console.h"

static int cmd_pair(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: pair start|status|clear\n");
        return 1;
    }

    if (strcmp(argv[1], "start") == 0) {
        return pairing_start() ? 0 : 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        uint8_t mac[6];
        if (pairing_get_peer(mac)) {
            printf("paired with %02x:%02x:%02x:%02x:%02x:%02x\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            printf("not paired\n");
        }
        return 0;
    }

    if (strcmp(argv[1], "clear") == 0) {
        pairing_clear();
        printf("pairing cleared\n");
        return 0;
    }

    printf("usage: pair start|status|clear\n");
    return 1;
}

void cmd_pair_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "pair",
        .help = "Manage the ESP-NOW peer: pair start|status|clear",
        .hint = "start|status|clear",
        .func = &cmd_pair,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
