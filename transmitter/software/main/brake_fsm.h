/*
 * DE-09 — braking state machine (pure, host-testable core).
 *
 * Decides the brake light from wheel-speed-derived acceleration: the
 * reference bus carries no brake-switch bit (docs/design/de-09-brake-decel-
 * logic.md). Three states — OFF / BRAKING / STOPPED — of which BRAKING and
 * STOPPED both render the light ON; they are separate because their *off*
 * conditions differ (moving away vs. pulling away from a stop).
 *
 * This is a line-by-line port of the algorithm in tools/trc_viz.html, which
 * is the version calibrated against the DE-07 ride logs and the source of
 * the default tunables below. tools/fsm_check.py replays a capture through
 * both and asserts they agree tick for tick, so the bench viewer and the
 * firmware stay the same machine.
 *
 * No ESP-IDF dependencies: brake_ctl.c ticks it at 50 Hz on target, and
 * test_host/fsm_replay.c ticks it over a .trc capture on the host.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tick period. 50 Hz is the DE-09 poll rate and the grid trc_viz uses; the
 * hold timers below are multiples of it. */
#define BRAKE_FSM_TICK_MS 20u

typedef enum {
    BRAKE_FSM_OFF     = 0, /* light off                        */
    BRAKE_FSM_BRAKING = 1, /* light on — slowing               */
    BRAKE_FSM_STOPPED = 2, /* light on — stopped / creeping    */
} brake_fsm_state_t;

/* Speeds in mph, accelerations in mph/s, times in ms. Defaults are the
 * trc_viz.html values, calibrated on the DE-07 ride logs. */
typedef struct {
    float    decel_on_mphps;          /* decel that turns the light on      */
    uint16_t decel_on_debounce_ms;    /* ... held this long first           */
    float    stop_speed_mph;          /* at/under = stopped                 */
    float    moving_speed_mph;        /* must be exceeded to leave STOPPED  */
    float    accel_off_mphps;         /* accel that turns the light off     */
    float    accel_off_min_speed_mph; /* ... only above this speed          */
    float    steady_band_mphps;       /* |accel| under this = steady        */
    uint16_t steady_timeout_ms;       /* steady-after-braking hold          */
    uint32_t stop_timeout_ms;         /* max on-time while stopped          */
    uint16_t state_min_dwell_ms;      /* global anti-strobe floor           */
    uint16_t speed_smooth_ms;         /* wheel-speed LPF tau ahead of the
                                         slope estimate — applied in
                                         can_decode.c, carried here so every
                                         DE-09 tunable lives in one struct  */
} brake_tunables_t;

#define BRAKE_TUNABLES_DEFAULT                  \
    ((brake_tunables_t){                        \
        .decel_on_mphps          = 2.0f,        \
        .decel_on_debounce_ms    = 120,         \
        .stop_speed_mph          = 1.0f,        \
        .moving_speed_mph        = 3.0f,        \
        .accel_off_mphps         = 0.5f,        \
        .accel_off_min_speed_mph = 5.0f,        \
        .steady_band_mphps       = 0.75f,       \
        .steady_timeout_ms       = 1500,        \
        .stop_timeout_ms         = 60000,       \
        .state_min_dwell_ms      = 250,         \
        .speed_smooth_ms         = 80,          \
    })

/* One tick's worth of input. clutch/gear are optional: a bike without them
 * (or a stale frame) passes gear = 0, which disables the launch guard and
 * leaves STOPPED to exit on speed or the stop timeout, exactly as the
 * reference does with its zero-filled series. */
typedef struct {
    float   speed_mph;
    bool    speed_valid;   /* false = signal absent/stale — see brake_fsm_step */
    float   accel_mphps;
    bool    accel_valid;
    bool    clutch_pulled;
    uint8_t gear;          /* 0 = neutral or unknown */
} brake_fsm_in_t;

typedef struct {
    brake_tunables_t  tun;
    brake_fsm_state_t state;
    uint32_t since_ms;       /* since the last transition (anti-strobe floor) */
    uint32_t decel_hold_ms;  /* decel has exceeded the threshold this long    */
    uint32_t steady_hold_ms; /* |accel| has been inside the band this long    */
    uint32_t stopped_hold_ms;/* time spent in STOPPED                         */
    uint32_t transitions;    /* lifetime count, for `state show` / the bench  */
} brake_fsm_t;

void brake_fsm_init(brake_fsm_t *f, const brake_tunables_t *tun);

/* Advance the machine by dt_ms and return the new state.
 *
 * With speed_valid == false the machine is held OFF and rearmed: wheel speed
 * is the one REQUIRED input, and a brake light that latches on because the
 * bus went quiet is worse than no light at all. The rearm (rather than a
 * plain hold) means the first tick after the signal returns may transition
 * immediately, which is also what makes a signal dropout indistinguishable
 * from a fresh boot. */
brake_fsm_state_t brake_fsm_step(brake_fsm_t *f, const brake_fsm_in_t *in,
                                 uint16_t dt_ms);

/* Light on? — BRAKING and STOPPED both render ON (DE-09 §4). */
static inline bool brake_fsm_light_on(brake_fsm_state_t s)
{
    return s != BRAKE_FSM_OFF;
}

const char *brake_fsm_state_name(brake_fsm_state_t s);

/* ---- tunables by name (CLI `state tune`, host harness overrides) --------
 *
 * Every tunable is reachable as a float by its struct field name, so the CLI
 * and the host harness can list and set them without either one carrying its
 * own copy of the table. */

int         brake_tune_count(void);
const char *brake_tune_name(int idx);   /* NULL if idx is out of range */

bool brake_tune_get(const brake_tunables_t *t, const char *name, float *out);

/* Rejects an unknown name or a value outside the field's range (which also
 * rejects NaN); integer fields round to nearest. */
bool brake_tune_set(brake_tunables_t *t, const char *name, float value);
bool brake_tune_range(const char *name, float *min_out, float *max_out);

#ifdef __cplusplus
}
#endif
