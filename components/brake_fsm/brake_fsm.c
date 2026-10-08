/* DE-09 — braking state machine. Pure C, host-testable. See brake_fsm.h. */

#include "brake_fsm.h"

#include <math.h>
#include <stddef.h>

/* Seed for since_trans_ms: large enough that the first tick is never blocked by
 * the anti-strobe floor, small enough that `+= dt` still changes a float (the
 * reference uses 1e12, where a float += 20 is a no-op; the clamp below makes the
 * distinction moot but 1e9 keeps the arithmetic meaningful if it is ever read).
 * Also the clamp ceiling, so a long dwell cannot drift toward infinity. */
#define SINCE_TRANS_MAX 1.0e9f

void brake_fsm_tunables_defaults(brake_fsm_tunables_t *t)
{
    /* The values the Python/JS reference was tuned to on real ride logs —
     * BrakeTunables in tools/trc_viz.py. Changing one of these changes a
     * calibrated safety behaviour: retune against the captures in logger/ and
     * re-run tools/fsm_check.py. */
    t->decel_on_mphps          = 2.0f;
    t->decel_on_debounce_ms    = 120.0f;
    t->stop_speed_mph          = 1.0f;
    t->moving_speed_mph        = 3.0f;
    t->accel_off_mphps         = 0.5f;
    t->accel_off_min_speed_mph = 5.0f;
    t->steady_band_mphps       = 0.75f;
    t->steady_timeout_ms       = 1500.0f;
    t->stop_timeout_ms         = 60000.0f;
    t->state_min_dwell_ms      = 250.0f;
}

void brake_fsm_init(brake_fsm_t *f, const brake_fsm_tunables_t *t)
{
    if (t != NULL) {
        f->tun = *t;
    } else {
        brake_fsm_tunables_defaults(&f->tun);
    }
    f->state           = BRAKE_FSM_OFF;
    f->last_rule       = BRAKE_FSM_RULE_NONE;
    f->since_trans_ms  = SINCE_TRANS_MAX;  /* allow an immediate first change */
    f->decel_hold_ms   = 0.0f;
    f->steady_hold_ms  = 0.0f;
    f->stopped_hold_ms = 0.0f;
}

bool brake_fsm_step(brake_fsm_t *f, const brake_fsm_input_t *in, float dt_ms)
{
    const brake_fsm_tunables_t *tun = &f->tun;

    /* NaN would make every comparison below false, which silently reads as
     * "no guard matched" rather than as a bad input. Treat it as invalid. */
    const bool ac_valid = in->accel_valid && !isnan(in->accel_mphps);
    const float sp = in->speed_mph;
    const float ac = in->accel_mphps;

    /* --- condition hold timers -------------------------------------------
     * Accumulated BEFORE the guards run, so a hold that reaches its threshold
     * on this tick fires on this tick. Reordering this below the guard block
     * delays every debounced transition by one tick. */
    const bool decel_active = ac_valid && (ac < -tun->decel_on_mphps);
    f->decel_hold_ms = decel_active ? f->decel_hold_ms + dt_ms : 0.0f;

    const bool steady_active = (f->state == BRAKE_FSM_BRAKING) && ac_valid &&
                               (fabsf(ac) < tun->steady_band_mphps);
    f->steady_hold_ms = steady_active ? f->steady_hold_ms + dt_ms : 0.0f;

    f->stopped_hold_ms = (f->state == BRAKE_FSM_STOPPED)
                             ? f->stopped_hold_ms + dt_ms : 0.0f;

    /* --- guards: first match wins ---------------------------------------
     * Gated as a whole by the global anti-strobe floor, so no single rule can
     * strobe the light (de-09 §4 "Anti-strobe"). */
    brake_fsm_state_t new_state = f->state;
    brake_fsm_rule_t  rule      = BRAKE_FSM_RULE_NONE;

    if (f->since_trans_ms >= tun->state_min_dwell_ms) {
        switch (f->state) {
        case BRAKE_FSM_OFF:
            if (f->decel_hold_ms >= tun->decel_on_debounce_ms) {
                new_state = BRAKE_FSM_BRAKING;  rule = BRAKE_FSM_RULE_1;
            } else if (sp < tun->stop_speed_mph) {
                new_state = BRAKE_FSM_STOPPED;  rule = BRAKE_FSM_RULE_2;
            }
            break;

        case BRAKE_FSM_BRAKING:
            if (sp < tun->stop_speed_mph) {
                new_state = BRAKE_FSM_STOPPED;  rule = BRAKE_FSM_RULE_3;
            } else if (ac_valid && ac > tun->accel_off_mphps &&
                       sp > tun->accel_off_min_speed_mph) {
                new_state = BRAKE_FSM_OFF;      rule = BRAKE_FSM_RULE_4;
            } else if (f->steady_hold_ms >= tun->steady_timeout_ms) {
                new_state = BRAKE_FSM_OFF;      rule = BRAKE_FSM_RULE_5;
            }
            break;

        case BRAKE_FSM_STOPPED:
        default:
            if (sp > tun->moving_speed_mph) {
                new_state = BRAKE_FSM_OFF;      rule = BRAKE_FSM_RULE_6A;
            } else if (!in->clutch_pulled && in->in_gear &&
                       sp > tun->stop_speed_mph) {
                new_state = BRAKE_FSM_OFF;      rule = BRAKE_FSM_RULE_6B;
            } else if (f->stopped_hold_ms >= tun->stop_timeout_ms) {
                new_state = BRAKE_FSM_OFF;      rule = BRAKE_FSM_RULE_6C;
            }
            break;
        }
    }

    if (new_state != f->state) {
        f->state           = new_state;
        f->last_rule       = rule;
        f->since_trans_ms  = 0.0f;
        /* ALL three hold timers reset on a transition, not just the one that
         * fired: the new state re-earns each condition from scratch. */
        f->decel_hold_ms   = 0.0f;
        f->steady_hold_ms  = 0.0f;
        f->stopped_hold_ms = 0.0f;
        return true;
    }

    if (f->since_trans_ms < SINCE_TRANS_MAX) {
        f->since_trans_ms += dt_ms;
    }
    return false;
}

const char *brake_fsm_state_name(brake_fsm_state_t s)
{
    switch (s) {
    case BRAKE_FSM_OFF:     return "OFF";
    case BRAKE_FSM_BRAKING: return "BRAKING";
    case BRAKE_FSM_STOPPED: return "STOPPED";
    default:                return "?";
    }
}

const char *brake_fsm_rule_name(brake_fsm_rule_t r)
{
    switch (r) {
    case BRAKE_FSM_RULE_1:  return "1";
    case BRAKE_FSM_RULE_2:  return "2";
    case BRAKE_FSM_RULE_3:  return "3";
    case BRAKE_FSM_RULE_4:  return "4";
    case BRAKE_FSM_RULE_5:  return "5";
    case BRAKE_FSM_RULE_6A: return "6a";
    case BRAKE_FSM_RULE_6B: return "6b";
    case BRAKE_FSM_RULE_6C: return "6c";
    case BRAKE_FSM_RULE_NONE:
    default:                return "-";
    }
}
