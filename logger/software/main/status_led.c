#include "status_led.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define LED_EXT_GPIO   CONFIG_LOGGER_LED_EXT_GPIO
#define LED_GREEN_GPIO CONFIG_LOGGER_LED_GREEN_GPIO
#define LED_RED_GPIO   CONFIG_LOGGER_LED_RED_GPIO

/* Render tick. Everything below is a whole number of ticks; 10 ms is fine
 * enough for a 40 ms activity flash and cheap enough to ignore. */
#define LED_TICK_MS 10

/* Heartbeat: a double thump ("lub-dub") on a 2 s period, so a live logger looks
 * deliberately alive rather than merely blinking. The inverted form is this
 * waveform negated -- mostly lit, two brief dropouts -- which is why the two
 * states are distinguishable at a glance and neither resembles the error
 * pattern. Keep HB_PERIOD_MS an exact multiple of ERR_PERIOD_MS so the error
 * blink stays even across the phase wrap. */
#define HB_PERIOD_MS   2000
#define HB_LUB_END_MS   100     /* on  [0, 100)   */
#define HB_GAP_END_MS   250     /* off [100, 250) */
#define HB_DUB_END_MS   350     /* on  [250, 350) then off to the end */

/* Error: 2 Hz, 50% duty. */
#define ERR_PERIOD_MS 500
#define ERR_ON_MS     250

/* How long after the last CAN frame the bus still counts as "active". Longer
 * than the gap between frames on any bus worth logging, short enough that
 * unplugging the bus shows up promptly. */
#define CAN_ACTIVE_HOLD_MS 400

/* SD write activity flash. The forced off-time caps the flash rate at ~10 Hz:
 * without it, a sustained capture (~1500 frames/s) would re-trigger the flash
 * every tick and D6 would look solid-on, indistinguishable from a fault. */
#define SD_FLASH_ON_MS  40
#define SD_FLASH_OFF_MS 60

static volatile led_state_t s_state = LED_STATE_IDLE;

/* Set by the CAN-RX and writer tasks, consumed by the render task. Plain bools
 * rather than timestamps: a bool write is a single atomic store, whereas the
 * obvious int64_t esp_timer_get_time() alternative can tear across the two
 * 32-bit halves on this core and strand the indicator in the wrong state. */
static volatile bool s_can_act;
static volatile bool s_sd_act;

static inline void led_write(gpio_num_t gpio, bool on)
{
    /* All three LEDs are active-high on this board -- D5/D6 are sourced through
     * R20/R21, and IO18 drives Q1's gate (high = Q1 sinks = lit). */
    gpio_set_level(gpio, on ? 1 : 0);
}

/* The heartbeat waveform at `phase` ms into the period. */
static bool heartbeat(int phase)
{
    return (phase < HB_LUB_END_MS) ||
           (phase >= HB_GAP_END_MS && phase < HB_DUB_END_MS);
}

static bool error_blink(int phase)
{
    return (phase % ERR_PERIOD_MS) < ERR_ON_MS;
}

static void status_led_task(void *arg)
{
    (void)arg;

    int phase = 0;          /* ms into the 2 s pattern period */
    int can_hold = 0;       /* ms of "bus is active" left to run */
    int flash_on = 0;       /* ms of D6 activity flash left to run */
    int flash_off = 0;      /* ms of enforced D6 dark time left to run */

    for (;;) {
        const led_state_t state = s_state;
        const bool hb  = heartbeat(phase);
        const bool err = error_blink(phase);

        /* --- CAN activity: re-arm the hold on every frame, then decay. --- */
        if (s_can_act) {
            s_can_act = false;
            can_hold = CAN_ACTIVE_HOLD_MS;
        } else if (can_hold > 0) {
            can_hold -= LED_TICK_MS;
        }
        const bool can_active = can_hold > 0;

        /* --- SD activity: one-shot flash, rate-limited by the off-time. --- */
        bool red_activity = false;
        if (flash_on > 0) {
            flash_on -= LED_TICK_MS;
            red_activity = true;
        } else if (flash_off > 0) {
            flash_off -= LED_TICK_MS;
        } else if (s_sd_act) {
            s_sd_act = false;
            flash_on = SD_FLASH_ON_MS - LED_TICK_MS;
            flash_off = SD_FLASH_OFF_MS;
            red_activity = true;
        }

        /* --- External indicator: the logger's own state. --- */
        switch (state) {
        case LED_STATE_ERROR:
            led_write(LED_EXT_GPIO, err);
            break;
        case LED_STATE_RECORDING:
            led_write(LED_EXT_GPIO, !hb);
            break;
        case LED_STATE_IDLE:
        default:
            led_write(LED_EXT_GPIO, hb);
            break;
        }

        /* --- D5 green: the bus. Independent of the logger state, so it still
         *     reports bus liveness while an error is being shown elsewhere --
         *     "no card" and "no CAN" are different faults and want to be
         *     distinguishable without touching the console. --- */
        led_write(LED_GREEN_GPIO, can_active ? !hb : hb);

        /* --- D6 red: the card. Error takes over the LED entirely; the write
         *     flash would be lost in a 2 Hz blink anyway. --- */
        led_write(LED_RED_GPIO, state == LED_STATE_ERROR ? err : red_activity);

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
        .pin_bit_mask = (1ULL << LED_EXT_GPIO) |
                        (1ULL << LED_GREEN_GPIO) |
                        (1ULL << LED_RED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    led_write(LED_EXT_GPIO, false);
    led_write(LED_GREEN_GPIO, false);
    led_write(LED_RED_GPIO, false);

    xTaskCreate(status_led_task, "status_led", 2560, NULL, 2, NULL);
}

void status_led_set(led_state_t state)
{
    s_state = state;
}

void status_led_can_activity(void)
{
    s_can_act = true;
}

void status_led_sd_activity(void)
{
    s_sd_act = true;
}
