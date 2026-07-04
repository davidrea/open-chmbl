/*
 * `link show` — ESP-NOW link health (BL-CLI-5): status, last state/seq,
 * last-rx age vs. the failsafe timeout, and rx/drop counters.
 */
#include <inttypes.h>
#include <stdio.h>

#include "esp_console.h"

#include "link.h"
#include "net.h"
#include "console.h"

static const char *status_name(link_status_t s)
{
    switch (s) {
    case LINK_WAITING: return "WAITING (no packet yet)";
    case LINK_UP:       return "UP";
    case LINK_LOST:     return "LOST";
    default:            return "?";
    }
}

static const char *state_name(brake_state_t s)
{
    switch (s) {
    case ST_OFF:   return "OFF";
    case ST_DECEL: return "DECEL";
    case ST_BRAKE: return "BRAKE";
    default:       return "?";
    }
}

static int cmd_link(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    link_info_t info;
    link_get_info(&info);

    printf("status      : %s\n", status_name(info.status));
    printf("last state  : %s\n", state_name(info.last_state));
    printf("last seq    : %u\n", info.last_seq);
    if (info.last_rx_age_ms < 0) {
        printf("last-rx age : never\n");
    } else {
        printf("last-rx age : %" PRId32 " ms (timeout %u ms)\n",
               info.last_rx_age_ms, info.timeout_ms);
    }
    printf("rx          : %u ok, %u dropped (stale/replay), %u dropped (unpaired sender)\n",
           info.rx_count, info.drop_stale, net_get_drop_unpaired());
    return 0;
}

void cmd_link_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "link",
        .help = "Show ESP-NOW link health: link show",
        .hint = "show",
        .func = &cmd_link,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
