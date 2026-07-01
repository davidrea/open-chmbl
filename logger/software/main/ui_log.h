/*
 * On-screen operations log for the ESP-WROVER-KIT LCD.
 *
 * A simple rolling text panel: each ui_log_line() appends one line (dropping the
 * oldest when full) and mirrors it to the UART console via ESP_LOGI. Thread-safe
 * — callable from app_main, the button callback, and the worker tasks.
 */
#ifndef UI_LOG_H
#define UI_LOG_H

/* Create the LVGL log label. Call once, after bsp_display_start(). */
void ui_log_init(void);

/* Append one printf-style line to the on-screen log (and the console). */
void ui_log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* UI_LOG_H */
