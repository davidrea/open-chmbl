/*
 * Status indication on the logger PCB's three LEDs.
 *
 * A single background task owns all three GPIOs and renders the current state;
 * every function here just records what should be shown and returns
 * immediately, so they are safe to call from the CAN-RX and writer hot paths.
 *
 *   External indicator  IO18, via Q1 to J4 pin2 -- the operator's LED, next to
 *                       the start/stop button. Shows the LOGGER state:
 *                         idle       heartbeat
 *                         recording  inverted heartbeat
 *                         error      2 Hz, 50% duty
 *
 *   D5 green            IO2, via R20 -- shows the BUS:
 *                         no CAN traffic    heartbeat
 *                         CAN traffic       inverted heartbeat
 *
 *   D6 red              IO1, via R21 -- shows the CARD:
 *                         each SD write     brief flash
 *                         error             2 Hz, 50% duty
 *
 * "Heartbeat" is a double thump (lub-dub) on a 2 s period; "inverted" is that
 * same waveform logically negated -- mostly lit, with two short dropouts. The
 * pair reads at a glance and, unlike a plain blink, can't be confused with the
 * error pattern. See status_led.c for the exact timings.
 */
#ifndef STATUS_LED_H
#define STATUS_LED_H

typedef enum {
    LED_STATE_IDLE,       /* mounted, ready, not recording */
    LED_STATE_RECORDING,  /* actively recording            */
    LED_STATE_ERROR,      /* microSD/CAN-start/file failure */
} led_state_t;

/* Configure the three LED GPIOs and start the render task. Call once at boot,
 * before anything that might report an error. */
void status_led_init(void);

/* Change the logger state shown on the external indicator (and, for
 * LED_STATE_ERROR, on D6). Thread-safe, non-blocking. */
void status_led_set(led_state_t state);

/* Note that a CAN frame was just received. Drives D5: traffic is considered
 * "present" for a short hold-off after the last call, so a live bus shows a
 * steady inverted heartbeat rather than flickering. Thread-safe, non-blocking. */
void status_led_can_activity(void);

/* Note that data was just written to the microSD. Drives D6's activity flash.
 * Thread-safe, non-blocking. */
void status_led_sd_activity(void);

#endif /* STATUS_LED_H */
