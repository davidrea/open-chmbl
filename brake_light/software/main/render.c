/*
 * DE-04 brake-bar render stage — platform half (GPIO + task).
 *
 * Binary output only: CONFIG_CHMBL_BAR_EN_A_GPIO and
 * CONFIG_CHMBL_BAR_EN_B_GPIO go high together when the bar should be lit and
 * low when it should be dark. Both AP3019 boost drivers' CTRL/EN pins are
 * driven as a *static enable* rather than a PWM dimming input — the explicit
 * deferral recorded in docs/design/de-04-led-render.md. The pure part of the
 * stage (state->output, anti-strobe) is render_core.c.
 */
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "render.h"

static const char *TAG = "render";

#define BAR_GPIO_A  (CONFIG_CHMBL_BAR_EN_A_GPIO)
#define BAR_GPIO_B  (CONFIG_CHMBL_BAR_EN_B_GPIO)

static render_core_t s_core;

/* Written by link.c / the console, read by the render task. Single aligned
 * word each, so a plain volatile is enough — there is no multi-field
 * invariant to tear. */
static volatile brake_state_t s_state      = ST_OFF;
static volatile bool          s_override   = false;
static volatile bar_out_t     s_override_v = BAR_OFF;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void bar_drive(bar_out_t out)
{
    int level = (out == BAR_ON) ? 1 : 0;
    if (BAR_GPIO_A >= 0) {
        gpio_set_level((gpio_num_t)BAR_GPIO_A, level);
    }
    if (BAR_GPIO_B >= 0) {
        gpio_set_level((gpio_num_t)BAR_GPIO_B, level);
    }
}

static void bar_gpio_init(int gpio, const char *which)
{
    if (gpio < 0) {
        ESP_LOGW(TAG, "brake-bar string %s enable not fitted (GPIO = -1)", which);
        return;
    }
    gpio_reset_pin((gpio_num_t)gpio);
    gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)gpio, 0);
    ESP_LOGI(TAG, "brake-bar string %s enable on GPIO%d (active high)", which, gpio);
}

static void render_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CHMBL_RENDER_TICK_MS));

        bar_out_t desired = s_override ? s_override_v
                                       : render_core_map(s_state);
        bar_drive(render_core_step(&s_core, desired, now_ms()));
    }
}

void render_init(void)
{
    const render_cfg_t cfg = {
        .min_on_ms  = CONFIG_CHMBL_RENDER_MIN_ON_MS,
        .min_off_ms = CONFIG_CHMBL_RENDER_MIN_OFF_MS,
    };
    render_core_init(&s_core, &cfg, now_ms());

    bar_gpio_init(BAR_GPIO_A, "A");
    bar_gpio_init(BAR_GPIO_B, "B");
    bar_drive(BAR_OFF);

    xTaskCreate(render_task, "render", 2560, NULL, 5, NULL);
}

void render_set_state(brake_state_t state)
{
    s_state = state;
}

void render_override(bar_out_t out)
{
    s_override_v = out;
    s_override   = true;
}

void render_override_clear(void)
{
    s_override = false;
}

void render_get_info(render_info_t *info)
{
    info->state        = s_state;
    info->out          = s_core.out;
    info->overridden   = s_override;
    info->override_out = s_override_v;
    info->hold_ms      = render_core_hold_remaining_ms(&s_core, now_ms());
    info->changes      = s_core.changes;
    info->held         = s_core.held;
    info->gpio_a       = BAR_GPIO_A;
    info->gpio_b       = BAR_GPIO_B;
    info->min_on_ms    = CONFIG_CHMBL_RENDER_MIN_ON_MS;
    info->min_off_ms   = CONFIG_CHMBL_RENDER_MIN_OFF_MS;
    info->tick_ms      = CONFIG_CHMBL_RENDER_TICK_MS;
}
