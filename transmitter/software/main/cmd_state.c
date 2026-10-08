/*
 * `state` command — the braking state machine's window on the console (DE-09).
 *
 * Mirrors docs/cli.md §3 (TX-CLI-3), plus a tuning verb so the thresholds can
 * be swept on the bench the same way tools/trc_viz.html sweeps them offline:
 *
 *     state [show]                current state, inputs, timers, output
 *     state force off|brake|auto  override / release the emitted state
 *     state tune                  list the DE-09 tunables and their values
 *     state tune <name> <value>   set one (range-checked; takes effect live)
 *
 * The state shown is both what drives the brake-light output (the external
 * indicator pin, see brake_ctl.c) and what net.c broadcasts in every
 * heartbeat. Per the wire protocol (docs/protocol.md §2) the TX FSM emits only
 * OFF or BRAKE — DECEL is reserved and not produced by this device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_console.h"

#include "brake_ctl.h"
#include "console.h"

static const char *force_name(brake_force_t f)
{
    switch (f) {
    case BRAKE_FORCE_OFF:   return "off";
    case BRAKE_FORCE_BRAKE: return "brake";
    default:                return "auto";
    }
}

static int state_do_show(void)
{
    brake_ctl_status_t st;
    brake_ctl_get(&st);

    printf("state:    %s -> %s (light %s)\n",
           brake_fsm_state_name(st.fsm_state),
           st.emitted == ST_BRAKE ? "BRAKE" : "OFF",
           st.light_on ? "ON" : "off");
    printf("override: %s\n", force_name(st.force));
    printf("speed:    %.2f mph (%s)\n", (double)st.speed_mph,
           st.speed_valid ? "valid" : "UNAVAILABLE — light held off");
    printf("accel:    %.2f mph/s (%s)\n", (double)st.accel_mphps,
           st.accel_valid ? "valid" : "not yet derived");
    printf("clutch:   %s   gear: %s\n",
           st.clutch_pulled ? "pulled" : "released",
           st.gear_valid ? (st.gear == 0 ? "N" : "in gear") : "unavailable");
    if (st.gear_valid && st.gear != 0) {
        printf("          gear %u\n", st.gear);
    }
    printf("timers:   decel %lu ms, steady %lu ms, stopped %lu ms\n",
           (unsigned long)st.decel_hold_ms, (unsigned long)st.steady_hold_ms,
           (unsigned long)st.stopped_hold_ms);
    if (st.since_ms == UINT32_MAX) {
        printf("          since last transition: rearmed (no valid speed yet)\n");
    } else {
        printf("          since last transition: %lu ms\n",
               (unsigned long)st.since_ms);
    }
    printf("changes:  %lu transitions since boot\n",
           (unsigned long)st.transitions);
    return 0;
}

static int state_do_tune_list(void)
{
    brake_tunables_t t;
    brake_ctl_get_tunables(&t);

    printf("%-24s %10s %18s\n", "tunable", "value", "range");
    for (int i = 0; i < brake_tune_count(); i++) {
        const char *name = brake_tune_name(i);
        float value, lo, hi;
        brake_tune_get(&t, name, &value);
        brake_tune_range(name, &lo, &hi);
        printf("%-24s %10.2f %8.2f .. %-8.2f\n", name, (double)value,
               (double)lo, (double)hi);
    }
    return 0;
}

static int cmd_state(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "show") == 0) {
        return state_do_show();
    }

    if (strcasecmp(argv[1], "force") == 0 && argc == 3) {
        if (strcasecmp(argv[2], "off") == 0) {
            brake_ctl_force(BRAKE_FORCE_OFF);
        } else if (strcasecmp(argv[2], "brake") == 0) {
            brake_ctl_force(BRAKE_FORCE_BRAKE);
        } else if (strcasecmp(argv[2], "auto") == 0) {
            brake_ctl_force(BRAKE_FORCE_AUTO);
        } else {
            printf("usage: state force off|brake|auto  (DECEL is reserved, "
                   "not TX-emitted)\n");
            return 1;
        }
        printf("state: override = %s\n", force_name(brake_ctl_get_force()));
        return 0;
    }

    if (strcasecmp(argv[1], "tune") == 0) {
        if (argc == 2) {
            return state_do_tune_list();
        }
        if (argc == 4) {
            char *end;
            const float value = strtof(argv[3], &end);
            if (end == argv[3] || *end != '\0') {
                printf("state: bad value '%s'\n", argv[3]);
                return 1;
            }
            if (!brake_ctl_tune_set(argv[2], value)) {
                float lo, hi;
                if (brake_tune_range(argv[2], &lo, &hi)) {
                    printf("state: %s must be within %.2f .. %.2f\n", argv[2],
                           (double)lo, (double)hi);
                } else {
                    printf("state: unknown tunable '%s' (try `state tune`)\n",
                           argv[2]);
                }
                return 1;
            }
            brake_tunables_t t;
            float applied;
            brake_ctl_get_tunables(&t);
            brake_tune_get(&t, argv[2], &applied);
            printf("state: %s = %.2f\n", argv[2], (double)applied);
            return 0;
        }
    }

    printf("usage: state [show] | state force off|brake|auto | "
           "state tune [<name> <value>]\n");
    return 1;
}

void cmd_state_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "state",
        .help = "Braking state machine: state [show] | force off|brake|auto | "
                "tune [<name> <value>]",
        .hint = "[show|force|tune]",
        .func = &cmd_state,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
