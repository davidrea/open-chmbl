/*
 * `sig` command — decoded-signal inspection and bench faking (DE-08).
 *
 * Mirrors docs/cli.md §3 (TX-CLI-1/2):
 *
 *     sig show                          all signals, validity, source
 *     sig set <name> <value|na>         fake a signal (na = unavailable)
 *     sig ramp wheel <mph/s> [until <mph>]  ramp faked wheel speed so the
 *                                       derived accel exercises thresholds
 *     sig source can|fake               switch the active signal source
 *
 * Short aliases accepted by `sig set`/`sig ramp`: wheel (wheel_speed),
 * clutch (clutch_pulled), throttle (throttle_pct). `sig set gear N` sets
 * neutral (gear 0). Faking only affects the decoded-signal view — nothing
 * is ever transmitted on the CAN bus.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_console.h"
#include "esp_timer.h"

#include "can_rx.h"
#include "console.h"

static const char *alias(const char *name)
{
    if (strcasecmp(name, "wheel") == 0) return "wheel_speed";
    if (strcasecmp(name, "clutch") == 0) return "clutch_pulled";
    if (strcasecmp(name, "throttle") == 0) return "throttle_pct";
    return name;
}

static const char *sig_unit(const char *name)
{
    if (strcmp(name, "wheel_speed") == 0 ||
        strcmp(name, "wheel_speed_rear") == 0) return "mph";
    if (strcmp(name, "accel") == 0) return "mph/s";
    if (strcmp(name, "throttle_pct") == 0) return "%";
    if (strncmp(name, "rpm", 3) == 0) return "rpm";
    return "";
}

/* ---- sig ramp ----------------------------------------------------------- */

#define RAMP_PERIOD_MS 50

static esp_timer_handle_t s_ramp_timer;
static float s_ramp_rate;   /* mph/s (signed) */
static float s_ramp_value;  /* current faked wheel speed */
static float s_ramp_until;  /* target */

static void ramp_stop(void)
{
    if (s_ramp_timer) {
        esp_timer_stop(s_ramp_timer);
    }
}

static void ramp_tick(void *arg)
{
    (void)arg;
    s_ramp_value += s_ramp_rate * ((float)RAMP_PERIOD_MS / 1000.0f);
    bool done = (s_ramp_rate < 0.0f) ? (s_ramp_value <= s_ramp_until)
                                     : (s_ramp_value >= s_ramp_until);
    if (done) {
        s_ramp_value = s_ramp_until;
        ramp_stop();
    }
    sig_fake_set("wheel_speed", s_ramp_value);
}

static int ramp_start(float rate, float until)
{
    if (!s_ramp_timer) {
        const esp_timer_create_args_t args = {
            .callback = ramp_tick,
            .name = "sig_ramp",
        };
        if (esp_timer_create(&args, &s_ramp_timer) != ESP_OK) {
            printf("sig: ramp timer create failed\n");
            return 1;
        }
    }
    ramp_stop();

    can_signals_t sigs;
    sig_snapshot(&sigs, NULL);
    s_ramp_value = sigs.wheel_speed.seen ? sigs.wheel_speed.value : 0.0f;
    s_ramp_rate = rate;
    s_ramp_until = until;

    if ((rate < 0.0f && until >= s_ramp_value) ||
        (rate > 0.0f && until <= s_ramp_value) || rate == 0.0f) {
        printf("sig: ramp from %.1f at %.1f mph/s never reaches %.1f\n",
               (double)s_ramp_value, (double)rate, (double)until);
        return 1;
    }

    sig_set_source(SIG_SOURCE_FAKE);
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_ramp_timer,
                                             RAMP_PERIOD_MS * 1000));
    printf("sig: ramping wheel %.1f -> %.1f mph at %.1f mph/s (source=fake)\n",
           (double)s_ramp_value, (double)until, (double)rate);
    return 0;
}

/* ---- sig show / set / source -------------------------------------------- */

static int sig_do_show(void)
{
    can_signals_t sigs;
    uint32_t now;
    sig_snapshot(&sigs, &now);

    printf("source: %s\n",
           sig_get_source() == SIG_SOURCE_FAKE ? "fake" : "can");
    printf("%-18s %10s %6s %6s\n", "signal", "value", "unit", "valid");
    for (int i = 0; sig_names[i]; i++) {
        const sig_value_t *v = sig_by_index(&sigs, i);
        printf("%-18s %10.2f %6s %6s\n", sig_names[i], (double)v->value,
               sig_unit(sig_names[i]),
               can_sig_valid(v, now) ? "yes" : (v->seen ? "STALE" : "no"));
    }
    return 0;
}

static int cmd_sig(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "show") == 0) {
        return sig_do_show();
    }

    if (strcasecmp(argv[1], "set") == 0 && argc == 4) {
        const char *name = alias(argv[2]);
        if (strcasecmp(argv[3], "na") == 0) {
            if (!sig_fake_clear(name)) {
                printf("sig: unknown signal '%s'\n", argv[2]);
                return 1;
            }
            printf("sig: %s marked unavailable (fake)\n", name);
            return 0;
        }
        float value;
        if (strcmp(name, "gear") == 0 && strcasecmp(argv[3], "N") == 0) {
            value = 0.0f;
        } else {
            char *end;
            value = strtof(argv[3], &end);
            if (end == argv[3] || *end != '\0') {
                printf("sig: bad value '%s'\n", argv[3]);
                return 1;
            }
        }
        if (!sig_fake_set(name, value)) {
            printf("sig: unknown signal '%s'\n", argv[2]);
            return 1;
        }
        if (sig_get_source() != SIG_SOURCE_FAKE) {
            printf("sig: note — source is 'can'; `sig source fake` to use "
                   "faked values\n");
        }
        printf("sig: %s = %.2f (fake)\n", name, (double)value);
        return 0;
    }

    if (strcasecmp(argv[1], "ramp") == 0 && argc >= 4 &&
        strcmp(alias(argv[2]), "wheel_speed") == 0) {
        char *end;
        float rate = strtof(argv[3], &end);
        if (end == argv[3] || *end != '\0') {
            printf("sig: bad rate '%s'\n", argv[3]);
            return 1;
        }
        float until = (rate < 0.0f) ? 0.0f : 100.0f;
        if (argc == 6 && strcasecmp(argv[4], "until") == 0) {
            until = strtof(argv[5], NULL);
        } else if (argc != 4) {
            printf("usage: sig ramp wheel <mph/s> [until <mph>]\n");
            return 1;
        }
        return ramp_start(rate, until);
    }

    if (strcasecmp(argv[1], "source") == 0 && argc == 3) {
        if (strcasecmp(argv[2], "can") == 0) {
            ramp_stop();
            sig_set_source(SIG_SOURCE_CAN);
        } else if (strcasecmp(argv[2], "fake") == 0) {
            sig_set_source(SIG_SOURCE_FAKE);
        } else {
            printf("usage: sig source can|fake\n");
            return 1;
        }
        printf("sig: source = %s\n", argv[2]);
        return 0;
    }

    printf("usage: sig [show] | sig set <name> <value|na> | "
           "sig ramp wheel <mph/s> [until <mph>] | sig source can|fake\n");
    return 1;
}

void cmd_sig_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "sig",
        .help = "Show/fake decoded CAN signals: sig show | set | ramp | source",
        .hint = "[show|set|ramp|source]",
        .func = &cmd_sig,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
