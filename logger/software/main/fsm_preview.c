#include "fsm_preview.h"

#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "brake_fsm.h"
#include "can_tap.h"
#include "logger_time.h"
#include "ui_log.h"

#define LED_GPIO CONFIG_LOGGER_LED_EXT_GPIO

/* The FSM is ticked on a fixed grid and handed the NOMINAL period as dt, which
 * is what the reference implementation does and what the hold timers were tuned
 * against. xTaskDelayUntil keeps the grid from drifting; a measured dt would
 * only inject scheduler jitter into the debounce windows. */
#define TICK_MS ((uint32_t)BRAKE_FSM_TICK_MS)

static brake_fsm_t          s_fsm;
static brake_fsm_tunables_t s_tun;
static volatile bool        s_light_on;

/* Latches so the "signals went away" / "signals came back" notices appear once
 * per event instead of 50 times a second. */
static bool s_had_signals;

static void led_write(bool on)
{
    /* Active-high: IO18 drives Q1's (2N7002K) gate through R3, and Q1 low-side
     * switches the LED on J4 pin 2. High = Q1 conducts = lit. */
    gpio_set_level(LED_GPIO, on ? 1 : 0);
    s_light_on = on;
}

/* Pull the Kconfig integers into the float tunables, ONCE. Kconfig has no float
 * type, so the menu stores milli-units; see components/brake_fsm/Kconfig. */
static void load_tunables(void)
{
    s_tun.decel_on_mphps          = CONFIG_BRAKE_FSM_DECEL_ON_MMPHPS / 1000.0f;
    s_tun.decel_on_debounce_ms    = (float)CONFIG_BRAKE_FSM_DECEL_ON_DEBOUNCE_MS;
    s_tun.stop_speed_mph          = CONFIG_BRAKE_FSM_STOP_SPEED_MMPH / 1000.0f;
    s_tun.moving_speed_mph        = CONFIG_BRAKE_FSM_MOVING_SPEED_MMPH / 1000.0f;
    s_tun.accel_off_mphps         = CONFIG_BRAKE_FSM_ACCEL_OFF_MMPHPS / 1000.0f;
    s_tun.accel_off_min_speed_mph =
        CONFIG_BRAKE_FSM_ACCEL_OFF_MIN_SPEED_MMPH / 1000.0f;
    s_tun.steady_band_mphps       = CONFIG_BRAKE_FSM_STEADY_BAND_MMPHPS / 1000.0f;
    s_tun.steady_timeout_ms       = (float)CONFIG_BRAKE_FSM_STEADY_TIMEOUT_MS;
    s_tun.stop_timeout_ms         = (float)CONFIG_BRAKE_FSM_STOP_TIMEOUT_MS;
    s_tun.state_min_dwell_ms      = (float)CONFIG_BRAKE_FSM_STATE_MIN_DWELL_MS;

    ui_log_line("DE-09 FSM tunables:");
    ui_log_line("  decel_on %.2f mph/s, debounce %.0f ms, dwell %.0f ms",
                s_tun.decel_on_mphps, s_tun.decel_on_debounce_ms,
                s_tun.state_min_dwell_ms);
    ui_log_line("  stop %.2f mph, moving %.2f mph, stop_timeout %.0f ms",
                s_tun.stop_speed_mph, s_tun.moving_speed_mph,
                s_tun.stop_timeout_ms);
    ui_log_line("  accel_off %.2f mph/s above %.2f mph",
                s_tun.accel_off_mphps, s_tun.accel_off_min_speed_mph);
    ui_log_line("  steady band %.2f mph/s for %.0f ms",
                s_tun.steady_band_mphps, s_tun.steady_timeout_ms);
}

static void fsm_preview_task(void *arg)
{
    (void)arg;
    TickType_t next = xTaskGetTickCount();

    for (;;) {
        xTaskDelayUntil(&next, pdMS_TO_TICKS(TICK_MS));

        const uint32_t now_ms = logger_now_ms();
        can_tap_snap_t s;
        can_tap_snapshot(&s, now_ms);

        /* VALIDITY IS THE CALLER'S JOB (brake_fsm.h). Without a trustworthy
         * wheel speed there is no braking decision to make, so hold the light
         * off and reset the machine rather than letting it run on a stale or
         * never-seen value — a decoder that has seen nothing reads 0.00 mph,
         * which rule 2 would happily turn into a lit STOPPED at boot.
         *
         * Note the host replay harness deliberately does NOT apply this gate,
         * so that it reproduces the Python reference exactly; this is the one
         * place the firmware is more conservative than the reference. */
        if (!s.speed_valid) {
            if (s_had_signals) {
                ui_log_line("FSM: wheel speed invalid (stale/absent) — "
                            "light forced OFF, machine reset");
                s_had_signals = false;
            }
            brake_fsm_init(&s_fsm, &s_tun);
            led_write(false);
            continue;
        }
        if (!s_had_signals) {
            ui_log_line("FSM: wheel speed live (%.2f mph) — machine running",
                        s.speed_mph);
            s_had_signals = true;
        }

        const brake_fsm_input_t in = {
            .speed_mph     = s.speed_mph,
            .accel_mphps   = s.accel_mphps,
            .accel_valid   = s.accel_valid,
            .clutch_pulled = s.clutch_pulled,
            .in_gear       = s.in_gear,
        };

        const brake_fsm_state_t prev = s_fsm.state;
        if (brake_fsm_step(&s_fsm, &in, BRAKE_FSM_TICK_MS)) {
            /* The record that explains the LED after the ride. Everything a
             * reviewer needs to re-derive the decision from the .trc: when,
             * which rule fired, and the decisive signal values at that tick. */
            ui_log_line("FSM %u.%03us %s -> %s (rule %s) light=%s",
                        (unsigned)(now_ms / 1000u), (unsigned)(now_ms % 1000u),
                        brake_fsm_state_name(prev),
                        brake_fsm_state_name(s_fsm.state),
                        brake_fsm_rule_name(s_fsm.last_rule),
                        brake_fsm_light_on(&s_fsm) ? "ON" : "off");
            /* Gear prints as "N" in neutral rather than "0": the rule-6b guard
             * turns on exactly that distinction, so the log should not make the
             * reader remember which number neutral is. */
            char gear_str[8];
            if (s.in_gear) {
                snprintf(gear_str, sizeof(gear_str), "%d", (int)s.gear);
            } else {
                snprintf(gear_str, sizeof(gear_str), "N");
            }
            ui_log_line("    speed %.2f mph  accel %s%.2f mph/s  "
                        "clutch %s  gear %s",
                        s.speed_mph,
                        in.accel_valid ? "" : "(invalid) ",
                        in.accel_valid ? s.accel_mphps : 0.0f,
                        s.clutch_pulled ? "IN" : "out",
                        gear_str);
        }

        led_write(brake_fsm_light_on(&s_fsm));
    }
}

void fsm_preview_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        /* R18 already holds Q1's gate down; no internal pull wanted. */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    led_write(false);

    load_tunables();
    brake_fsm_init(&s_fsm, &s_tun);
    s_had_signals = false;

    ui_log_line("remote brake-preview LED on IO%d (active-high via Q1, J4 pin2)",
                LED_GPIO);
    ui_log_line("  ON = this firmware would command ST_BRAKE; no ESP-NOW in this build");

    xTaskCreate(fsm_preview_task, "fsm_preview", 3072, NULL, 4, NULL);
}

bool fsm_preview_light_on(void)
{
    return s_light_on;
}
