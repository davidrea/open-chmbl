/*
 * Operations log. Each ui_log_line() emits one line via ESP_LOGI (view with
 * `idf.py monitor`) and mirrors it to the onboard LCD console (lcd_console.h)
 * if enabled. Callable from any task.
 */
#ifndef UI_LOG_H
#define UI_LOG_H

/* Emit one printf-style line to the serial console and the LCD. */
void ui_log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* UI_LOG_H */
