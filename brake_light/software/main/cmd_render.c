/*
 * `render show` — view the brake bar's render output (BL-CLI-4).
 *
 * Shows the effective braking state, the binary output now on the driver
 * enable pins, the anti-strobe floors and any hold in progress, and whether
 * the `light` bench override is engaged.
 *
 * There is deliberately no brightness column: brightness/auto-dimming is
 * deferred (docs/design/de-04-led-render.md, de-02-auto-brightness.md), the
 * bar is a binary GPIO, and the LED current is fixed in hardware by the
 * driver sense resistor. `ambient *` and `bright cap` from docs/cli.md §4
 * are deferred with it.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_console.h"

#include "console.h"
#include "render.h"

static const char *state_name(brake_state_t s)
{
    switch (s) {
    case ST_OFF:   return "OFF";
    case ST_DECEL: return "DECEL (reserved)";
    case ST_BRAKE: return "BRAKE";
    default:       return "?";
    }
}

static void print_gpio(const char *label, int gpio)
{
    if (gpio < 0) {
        printf("%s : not fitted\n", label);
    } else {
        printf("%s : GPIO%d\n", label, gpio);
    }
}

static int cmd_render(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "show") != 0) {
        printf("usage: render [show]\n");
        return 1;
    }

    render_info_t info;
    render_get_info(&info);

    printf("state       : %s%s\n", state_name(info.state),
           info.overridden ? " (ignored — override engaged)" : "");
    printf("output      : %s  (binary; no brightness — DE-04 defers dimming)\n",
           info.out == BAR_ON ? "ON (pins high)" : "OFF (pins low)");
    printf("override    : %s\n",
           info.overridden ? (info.override_out == BAR_ON ? "ON (light on)"
                                                          : "OFF (light off)")
                           : "none (light auto)");
    print_gpio("bar EN A   ", info.gpio_a);
    print_gpio("bar EN B   ", info.gpio_b);
    printf("anti-strobe : min-on %u ms, min-off %u ms, tick %u ms\n",
           info.min_on_ms, info.min_off_ms, info.tick_ms);
    if (info.hold_ms) {
        printf("hold        : %" PRIu32 " ms until the pending change commits\n",
               info.hold_ms);
    } else {
        printf("hold        : none\n");
    }
    printf("transitions : %" PRIu32 " committed, %" PRIu32 " ticks held off\n",
           info.changes, info.held);
    return 0;
}

void cmd_render_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "render",
        .help = "Show the brake bar's render output: render show",
        .hint = "show",
        .func = &cmd_render,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
