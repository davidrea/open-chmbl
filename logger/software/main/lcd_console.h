/*
 * Minimal scrolling text console on the ESP-WROVER-KIT's onboard LCD.
 *
 * Drives the panel directly with ESP-IDF's built-in esp_lcd driver (ST7789 by
 * default; ILI9341 selectable in menuconfig for older kits) — no LVGL, no BSP.
 * Renders lines with an embedded 8x8 bitmap font. Disabled entirely (both
 * functions become no-ops) if CONFIG_LOGGER_LCD_ENABLE is off.
 */
#ifndef LCD_CONSOLE_H
#define LCD_CONSOLE_H

/* Turn on the backlight and bring up the panel. Call once at boot, before any
 * lcd_console_write() calls. Logs and returns on failure (never aborts) so
 * the caller keeps working over serial only. */
void lcd_console_init(void);

/* Append one line to the scrolling on-screen log and redraw. Truncates to the
 * console width. No-op if the panel failed to initialize. Thread-safe. */
void lcd_console_write(const char *line);

#endif /* LCD_CONSOLE_H */
