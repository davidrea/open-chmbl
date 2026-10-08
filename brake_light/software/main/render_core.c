/*
 * DE-04 render core — pure state->output map plus the anti-strobe dwell floor.
 * See render_core.h for the contract and ../test_host/render_core_test.c for
 * the proofs. Deliberately free of ESP-IDF headers so it builds on the host.
 */
#include "render_core.h"

bar_out_t render_core_map(brake_state_t state)
{
    switch (state) {
    case ST_BRAKE:
        return BAR_ON;
    case ST_DECEL:
        /* RESERVED, never emitted by the TX FSM. Mapped to ON so an
         * unexpected value fails toward a lit brake light; with brightness
         * deferred there is no dimmer tier to render it as. */
        return BAR_ON;
    case ST_OFF:
        return BAR_OFF;
    default:
        /* Unknown value on the wire: treat as not-braking rather than
         * latching a fake BRAKE (docs/safety-regulatory.md §4). */
        return BAR_OFF;
    }
}

void render_core_init(render_core_t *rc, const render_cfg_t *cfg, int64_t now_ms)
{
    rc->cfg            = *cfg;
    rc->out            = BAR_OFF;
    rc->desired        = BAR_OFF;
    rc->last_change_ms = now_ms;
    rc->changes        = 0;
    rc->held           = 0;
}

/* How long the current output must be held before it may change. */
static uint16_t floor_ms(const render_core_t *rc)
{
    return (rc->out == BAR_ON) ? rc->cfg.min_on_ms : rc->cfg.min_off_ms;
}

bar_out_t render_core_step(render_core_t *rc, bar_out_t desired, int64_t now_ms)
{
    rc->desired = desired;

    if (desired == rc->out) {
        return rc->out;
    }

    int64_t age = now_ms - rc->last_change_ms;
    if (age < 0) {
        /* Clock went backwards (shouldn't happen with esp_timer, but don't
         * let it wedge the output): re-anchor and hold this step. */
        rc->last_change_ms = now_ms;
        rc->held++;
        return rc->out;
    }

    if (age < (int64_t)floor_ms(rc)) {
        rc->held++;
        return rc->out;
    }

    rc->out            = desired;
    rc->last_change_ms = now_ms;
    rc->changes++;
    return rc->out;
}

uint32_t render_core_hold_remaining_ms(const render_core_t *rc, int64_t now_ms)
{
    if (rc->desired == rc->out) {
        return 0;
    }
    int64_t age = now_ms - rc->last_change_ms;
    if (age < 0) {
        return floor_ms(rc);
    }
    if (age >= (int64_t)floor_ms(rc)) {
        return 0;
    }
    return (uint32_t)((int64_t)floor_ms(rc) - age);
}
