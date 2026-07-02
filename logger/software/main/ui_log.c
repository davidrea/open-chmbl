#include "ui_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"

#include "lcd_console.h"

static const char *TAG = "logger";

void ui_log_line(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    ESP_LOGI(TAG, "%s", line);
    lcd_console_write(line);
}
