/*
 * `net` command — ESP-NOW heartbeat transmit control/diagnostics
 * (TX-CLI-4/5).
 *
 *     net show          peer, rate, running state, seq + send counters
 *     net rate <hz>     set the heartbeat rate (1-50 Hz)
 *     net send          force one heartbeat now
 *     net start         resume transmitting
 *     net stop          pause transmitting — pair this with `link show` on
 *                        the brake_light to observe its link-loss behaviour
 *                        (DE-03 placeholder)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"

#include "pairing.h"
#include "net.h"
#include "console.h"

static int cmd_net(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "show") == 0) {
        uint8_t peer[6];
        uint16_t seq;
        uint32_t ok, fail;
        net_get_stats(&seq, &ok, &fail);

        if (pairing_get_peer(peer)) {
            printf("peer  : %02x:%02x:%02x:%02x:%02x:%02x\n",
                   peer[0], peer[1], peer[2], peer[3], peer[4], peer[5]);
        } else {
            printf("peer  : unpaired ('pair start' to pair)\n");
        }
        printf("state : %s\n", net_is_running() ? "running" : "stopped");
        printf("rate  : %u Hz\n", net_get_rate());
        printf("seq   : %u\n", seq);
        printf("sent  : %u ok, %u fail\n", ok, fail);
        return 0;
    }

    if (strcmp(argv[1], "rate") == 0) {
        if (argc < 3) {
            printf("usage: net rate <1-50>\n");
            return 1;
        }
        int hz = atoi(argv[2]);
        if (hz < 1 || hz > 50) {
            printf("net: rate must be 1-50 Hz\n");
            return 1;
        }
        net_set_rate((uint8_t)hz);
        printf("net: rate now %d Hz\n", hz);
        return 0;
    }

    if (strcmp(argv[1], "send") == 0) {
        if (!net_send_now()) {
            printf("net: send failed (unpaired or radio error)\n");
            return 1;
        }
        printf("net: heartbeat sent\n");
        return 0;
    }

    if (strcmp(argv[1], "start") == 0) {
        net_start();
        printf("net: transmitting\n");
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0) {
        net_stop();
        printf("net: stopped (heartbeat paused)\n");
        return 0;
    }

    printf("usage: net show|rate <hz>|send|start|stop\n");
    return 1;
}

void cmd_net_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "net",
        .help = "ESP-NOW heartbeat control: net show|rate <hz>|send|start|stop",
        .hint = "show|rate|send|start|stop",
        .func = &cmd_net,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
