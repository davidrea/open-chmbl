/*
 * DE-09 brake-light preview — the on-board state machine and the remote LED.
 *
 * WHAT THIS IS. This firmware runs the real DE-09 braking state machine
 * (components/brake_fsm) against the real decoded CAN signals, at the real
 * 50 Hz tick, and lights the remote LED on J4 pin 2 (IO18, through Q1) whenever
 * the machine says the brake light should be ON.
 *
 * WHAT THIS IS NOT. There is NO ESP-NOW radio in this build — the logger does
 * not pair with, or transmit to, the rider-side brake_light unit. The LED is a
 * stand-in for "this firmware would now be commanding ST_BRAKE over the link".
 * It is a ride-validation instrument: the owner rides with the logger, watches
 * the LED, and afterwards reads the .trc plus the console transition log to see
 * exactly why the FSM decided what it did. Nothing here is the product path;
 * that is the transmitter (see docs/design/de-09-brake-decel-logic.md §7).
 *
 * The LED is a steady level — on or off, no blinking and no brightness control.
 * BRAKING and STOPPED both render as ON (protocol ST_BRAKE), OFF renders as
 * off. DE-09 and docs/safety-regulatory.md both forbid strobing.
 */
#ifndef FSM_PREVIEW_H
#define FSM_PREVIEW_H

#include <stdbool.h>

/* Configure the remote-LED GPIO (off), load the Kconfig tunables, and start the
 * 50 Hz tick task. Call after can_tap_init(). */
void fsm_preview_init(void);

/* Current rendered output: true while the firmware would be commanding the
 * brake light on. For the console/diagnostics only. */
bool fsm_preview_light_on(void);

#endif /* FSM_PREVIEW_H */
