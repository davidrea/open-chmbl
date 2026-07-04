/*
 * Status indicator on the WROVER-KIT's onboard red LED die (GPIO0 -- the only
 * leg of the RGB LED not shared with the microSD bus or the (removed) LCD).
 * A background task owns the GPIO and renders whichever state is current;
 * status_led_set() just records the desired state and returns immediately.
 */
#ifndef STATUS_LED_H
#define STATUS_LED_H

typedef enum {
    LED_STATE_IDLE,       /* mounted, ready, not recording: slow heartbeat */
    LED_STATE_RECORDING,  /* actively recording: solid on */
    LED_STATE_ERROR,      /* microSD/CAN-start/file failure: fast blink */
} led_state_t;

/* Configure the LED GPIO and start the blink task. Call once at boot. */
void status_led_init(void);

/* Change the displayed state. Thread-safe, non-blocking. */
void status_led_set(led_state_t state);

#endif /* STATUS_LED_H */
