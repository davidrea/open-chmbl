/*
 * Operations log to the serial console (view with `idf.py monitor`).
 *
 * Each ui_log_line() emits one line via ESP_LOGI. In this build the console is the
 * ONLY diagnostic surface: recording is silent (no status LED), and the remote LED
 * belongs to the DE-09 brake-light preview (fsm_preview.h). Callable from any task.
 */
#ifndef UI_LOG_H
#define UI_LOG_H

/* Emit one printf-style line to the serial console. */
void ui_log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* UI_LOG_H */
