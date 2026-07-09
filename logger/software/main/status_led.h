/*
 * Status indicator on the logger board's status LED (GPIO18, switching an
 * external panel LED through a low-side N-FET on the J4 breakout; the GPIO is
 * configurable -- see status_led.c / Kconfig). A background task owns the GPIO
 * and renders whichever state is current; status_led_set() just records the
 * desired state and returns immediately.
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
