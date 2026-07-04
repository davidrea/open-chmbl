/*
 * `light` command — drive the stand-in brake-light output.
 *
 * Until the real LED pattern/render engine (DE-04) lands, the brake light is a
 * single GPIO so the console plumbing can be exercised on the bench:
 *
 *     light            show current state
 *     light on         drive the pin high
 *     light off        drive the pin low
 *     light toggle     invert the pin
 *
 * light_set() is also the brake light's single physical output: the link
 * watchdog (link.c) drives it from the received state (or blinks it as a
 * link-loss placeholder), same as this CLI command does for bench testing.
 */
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_console.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "console.h"

#define LIGHT_GPIO ((gpio_num_t)CONFIG_CHMBL_LIGHT_GPIO)

static const char *TAG = "cmd_light";
static bool s_light_on;

void light_set(bool on)
{
    s_light_on = on;
    gpio_set_level(LIGHT_GPIO, on ? 1 : 0);
}

/* GPIO bring-up, split out from cmd_light_register() so the physical output
 * exists regardless of whether the dev CLI (CONFIG_CHMBL_CLI) is built in —
 * the link watchdog (link.c) drives it unconditionally, same as the real
 * render engine will. */
void light_init(void)
{
    gpio_reset_pin(LIGHT_GPIO);
    gpio_set_direction(LIGHT_GPIO, GPIO_MODE_OUTPUT);
    light_set(false);
    ESP_LOGI(TAG, "stand-in brake light on GPIO%d", LIGHT_GPIO);
}

static int cmd_light(int argc, char **argv)
{
    if (argc < 2) {
        printf("light: GPIO%d is %s\n", LIGHT_GPIO, s_light_on ? "ON" : "OFF");
        return 0;
    }

    const char *action = argv[1];
    if (strcmp(action, "on") == 0) {
        light_set(true);
    } else if (strcmp(action, "off") == 0) {
        light_set(false);
    } else if (strcmp(action, "toggle") == 0) {
        light_set(!s_light_on);
    } else {
        printf("usage: light [on|off|toggle]\n");
        return 1;
    }

    printf("light: GPIO%d is now %s\n", LIGHT_GPIO, s_light_on ? "ON" : "OFF");
    return 0;
}

void cmd_light_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "light",
        .help = "Drive the stand-in brake-light GPIO: light [on|off|toggle]",
        .hint = "[on|off|toggle]",
        .func = &cmd_light,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
