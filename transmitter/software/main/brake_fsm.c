/* DE-09 — braking state machine core. Pure C, host-testable. */

#include "brake_fsm.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* since_ms parks here whenever the machine is (re)armed: larger than any
 * plausible dwell floor, so the very next tick may transition. Saturating
 * rather than wrapping keeps that true however long the arm lasts. */
#define SINCE_REARMED UINT32_MAX

static void rearm(brake_fsm_t *f)
{
    f->since_ms = SINCE_REARMED;
    f->decel_hold_ms = 0;
    f->steady_hold_ms = 0;
    f->stopped_hold_ms = 0;
}

void brake_fsm_init(brake_fsm_t *f, const brake_tunables_t *tun)
{
    memset(f, 0, sizeof(*f));
    f->tun = (tun != NULL) ? *tun : BRAKE_TUNABLES_DEFAULT;
    f->state = BRAKE_FSM_OFF;
    rearm(f);
}

brake_fsm_state_t brake_fsm_step(brake_fsm_t *f, const brake_fsm_in_t *in,
                                 uint16_t dt_ms)
{
    const brake_tunables_t *u = &f->tun;

    /* Wheel speed is the one REQUIRED input. Without it there is nothing to
     * infer braking from, so hold the light off and rearm — see the header. */
    if (!in->speed_valid) {
        if (f->state != BRAKE_FSM_OFF) {
            f->state = BRAKE_FSM_OFF;
            f->transitions++;
        }
        rearm(f);
        return f->state;
    }

    const float sp = in->speed_mph;
    const float ac = in->accel_mphps;
    const bool  acv = in->accel_valid;

    /* Hold timers advance first, so a guard can fire on the same tick the
     * hold completes (matches the reference implementation). */
    f->decel_hold_ms = (acv && ac < -u->decel_on_mphps)
                           ? f->decel_hold_ms + dt_ms : 0;
    f->steady_hold_ms = (f->state == BRAKE_FSM_BRAKING && acv &&
                         fabsf(ac) < u->steady_band_mphps)
                            ? f->steady_hold_ms + dt_ms : 0;
    f->stopped_hold_ms = (f->state == BRAKE_FSM_STOPPED)
                             ? f->stopped_hold_ms + dt_ms : 0;

    /* First matching guard wins, and only once the anti-strobe floor has
     * elapsed since the last transition (DE-09 §4). */
    brake_fsm_state_t next = f->state;
    if (f->since_ms >= u->state_min_dwell_ms) {
        switch (f->state) {
        case BRAKE_FSM_OFF:
            /* The `> 0` half is not redundant: the hold is zero unless the
             * decel threshold was exceeded *this tick*, so without it a
             * debounce of 0 would read as "0 >= 0" and turn the light on every
             * tick regardless of deceleration. The reference implementation
             * has that same degenerate case; the tunable's range below starts
             * at one tick so it is not reachable from the CLI, and this guard
             * makes the function safe for any caller. Identical behaviour to
             * the reference for every value in range. */
            if (f->decel_hold_ms > 0 &&
                f->decel_hold_ms >= u->decel_on_debounce_ms) {
                next = BRAKE_FSM_BRAKING;       /* rule 1 — slowing hard   */
            } else if (sp < u->stop_speed_mph) {
                next = BRAKE_FSM_STOPPED;       /* rule 2 — settled to a stop */
            }
            break;

        case BRAKE_FSM_BRAKING:
            if (sp < u->stop_speed_mph) {
                next = BRAKE_FSM_STOPPED;       /* rule 3 — on through the stop */
            } else if (acv && ac > u->accel_off_mphps &&
                       sp > u->accel_off_min_speed_mph) {
                next = BRAKE_FSM_OFF;           /* rule 4 — accelerating away */
            } else if (f->steady_hold_ms >= u->steady_timeout_ms) {
                next = BRAKE_FSM_OFF;           /* rule 5 — steady cruise  */
            }
            break;

        case BRAKE_FSM_STOPPED:
        default:
            if (sp > u->moving_speed_mph ||                       /* 6a */
                (!in->clutch_pulled && in->gear != 0 &&
                 sp > u->stop_speed_mph) ||                       /* 6b */
                f->stopped_hold_ms >= u->stop_timeout_ms) {       /* 6c */
                next = BRAKE_FSM_OFF;
            }
            break;
        }
    }

    if (next != f->state) {
        f->state = next;
        f->transitions++;
        f->since_ms = 0;
        f->decel_hold_ms = 0;
        f->steady_hold_ms = 0;
        f->stopped_hold_ms = 0;
    } else if (f->since_ms <= SINCE_REARMED - dt_ms) {
        f->since_ms += dt_ms;
    }
    return f->state;
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

/* ---- tunables by name ---------------------------------------------------- */

typedef enum { TUNE_F32, TUNE_U16, TUNE_U32 } tune_kind_t;

typedef struct {
    const char *name;
    uint16_t    offset;
    uint8_t     kind;
    float       min;
    float       max;
} tune_field_t;

#define TUNE(field, kind, lo, hi) \
    { #field, (uint16_t)offsetof(brake_tunables_t, field), (kind), (lo), (hi) }

/* Ranges mirror the trc_viz.html sliders, so a value found on the bench can
 * be typed straight into `state tune`. */
static const tune_field_t s_fields[] = {
    TUNE(decel_on_mphps,          TUNE_F32,    0.1f,      8.0f),
    TUNE(decel_on_debounce_ms,    TUNE_U16,   20.0f,    600.0f),
    TUNE(stop_speed_mph,          TUNE_F32,    0.1f,      4.0f),
    TUNE(moving_speed_mph,        TUNE_F32,    0.5f,     10.0f),
    TUNE(accel_off_mphps,         TUNE_F32,    0.1f,      5.0f),
    TUNE(accel_off_min_speed_mph, TUNE_F32,    0.0f,     15.0f),
    TUNE(steady_band_mphps,       TUNE_F32,    0.1f,      3.0f),
    TUNE(steady_timeout_ms,       TUNE_U16,  100.0f,   5000.0f),
    TUNE(stop_timeout_ms,         TUNE_U32, 1000.0f, 120000.0f),
    TUNE(state_min_dwell_ms,      TUNE_U16,    0.0f,   1000.0f),
    TUNE(speed_smooth_ms,         TUNE_U16,    0.0f,    400.0f),
};

#define TUNE_COUNT ((int)(sizeof(s_fields) / sizeof(s_fields[0])))

static const tune_field_t *find_field(const char *name)
{
    for (int i = 0; i < TUNE_COUNT; i++) {
        if (strcmp(s_fields[i].name, name) == 0) {
            return &s_fields[i];
        }
    }
    return NULL;
}

int brake_tune_count(void)
{
    return TUNE_COUNT;
}

const char *brake_tune_name(int idx)
{
    return (idx >= 0 && idx < TUNE_COUNT) ? s_fields[idx].name : NULL;
}

bool brake_tune_range(const char *name, float *min_out, float *max_out)
{
    const tune_field_t *f = find_field(name);
    if (f == NULL) {
        return false;
    }
    if (min_out) *min_out = f->min;
    if (max_out) *max_out = f->max;
    return true;
}

bool brake_tune_get(const brake_tunables_t *t, const char *name, float *out)
{
    const tune_field_t *f = find_field(name);
    if (f == NULL) {
        return false;
    }
    const void *p = (const uint8_t *)t + f->offset;
    switch (f->kind) {
    case TUNE_U16: *out = (float)*(const uint16_t *)p; break;
    case TUNE_U32: *out = (float)*(const uint32_t *)p; break;
    default:       *out = *(const float *)p;           break;
    }
    return true;
}

bool brake_tune_set(brake_tunables_t *t, const char *name, float value)
{
    const tune_field_t *f = find_field(name);
    if (f == NULL || !(value >= f->min && value <= f->max)) {
        return false;   /* also rejects NaN */
    }
    void *p = (uint8_t *)t + f->offset;
    switch (f->kind) {
    case TUNE_U16: *(uint16_t *)p = (uint16_t)(value + 0.5f); break;
    case TUNE_U32: *(uint32_t *)p = (uint32_t)(value + 0.5f); break;
    default:       *(float *)p = value;                       break;
    }
    return true;
}
