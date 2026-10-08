/*
 * `light` command — bench override of the brake bar's binary output.
 *
 *     light            show the override state and what the bar is doing
 *     light on         force the bar on  (both driver EN pins high)
 *     light off        force the bar off (both driver EN pins low)
 *     light toggle     invert the forced output
 *     light auto       release the override, back to the link-driven state
 *
 * This is how the bar gets exercised on the bench with no transmitter
 * present. It does not poke the GPIOs itself: the render stage (render.c) is
 * the single writer of the enable pins, so the override still goes through
 * the anti-strobe dwell floor and cannot fight the link watchdog for the pin
 * (which is what the old direct-GPIO version did — link.c overwrote it on
 * its next tick). `render show` views the result; `light auto` hands the bar
 * back to the link.
 *
 * There is no brightness argument: the bar is binary (DE-04), and the LED
 * current is fixed in hardware by the driver sense resistor.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "sdkconfig.h"

#include "console.h"
#include "render.h"

static void print_state(void)
{
    render_info_t info;
    render_get_info(&info);

    if (info.overridden) {
        printf("light: override %s", info.override_out == BAR_ON ? "ON" : "OFF");
    } else {
        printf("light: auto (link-driven)");
    }
    printf(", bar is %s", info.out == BAR_ON ? "ON" : "OFF");
    if (info.hold_ms) {
        printf(" (anti-strobe hold %" PRIu32 " ms)", info.hold_ms);
    }
    printf("\n");
}

static int cmd_light(int argc, char **argv)
{
    if (argc < 2) {
        print_state();
        return 0;
    }

    render_info_t info;
    render_get_info(&info);

    const char *action = argv[1];
    if (strcmp(action, "on") == 0) {
        render_override(BAR_ON);
    } else if (strcmp(action, "off") == 0) {
        render_override(BAR_OFF);
    } else if (strcmp(action, "toggle") == 0) {
        /* Toggle what the bar is actually showing, so `light toggle` works
         * as the first command too (from auto mode). */
        render_override(info.out == BAR_ON ? BAR_OFF : BAR_ON);
    } else if (strcmp(action, "auto") == 0) {
        render_override_clear();
    } else {
        printf("usage: light [on|off|toggle|auto]\n");
        return 1;
    }

    /* The render task is the only writer of the pins, so give it a couple of
     * ticks to act before reporting what the bar is actually doing. */
    vTaskDelay(pdMS_TO_TICKS(2 * CONFIG_CHMBL_RENDER_TICK_MS));
    print_state();
    return 0;
}

void cmd_light_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "light",
        .help = "Bench override of the brake bar: light [on|off|toggle|auto]",
        .hint = "[on|off|toggle|auto]",
        .func = &cmd_light,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
