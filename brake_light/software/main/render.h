/*
 * DE-04 brake-bar render stage — platform half.
 *
 * Owns the brake bar's *only* physical output: the two AP3019 LED-driver
 * enable pins (one per series string, driven together as one logical output).
 * The pins are plain binary GPIOs — high = bar lit, low = bar dark. There is
 * no PWM, no LEDC, no duty cycle, no commanded brightness and no brightness
 * cap anywhere in this path; brightness/auto-dimming is deferred (DE-02), and
 * the per-string LED current is fixed in hardware by the driver's sense
 * resistor. See docs/design/de-04-led-render.md.
 *
 * The render task is the single writer of those pins. link.c publishes the
 * effective braking state into it; the `light` console command publishes a
 * bench override of the same output. Nothing else touches the GPIOs.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"
#include "render_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    brake_state_t state;          /* last state published by link.c */
    bar_out_t     out;            /* output currently driven on the pins */
    bool          overridden;     /* true while the `light` bench override holds */
    bar_out_t     override_out;   /* the forced output, when overridden */
    uint32_t      hold_ms;        /* anti-strobe hold left on a pending change */
    uint32_t      changes;        /* committed bar transitions since boot */
    uint32_t      held;           /* render ticks where a change was held off */
    int           gpio_a;         /* string-A enable GPIO, <0 = not fitted */
    int           gpio_b;         /* string-B enable GPIO, <0 = not fitted */
    uint16_t      min_on_ms;
    uint16_t      min_off_ms;
    uint16_t      tick_ms;
} render_info_t;

/* Configure the enable GPIOs (dark), then start the render task. Called
 * unconditionally from app_main() — the bar is core functionality, not a
 * debug feature, so it works with the dev CLI compiled out. */
void render_init(void);

/* Publish the effective braking state (link.c, ~10 Hz). Ignored while the
 * bench override is engaged. */
void render_set_state(brake_state_t state);

/* Bench override of the same output (`light on|off`). */
void render_override(bar_out_t out);

/* Release the override, back to the link-driven state (`light auto`). */
void render_override_clear(void);

void render_get_info(render_info_t *info);

#ifdef __cplusplus
}
#endif
