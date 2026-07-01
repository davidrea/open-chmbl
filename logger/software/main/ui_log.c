#include "ui_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp_wrover_kit.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

static const char *TAG = "logger";

/* Rolling buffer sized to fill the 320x240 panel with the default font. */
#define UI_LOG_LINES     16
#define UI_LOG_LINE_LEN  56

static char           s_lines[UI_LOG_LINES][UI_LOG_LINE_LEN];
static int            s_count;               /* number of populated lines */
static lv_obj_t      *s_label;
static SemaphoreHandle_t s_mutex;

/* Rebuild the label text from the ring (caller holds the display lock). */
static void ui_log_render(void)
{
    if (s_label == NULL) {
        return;
    }
    static char joined[UI_LOG_LINES * UI_LOG_LINE_LEN];
    size_t pos = 0;
    for (int i = 0; i < s_count; i++) {
        int n = snprintf(joined + pos, sizeof(joined) - pos, "%s%s",
                         (i == 0) ? "" : "\n", s_lines[i]);
        if (n < 0) {
            break;
        }
        pos += (size_t)n;
        if (pos >= sizeof(joined)) {
            break;
        }
    }
    lv_label_set_text(s_label, joined);
}

void ui_log_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    if (bsp_display_lock(1000)) {
        s_label = lv_label_create(lv_screen_active());
        lv_label_set_long_mode(s_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_label, LV_PCT(100));
        lv_obj_align(s_label, LV_ALIGN_TOP_LEFT, 2, 2);
        lv_label_set_text(s_label, "");
        bsp_display_unlock();
    }
}

void ui_log_line(const char *fmt, ...)
{
    char line[UI_LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    ESP_LOGI(TAG, "%s", line);

    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
    if (s_count < UI_LOG_LINES) {
        strncpy(s_lines[s_count++], line, UI_LOG_LINE_LEN - 1);
        s_lines[s_count - 1][UI_LOG_LINE_LEN - 1] = '\0';
    } else {
        /* Scroll: drop the oldest, append at the bottom. */
        memmove(s_lines[0], s_lines[1], (UI_LOG_LINES - 1) * UI_LOG_LINE_LEN);
        strncpy(s_lines[UI_LOG_LINES - 1], line, UI_LOG_LINE_LEN - 1);
        s_lines[UI_LOG_LINES - 1][UI_LOG_LINE_LEN - 1] = '\0';
    }

    if (bsp_display_lock(100)) {
        ui_log_render();
        bsp_display_unlock();
    }
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}
