/*
 * Operations log to the serial console (view with `idf.py monitor`).
 *
 * Each ui_log_line() emits one line via ESP_LOGI. At-a-glance status (idle /
 * recording / error) is separate -- see status_led.h for the onboard LED.
 * Callable from any task.
 */
#ifndef UI_LOG_H
#define UI_LOG_H

/* Emit one printf-style line to the serial console. */
void ui_log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* UI_LOG_H */
