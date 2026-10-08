/*
 * Optional fatal-error indication on an onboard LED — OFF BY DEFAULT.
 *
 * This build has no LED status indication for logging. Two reasons:
 *
 *  * The remote LED (IO18, J4 pin 2) now belongs to the DE-09 brake-light
 *    preview (fsm_preview.h). It is the one LED the rider can see, and sharing
 *    it with logger status would make the FSM decision unreadable, which is the
 *    whole point of this build.
 *  * D5 (IO2) and D6 (IO1) are inside the sealed enclosure and effectively
 *    invisible on a ride, so a status pattern on them buys nothing.
 *
 * Recording is therefore silent: the console operations log (ui_log.h) is the
 * diagnostic surface. For bench work, CONFIG_LOGGER_FAULT_LED_ENABLE turns D6
 * into a 2 Hz blink on a latched fatal error (no card, mount failure, CAN
 * start failure, transceiver not silent) — nothing more. With it off, this
 * module touches no GPIO at all, so the onboard LEDs stay dark and the pins
 * stay free.
 */
#ifndef FAULT_LED_H
#define FAULT_LED_H

#include <stdbool.h>

/* Configure the LED and start its blink task, if the option is enabled.
 * A no-op otherwise. */
void fault_led_init(void);

/* Raise or clear the fault indication. Thread-safe, non-blocking, and a no-op
 * when the option is disabled — call it unconditionally. */
void fault_led_set(bool fault);

#endif /* FAULT_LED_H */
