/*
 * Indicators on the logger PCB's three LEDs, as wired for the transmitter.
 *
 * A single background task owns all three GPIOs and renders the current
 * state; every function here just records what should be shown and returns
 * immediately, so they are safe to call from the CAN-RX and FSM tick paths.
 *
 *   External indicator  IO18, via Q1 to J4 pin2 -- on a logger this is the
 *                       operator's pod LED. On the transmitter it is the
 *                       BRAKE LIGHT: straight on/off from the DE-09 state
 *                       machine, no pattern. Until the ESP-NOW link carries
 *                       the state to a real light, this pin *is* the output
 *                       under test (see brake_ctl.c).
 *
 *   D5 green            IO2, via R20 -- shows the BUS:
 *                         no CAN traffic    heartbeat
 *                         CAN traffic       inverted heartbeat
 *
 *   D6 red              IO1, via R21 -- fault lamp:
 *                         fault             2 Hz, 50% duty
 *                         otherwise         dark
 *                       Ride logging (trc_log.h) has no indicator of its own
 *                       -- recording is silent -- but a card fault lights
 *                       this lamp too, through status_led_log_fault().
 *
 * "Heartbeat" is a double thump (lub-dub) on a 2 s period; "inverted" is that
 * same waveform logically negated -- mostly lit, with two short dropouts. The
 * pair reads at a glance and, unlike a plain blink, can't be confused with the
 * fault pattern. Timings match the logger firmware's indicator so the same
 * board means the same thing whichever image is flashed.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the three LED GPIOs and start the render task. Call once at boot,
 * before anything that might report a fault. */
void status_led_init(void);

/* The brake-light output itself. Thread-safe, non-blocking. */
void status_led_brake(bool on);

/* Note that a CAN frame was just received. Drives D5: traffic counts as
 * "present" for a short hold-off after the last call, so a live bus shows a
 * steady inverted heartbeat rather than flickering. */
void status_led_can_activity(void);

/* Latch or clear the fault lamp (D6). */
void status_led_fault(bool on);

/* Raise or clear the ride logger's own fault (a card that is present but
 * won't mount, or a file that can't be opened or written). A separate source
 * from status_led_fault(), ORed with it on D6, so the logger clearing its
 * fault can never clear a latched silent-pin or TWAI fault. */
void status_led_log_fault(bool on);

#ifdef __cplusplus
}
#endif
