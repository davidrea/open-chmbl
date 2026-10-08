/*
 * `ind show` — view the status indicator (BL-CLI-6, minimal DE-10 slice).
 *
 * Reports the status being indicated and the resulting pattern on D13 (RED) /
 * D14 (GRN). Only link health is aggregated for now; `ind test` and
 * `ind source` from docs/cli.md §4 land with the full DE-10 code engine
 * (fault classes, battery/charge codes, priority resolution, night-dim).
 */
#include <stdio.h>
#include <string.h>

#include "esp_console.h"

#include "console.h"
#include "status.h"

static const char *link_name(link_status_t s)
{
    switch (s) {
    case LINK_WAITING: return "WAITING (no packet yet)";
    case LINK_UP:      return "UP";
    case LINK_LOST:    return "LOST";
    default:           return "?";
    }
}

static void print_gpio(const char *label, int gpio, bool on)
{
    if (gpio < 0) {
        printf("%s : not fitted\n", label);
    } else {
        printf("%s : GPIO%d %s\n", label, gpio, on ? "lit" : "dark");
    }
}

static int cmd_ind(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "show") != 0) {
        printf("usage: ind [show]\n");
        return 1;
    }

    status_info_t info;
    status_get_info(&info);

    printf("source  : link health (DE-03)\n");
    printf("status  : %s\n", link_name(info.link));
    printf("pattern : %s (blink half-period %u ms)\n", info.pattern, info.blink_ms);
    print_gpio("GRN D14", info.gpio_grn, info.grn_on);
    print_gpio("RED D13", info.gpio_red, info.red_on);
    printf("note    : independent of the brake bar — the bar is never blinked\n");
    return 0;
}

void cmd_ind_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ind",
        .help = "Show the status-indicator LEDs: ind show",
        .hint = "show",
        .func = &cmd_ind,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
