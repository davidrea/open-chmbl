/* DE-09 — 50 Hz brake-state tick, output drive, force override, tunables. */

#include "brake_ctl.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "can_rx.h"
#include "status_led.h"

static const char *TAG = "brake";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static brake_fsm_t         s_fsm;
static brake_ctl_status_t  s_status;
static brake_force_t       s_force = BRAKE_FORCE_AUTO;

/* Map a decoded snapshot onto one tick of FSM input.
 *
 * Validity matters more than the value here. An absent or stale clutch/gear
 * makes gear 0, which simply disables the launch guard (DE-09 rule 6b) and
 * leaves STOPPED to exit on speed or the stop timeout — the documented
 * degradation for a bike whose bus doesn't publish them. Wheel speed is the
 * exception: the FSM holds the light off without it, because a light that
 * latches on when the bus goes quiet is worse than no light at all. */
static void snapshot_to_input(const can_signals_t *s, uint32_t now,
                              brake_fsm_in_t *in)
{
    const bool gear_valid = can_sig_valid(&s->gear, now);

    in->speed_mph     = s->wheel_speed.value;
    in->speed_valid   = can_sig_valid(&s->wheel_speed, now);
    in->accel_mphps   = s->accel.value;
    in->accel_valid   = can_sig_valid(&s->accel, now);
    in->clutch_pulled = can_sig_valid(&s->clutch_pulled, now) &&
                        s->clutch_pulled.value != 0.0f;
    in->gear = gear_valid ? (uint8_t)(s->gear.value + 0.5f) : 0u;
}

static void brake_tick_task(void *arg)
{
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();
    brake_fsm_state_t prev = BRAKE_FSM_OFF;

    for (;;) {
        can_signals_t sigs;
        uint32_t now;
        sig_snapshot(&sigs, &now);

        brake_fsm_in_t in;
        snapshot_to_input(&sigs, now, &in);

        taskENTER_CRITICAL(&s_lock);
        const brake_fsm_state_t state =
            brake_fsm_step(&s_fsm, &in, BRAKE_FSM_TICK_MS);
        const brake_force_t force = s_force;

        const bool light = (force == BRAKE_FORCE_AUTO)
                               ? brake_fsm_light_on(state)
                               : (force == BRAKE_FORCE_BRAKE);

        s_status.fsm_state       = state;
        s_status.force           = force;
        s_status.light_on        = light;
        s_status.emitted         = light ? ST_BRAKE : ST_OFF;
        s_status.speed_mph       = in.speed_mph;
        s_status.speed_valid     = in.speed_valid;
        s_status.accel_mphps     = in.accel_mphps;
        s_status.accel_valid     = in.accel_valid;
        s_status.clutch_pulled   = in.clutch_pulled;
        s_status.gear            = in.gear;
        s_status.gear_valid      = can_sig_valid(&sigs.gear, now);
        s_status.since_ms        = s_fsm.since_ms;
        s_status.decel_hold_ms   = s_fsm.decel_hold_ms;
        s_status.steady_hold_ms  = s_fsm.steady_hold_ms;
        s_status.stopped_hold_ms = s_fsm.stopped_hold_ms;
        s_status.transitions     = s_fsm.transitions;
        taskEXIT_CRITICAL(&s_lock);

        status_led_brake(light);

        if (state != prev) {
            ESP_LOGI(TAG, "%s -> %s (speed %.1f mph, accel %.2f mph/s)",
                     brake_fsm_state_name(prev), brake_fsm_state_name(state),
                     (double)in.speed_mph,
                     in.accel_valid ? (double)in.accel_mphps : 0.0);
            prev = state;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BRAKE_FSM_TICK_MS));
    }
}

void brake_ctl_init(void)
{
    const brake_tunables_t tun = BRAKE_TUNABLES_DEFAULT;
    brake_fsm_init(&s_fsm, &tun);
    can_rx_set_speed_smoothing(tun.speed_smooth_ms);

    s_status.emitted = ST_OFF;
    status_led_brake(false);

    xTaskCreate(brake_tick_task, "brake_ctl", 3072, NULL, 7, NULL);
    ESP_LOGI(TAG, "state machine ticking at %u Hz, light on GPIO%d",
             1000u / BRAKE_FSM_TICK_MS, CONFIG_CHMBL_BRAKE_LED_GPIO);
}

void brake_ctl_get(brake_ctl_status_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_status;
    taskEXIT_CRITICAL(&s_lock);
}

brake_state_t state_get(void)
{
    taskENTER_CRITICAL(&s_lock);
    const brake_state_t s = s_status.emitted;
    taskEXIT_CRITICAL(&s_lock);
    return s;
}

void brake_ctl_force(brake_force_t force)
{
    taskENTER_CRITICAL(&s_lock);
    s_force = force;
    taskEXIT_CRITICAL(&s_lock);
}

brake_force_t brake_ctl_get_force(void)
{
    return s_force;
}

void brake_ctl_get_tunables(brake_tunables_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_fsm.tun;
    taskEXIT_CRITICAL(&s_lock);
}

bool brake_ctl_tune_set(const char *name, float value)
{
    taskENTER_CRITICAL(&s_lock);
    const bool ok = brake_tune_set(&s_fsm.tun, name, value);
    const uint16_t tau = s_fsm.tun.speed_smooth_ms;
    taskEXIT_CRITICAL(&s_lock);

    /* speed_smooth_ms is a DE-09 tunable applied inside the decoder, so push
     * it across rather than leaving the two copies to drift. */
    if (ok) {
        can_rx_set_speed_smoothing(tau);
    }
    return ok;
}
