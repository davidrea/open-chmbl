/*
 * `state` command — set / show the stand-in braking output state.
 *
 * The transmitter's real output is a braking state broadcast over ESP-NOW.
 * Per the wire protocol (docs/protocol.md §2) the current TX FSM emits only
 * OFF or BRAKE — DECEL is reserved and not produced by this device. Until the
 * CAN decode (DE-08), state machine (DE-09), and ESP-NOW link (DE-01) land,
 * this command lets the bench drive and observe that emitted state directly,
 * and lights a stand-in indicator GPIO whenever the state is BRAKE. Mirrors
 * `state force OFF|BRAKE` (TX-CLI-3) in docs/cli.md.
 *
 *     state              show current state
 *     state off          force OFF
 *     state brake        force BRAKE
 */
#include <stdio.h>
#include <strings.h>

#include "driver/gpio.h"
#include "esp_console.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "console.h"

#define STATE_GPIO ((gpio_num_t)CONFIG_CHMBL_STATE_GPIO)

static const char *TAG = "cmd_state";

/* Mirrors the wire brake_state_t (docs/protocol.md §2). DECEL (=1) is reserved
 * and not emitted by the current TX FSM, so the standin only forces OFF/BRAKE. */
typedef enum { ST_OFF = 0, ST_DECEL = 1, ST_BRAKE = 2 } brake_state_t;
static brake_state_t s_state = ST_OFF;

static const char *state_name(brake_state_t s)
{
    switch (s) {
    case ST_OFF:   return "OFF";
    case ST_DECEL: return "DECEL";
    case ST_BRAKE: return "BRAKE";
    default:       return "?";
    }
}

static void state_set(brake_state_t s)
{
    s_state = s;
    /* Indicator lit whenever we'd be signalling the helmet (BRAKE). */
    gpio_set_level(STATE_GPIO, s == ST_OFF ? 0 : 1);
}

static int cmd_state(int argc, char **argv)
{
    if (argc < 2) {
        printf("state: %s (indicator GPIO%d %s)\n",
               state_name(s_state), STATE_GPIO, s_state == ST_OFF ? "off" : "on");
        return 0;
    }

    if (strcasecmp(argv[1], "off") == 0) {
        state_set(ST_OFF);
    } else if (strcasecmp(argv[1], "brake") == 0) {
        state_set(ST_BRAKE);
    } else {
        printf("usage: state [off|brake]  (DECEL is reserved, not TX-emitted)\n");
        return 1;
    }

    printf("state: now %s\n", state_name(s_state));
    return 0;
}

void cmd_state_register(void)
{
    gpio_reset_pin(STATE_GPIO);
    gpio_set_direction(STATE_GPIO, GPIO_MODE_OUTPUT);
    state_set(ST_OFF);
    ESP_LOGI(TAG, "stand-in state indicator on GPIO%d", STATE_GPIO);

    const esp_console_cmd_t cmd = {
        .command = "state",
        .help = "Set/show the stand-in braking output state: state [off|brake]",
        .hint = "[off|brake]",
        .func = &cmd_state,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
