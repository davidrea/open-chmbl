/*
 * Bring-up scratch app for the logger PCB. Covers:
 *
 *   BRINGUP.md §4.1 -- the two status LEDs light, on the GPIOs the netlist says.
 *     D6 (red)   IO1, U1 pin 39, via R21 -- active-high
 *     D5 (green) IO2, U1 pin 38, via R20 -- active-high
 *
 *   BRINGUP.md §4.5 -- microSD card-detect reads correctly.
 *     DET_A      IO8, U1 pin 12, J5 pin10; R17 pull-up, active-low
 *
 * The LED pattern is deliberately phased rather than a plain alternating blink:
 * a simultaneous or free-running blink can't tell "both LEDs work" apart from
 * "the pins are swapped". Each phase drives exactly one thing and says over
 * the console what should be visible, so a mismatch is a swap, not a guess.
 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_RED_GPIO   GPIO_NUM_1  /* D6 */
#define LED_GREEN_GPIO GPIO_NUM_2  /* D5 */
#define DET_A_GPIO     GPIO_NUM_8  /* J5 card-detect, active-low */

static const char *TAG = "blink";
static const char *DTAG = "cardet";
static const char *STAG = "soak";

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_EXT:      return "EXT";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_USB:      return "USB";
    case ESP_RST_JTAG:     return "JTAG";
    default:               return "OTHER";
    }
}

/* Heartbeat for the §4.3 reset soak (../reset_soak.py).
 *
 * Resetting this board drops the native USB-serial-JTAG link, and host
 * re-enumeration takes ~2 s -- so the boot log and the app banner are already
 * gone by the time a script can reopen the port. There is no way to capture
 * them over USB alone (the UART0 test points TP1/TP2 would see everything, but
 * that needs an external adapter). Instead the app reports continuously: a
 * fresh, low uptime after a reset is what proves the reboot actually happened
 * and reached app_main, and the reset reason says how it got there. */
static void soak_heartbeat_task(void *arg)
{
    esp_reset_reason_t reason = (esp_reset_reason_t)(intptr_t)arg;
    for (;;) {
        ESP_LOGI(STAG, "alive up=%lld ms rst=%d(%s)",
                 esp_timer_get_time() / 1000, (int)reason, reset_reason_str(reason));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* DET_A is active-low: R17 pulls it up, a seated card pulls J5 pin10 to GND. */
static inline const char *det_a_str(int level)
{
    return level ? "HIGH -> no card" : "LOW  -> card seated";
}

/* Report DET_A whenever it changes, so §4.5 (both states) and §6.6 (insert/
 * remove cycling) can be done in one session instead of one boot per state.
 * Polled rather than interrupt-driven -- this is a scratch app and a mechanical
 * detect switch bounces; 50 ms sampling with a change filter is plenty. */
static void card_detect_task(void *arg)
{
    int last = (int)(intptr_t)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        int now = gpio_get_level(DET_A_GPIO);
        if (now != last) {
            last = now;
            ESP_LOGI(DTAG, "DET_A (IO8) changed: %d  %s", now, det_a_str(now));
        }
    }
}

/* Both LEDs are active-high (LED anode to 3V3 side of R20/R21, cathode to the
 * GPIO would be active-low -- this board is the other way round). */
static void led_set(gpio_num_t gpio, bool on)
{
    gpio_set_level(gpio, on ? 1 : 0);
}

static void both(bool on)
{
    led_set(LED_RED_GPIO, on);
    led_set(LED_GREEN_GPIO, on);
}

/* n on/off cycles at `period_ms` total per cycle, 50% duty. */
static void blink(gpio_num_t gpio, int n, int period_ms)
{
    for (int i = 0; i < n; i++) {
        led_set(gpio, true);
        vTaskDelay(pdMS_TO_TICKS(period_ms / 2));
        led_set(gpio, false);
        vTaskDelay(pdMS_TO_TICKS(period_ms / 2));
    }
}

void app_main(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_RED_GPIO) | (1ULL << LED_GREEN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    both(false);

    /* DET_A: input, NO internal pull. The board has an external pull-up (R17),
     * and enabling the internal one would read high even if R17 were missing or
     * unstuffed -- i.e. it would turn a real §4.5 failure into a false pass. */
    const gpio_config_t det_cfg = {
        .pin_bit_mask = 1ULL << DET_A_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&det_cfg));

    ESP_LOGI(TAG, "logger PCB status-LED check -- RED=IO%d (D6), GREEN=IO%d (D5)",
             LED_RED_GPIO, LED_GREEN_GPIO);

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(STAG, "boot: reset reason %d (%s)", (int)reason, reset_reason_str(reason));
    xTaskCreate(soak_heartbeat_task, "soak_hb", 2560,
                (void *)(intptr_t)reason, 2, NULL);

    int det_a = gpio_get_level(DET_A_GPIO);
    ESP_LOGI(DTAG, "DET_A (IO8) at boot: %d  %s", det_a, det_a_str(det_a));
    ESP_LOGI(DTAG, "expect HIGH with J5 empty, LOW with a card seated (R17 pull-up)");
    xTaskCreate(card_detect_task, "card_detect", 2560,
                (void *)(intptr_t)det_a, 3, NULL);

    for (;;) {
        ESP_LOGI(TAG, "phase 1: RED only, 3 slow blinks");
        blink(LED_RED_GPIO, 3, 600);
        vTaskDelay(pdMS_TO_TICKS(400));

        ESP_LOGI(TAG, "phase 2: GREEN only, 3 slow blinks");
        blink(LED_GREEN_GPIO, 3, 600);
        vTaskDelay(pdMS_TO_TICKS(400));

        ESP_LOGI(TAG, "phase 3: alternating, 6 swaps");
        for (int i = 0; i < 6; i++) {
            led_set(LED_RED_GPIO, i % 2 == 0);
            led_set(LED_GREEN_GPIO, i % 2 != 0);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        both(false);
        vTaskDelay(pdMS_TO_TICKS(400));

        ESP_LOGI(TAG, "phase 4: both on, 1 s");
        both(true);
        vTaskDelay(pdMS_TO_TICKS(1000));
        both(false);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
