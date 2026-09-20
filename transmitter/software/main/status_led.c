#include "status_led.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define LED_BRAKE_GPIO CONFIG_CHMBL_BRAKE_LED_GPIO
#define LED_GREEN_GPIO CONFIG_CHMBL_LED_GREEN_GPIO
#define LED_RED_GPIO   CONFIG_CHMBL_LED_RED_GPIO

/* Render tick. Everything below is a whole number of ticks. */
#define LED_TICK_MS 10

/* Heartbeat: a double thump ("lub-dub") on a 2 s period. The inverted form is
 * this waveform negated -- mostly lit, two brief dropouts. Keep HB_PERIOD_MS an
 * exact multiple of ERR_PERIOD_MS so the fault blink stays even across the
 * phase wrap. */
#define HB_PERIOD_MS   2000
#define HB_LUB_END_MS   100     /* on  [0, 100)   */
#define HB_GAP_END_MS   250     /* off [100, 250) */
#define HB_DUB_END_MS   350     /* on  [250, 350) then off to the end */

/* Fault: 2 Hz, 50% duty. */
#define ERR_PERIOD_MS 500
#define ERR_ON_MS     250

/* How long after the last CAN frame the bus still counts as "active". Longer
 * than the gap between frames on any bus worth decoding, short enough that
 * unplugging the harness shows up promptly. */
#define CAN_ACTIVE_HOLD_MS 400

/* Written by the CAN-RX and FSM tick tasks, read by the render task. Plain
 * bools rather than timestamps: a bool write is a single atomic store, whereas
 * the obvious int64_t esp_timer_get_time() alternative can tear across its two
 * 32-bit halves and strand an indicator in the wrong state. */
static volatile bool s_brake;
static volatile bool s_can_act;
static volatile bool s_fault;

static inline void led_write(gpio_num_t gpio, bool on)
{
    /* All three LEDs are active-high on this board -- D5/D6 are sourced
     * through R20/R21, and IO18 drives Q1's gate (high = Q1 sinks = lit). */
    gpio_set_level(gpio, on ? 1 : 0);
}

/* The heartbeat waveform at `phase` ms into the period. */
static bool heartbeat(int phase)
{
    return (phase < HB_LUB_END_MS) ||
           (phase >= HB_GAP_END_MS && phase < HB_DUB_END_MS);
}

static bool fault_blink(int phase)
{
    return (phase % ERR_PERIOD_MS) < ERR_ON_MS;
}

static void status_led_task(void *arg)
{
    (void)arg;

    int phase = 0;      /* ms into the 2 s pattern period */
    int can_hold = 0;   /* ms of "bus is active" left to run */

    for (;;) {
        const bool hb = heartbeat(phase);

        if (s_can_act) {
            s_can_act = false;
            can_hold = CAN_ACTIVE_HOLD_MS;
        } else if (can_hold > 0) {
            can_hold -= LED_TICK_MS;
        }

        /* The brake light is never a pattern: it is the output under test and
         * has to mean exactly what the state machine decided, with no blink
         * that could be read as braking. */
        led_write(LED_BRAKE_GPIO, s_brake);

        /* D5 green stays independent of the fault lamp, so "no CAN" and
         * "silent-pin fault" remain distinguishable without the console. */
        led_write(LED_GREEN_GPIO, (can_hold > 0) ? !hb : hb);

        led_write(LED_RED_GPIO, s_fault && fault_blink(phase));

        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
        phase += LED_TICK_MS;
        if (phase >= HB_PERIOD_MS) {
            phase = 0;
        }
    }
}

void status_led_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_BRAKE_GPIO) |
                        (1ULL << LED_GREEN_GPIO) |
                        (1ULL << LED_RED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    led_write(LED_BRAKE_GPIO, false);
    led_write(LED_GREEN_GPIO, false);
    led_write(LED_RED_GPIO, false);

    xTaskCreate(status_led_task, "status_led", 2560, NULL, 2, NULL);
}

void status_led_brake(bool on)
{
    s_brake = on;
}

void status_led_can_activity(void)
{
    s_can_act = true;
}

void status_led_fault(bool on)
{
    s_fault = on;
}
