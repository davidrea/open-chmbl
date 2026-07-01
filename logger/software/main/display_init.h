/*
 * LCD bring-up for the ESP-WROVER-KIT v4.1.
 *
 * The kit's panel is an ILI9341, but the esp_wrover_kit BSP only ships the
 * ST7789 driver (its "ILI9341" menuconfig option merely flips colour order and
 * a mirror flag — it still calls esp_lcd_new_panel_st7789). Driving the ILI9341
 * with the ST7789 init sequence produces a garbage/striped panel. So we init the
 * LCD ourselves with the real ILI9341 driver + esp_lvgl_port, reusing the BSP's
 * pin macros and its backlight helpers. The BSP still owns the SD card + button.
 *
 * After this runs, bsp_display_lock()/bsp_display_unlock() work as usual (they
 * wrap the global esp_lvgl_port lock we initialise here).
 */
#ifndef DISPLAY_INIT_H
#define DISPLAY_INIT_H

#include "lvgl.h"

/* Bring up SPI + the ILI9341 panel + LVGL and turn the backlight on. Returns the
 * LVGL display handle (or aborts via ESP_ERROR_CHECK on a fatal init error). */
lv_display_t *logger_display_start(void);

#endif /* DISPLAY_INIT_H */
