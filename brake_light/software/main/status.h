/*
 * Status indicator — minimal DE-10 slice.
 *
 * The first-pass brake_light PCB carries two discrete status LEDs, D13 (RED)
 * and D14 (GRN), each anode-fed through a 100 ohm resistor (R21/R20) from an
 * MCU pin with the cathode on GND: GPIO high = lit. They are *separate from
 * the brake bar*, which is exactly what DE-03 needs — link-loss and
 * pre-first-packet "waiting" are now indicated here instead of blinking the
 * brake bar (illegal and unsafe, docs/safety-regulatory.md §1).
 *
 * This is deliberately NOT the full DE-10 colour/blink-code engine: no status
 * priority resolution, no fault-class blink counts, no battery or charge
 * codes, no night-dim. It reports link health only. See
 * docs/design/de-10-status-indicator.md for what is still deferred.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    link_status_t link;        /* the status being indicated */
    const char   *pattern;     /* human-readable code, e.g. "GRN steady" */
    bool          grn_on;      /* current pin levels */
    bool          red_on;
    int           gpio_grn;    /* <0 = not fitted */
    int           gpio_red;
    uint16_t      blink_ms;    /* blink half-period */
} status_info_t;

/* Configure the indicator GPIOs (dark) and start the indicator task. Called
 * unconditionally from app_main(). */
void status_init(void);

/* Publish link health (link.c, ~10 Hz). */
void status_set_link(link_status_t link);

void status_get_info(status_info_t *info);

#ifdef __cplusplus
}
#endif
