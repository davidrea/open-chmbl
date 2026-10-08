/*
 * DE-09 — braking state machine (pure, host-testable core).
 *
 * Fuses the DE-08 decoded signals into an OFF / BRAKING / STOPPED light
 * decision. The reference bike publishes no brake-switch bit, so braking is
 * inferred from the slope of wheel speed, qualified by clutch and gear.
 * Specified in docs/design/de-09-brake-decel-logic.md §4.
 *
 * No ESP-IDF headers, no FreeRTOS, no clock of its own: the caller ticks it at a
 * fixed rate and passes dt. That is what makes it unit-testable on the host and
 * what makes the host replay harness exercise the same code the firmware runs.
 *
 * PORTED FROM THE TUNED REFERENCE, NOT RE-DERIVED. run_fsm() in
 * tools/trc_viz.py (and the identical logic in tools/trc_viz.html) is the source
 * of truth for the transition structure: those thresholds were calibrated
 * against real ride logs, so this file reproduces their semantics exactly rather
 * than re-interpreting the design doc. Three details matter and are easy to get
 * wrong:
 *
 *   1. The condition hold timers accumulate BEFORE the guards are evaluated, so
 *      a guard can fire on the same tick its condition reaches the threshold.
 *   2. On any transition all three hold timers reset, not just the one that
 *      fired.
 *   3. `since_trans` starts large enough that the very first tick may transition
 *      immediately — the anti-strobe floor must not suppress the first decision.
 *
 * tools/fsm_check.py asserts this C implementation and the Python reference
 * agree on committed ride captures; see docs/design/de-09-brake-decel-logic.md
 * §7.
 *
 * VALIDITY IS THE CALLER'S JOB. This module takes the signal values it is given
 * and has no notion of staleness, exactly as the reference does. Firmware must
 * not tick it with signals it does not trust — hold the light off and
 * brake_fsm_init() instead (see logger/software/main/fsm_preview.c).
 */
#ifndef BRAKE_FSM_H
#define BRAKE_FSM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nominal tick period. DE-09 specifies a ~50 Hz Poll; the reference steps a
 * 20 ms grid and the tunables are calibrated at that rate, so firmware should
 * tick at this period and pass it as dt rather than measuring elapsed time
 * (a jittery dt would detune the hold timers). */
#define BRAKE_FSM_TICK_MS 20.0f

typedef enum {
    BRAKE_FSM_OFF     = 0,  /* light off                    */
    BRAKE_FSM_BRAKING = 1,  /* light on — slowing           */
    BRAKE_FSM_STOPPED = 2,  /* light on — held at a stop    */
} brake_fsm_state_t;

/* Which guard caused the last transition, for the console transition log.
 * Numbering follows the rule column of de-09-brake-decel-logic.md §4. */
typedef enum {
    BRAKE_FSM_RULE_NONE = 0, /* no transition yet            */
    BRAKE_FSM_RULE_1,        /* OFF     -> BRAKING  slowing hard (debounced) */
    BRAKE_FSM_RULE_2,        /* OFF     -> STOPPED  settling to a stop       */
    BRAKE_FSM_RULE_3,        /* BRAKING -> STOPPED  keep on through the stop */
    BRAKE_FSM_RULE_4,        /* BRAKING -> OFF      accelerating away        */
    BRAKE_FSM_RULE_5,        /* BRAKING -> OFF      steady cruise            */
    BRAKE_FSM_RULE_6A,       /* STOPPED -> OFF      moving away              */
    BRAKE_FSM_RULE_6B,       /* STOPPED -> OFF      launching (clutch+gear)  */
    BRAKE_FSM_RULE_6C,       /* STOPPED -> OFF      parked / long stop       */
} brake_fsm_rule_t;

/* Thresholds and timings. Defaults are the values the reference was tuned to
 * (BrakeTunables in tools/trc_viz.py); brake_fsm_tunables_defaults() fills them.
 * Firmware may override from Kconfig — see components/brake_fsm/Kconfig. */
typedef struct {
    float decel_on_mphps;          /* rule 1: |decel| trigger            */
    float decel_on_debounce_ms;    /* rule 1: continuous hold required   */
    float stop_speed_mph;          /* rules 2/3: STOPPED entry; 6b rolling qualifier */
    float moving_speed_mph;        /* rule 6a: STOPPED exit (hysteresis) */
    float accel_off_mphps;         /* rule 4: accel threshold            */
    float accel_off_min_speed_mph; /* rule 4: speed qualifier            */
    float steady_band_mphps;       /* rule 5: |accel| band               */
    float steady_timeout_ms;       /* rule 5: hold required              */
    float stop_timeout_ms;         /* rule 6c: long-stop release         */
    float state_min_dwell_ms;      /* global anti-strobe floor           */
} brake_fsm_tunables_t;

/* One tick's worth of signals. Values only — see the validity note above. */
typedef struct {
    float speed_mph;     /* wheel speed, mph                              */
    float accel_mphps;   /* derived slope of wheel speed, mph/s (- = decel) */
    bool  accel_valid;   /* false until the slope estimator has primed    */
    bool  clutch_pulled; /* clutch lever in                               */
    bool  in_gear;       /* gear != 0 (neutral is NOT in gear)            */
} brake_fsm_input_t;

typedef struct {
    brake_fsm_tunables_t tun;

    brake_fsm_state_t state;
    brake_fsm_rule_t  last_rule;   /* guard that produced `state`         */

    /* All in ms. since_trans_ms is clamped rather than left to overflow. */
    float since_trans_ms;
    float decel_hold_ms;
    float steady_hold_ms;
    float stopped_hold_ms;
} brake_fsm_t;

/* Fill `t` with the shipped defaults (the tuned reference values). */
void brake_fsm_tunables_defaults(brake_fsm_tunables_t *t);

/* Reset to OFF with the given tunables. Passing NULL uses the defaults.
 * After this the next tick may transition immediately (point 3 above). */
void brake_fsm_init(brake_fsm_t *f, const brake_fsm_tunables_t *t);

/* Advance one tick. dt_ms is the nominal tick period (BRAKE_FSM_TICK_MS).
 * Returns true if the state changed on this tick, in which case f->state and
 * f->last_rule describe the transition. */
bool brake_fsm_step(brake_fsm_t *f, const brake_fsm_input_t *in, float dt_ms);

/* The rendered light: BRAKING and STOPPED are both "on" (protocol ST_BRAKE),
 * OFF is off. Steady level — DE-09 forbids strobing. */
static inline bool brake_fsm_light_on(const brake_fsm_t *f)
{
    return f->state != BRAKE_FSM_OFF;
}

const char *brake_fsm_state_name(brake_fsm_state_t s);
const char *brake_fsm_rule_name(brake_fsm_rule_t r);

#ifdef __cplusplus
}
#endif

#endif /* BRAKE_FSM_H */
