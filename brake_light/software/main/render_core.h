/*
 * DE-04 render core — the pure, host-testable half of the brake-bar render
 * stage. No ESP-IDF, no GPIO, no time source of its own: the caller supplies
 * the clock, so the whole state->output map and the anti-strobe dwell floor
 * are provable off-target (see ../test_host/).
 *
 * The bar is a BINARY output: high = lit, low = dark. Brightness/PWM dimming
 * is deliberately deferred — see docs/design/de-04-led-render.md and
 * docs/design/de-02-auto-brightness.md. There is no duty cycle, no commanded
 * brightness and no brightness cap in this path.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The single physical output this element owns. */
typedef enum {
    BAR_OFF = 0,
    BAR_ON  = 1,
} bar_out_t;

/* Anti-strobe floor (BL-RND-2). Asymmetric on purpose:
 *
 *  - min_on_ms  — once the bar is lit it stays lit at least this long. This
 *    is the floor that actually prevents a strobe, and it costs nothing in
 *    safety: holding a brake light on slightly too long is honest.
 *  - min_off_ms — once the bar is dark it stays dark at least this long
 *    before it may relight. Kept small, because this is the only floor that
 *    can delay a *brake-on* edge against the <=100 ms end-to-end budget
 *    (ARCHITECTURE.md §5), and it only bites if the bar went dark moments ago.
 *
 * Worst case the pair bounds the bar to one on->off->on cycle per
 * (min_on_ms + min_off_ms). The primary guards against oscillation live
 * upstream (the TX FSM's hysteresis, decel debounce and its own dwell floor,
 * docs/firmware.md) and in link.c, which holds the bar steady rather than
 * blinking it; this floor is defence in depth inside the render stage.
 */
typedef struct {
    uint16_t min_on_ms;
    uint16_t min_off_ms;
} render_cfg_t;

typedef struct {
    render_cfg_t cfg;
    bar_out_t    out;            /* output currently committed to the pins */
    bar_out_t    desired;        /* last requested output */
    int64_t      last_change_ms; /* when `out` last changed */
    uint32_t     changes;        /* committed transitions (diagnostics) */
    uint32_t     held;           /* steps where a change was held off */
} render_core_t;

/* Map an effective braking state onto the binary bar output.
 *
 * ST_DECEL is RESERVED in the protocol and is not emitted by the transmitter
 * (protocol.h, docs/protocol.md §2). It is mapped to ON rather than left
 * undefined: if a future soft-cue tier ever starts emitting it, the fail-safe
 * reading of "the bike is slowing" is a lit brake light, not a dark one. With
 * brightness deferred there is no middle tier to render it as. */
bar_out_t render_core_map(brake_state_t state);

/* Start dark, with the dwell clock anchored at `now_ms`. */
void render_core_init(render_core_t *rc, const render_cfg_t *cfg, int64_t now_ms);

/* Feed the desired output and the current time; returns the output that may be
 * driven right now. A change that arrives inside the dwell floor is remembered
 * and committed by a later call, so this must be stepped periodically (the
 * platform render task does so at CONFIG_CHMBL_RENDER_TICK_MS). */
bar_out_t render_core_step(render_core_t *rc, bar_out_t desired, int64_t now_ms);

/* Milliseconds until the pending desired output could be committed; 0 when
 * nothing is pending or the floor has already elapsed. Diagnostics only. */
uint32_t render_core_hold_remaining_ms(const render_core_t *rc, int64_t now_ms);

#ifdef __cplusplus
}
#endif
