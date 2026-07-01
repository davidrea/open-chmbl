#include "display_init.h"

#include <stdlib.h>

#include "bsp/esp_wrover_kit.h"   /* pin macros + bsp_display_brightness_init/backlight_on */
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

/* Native ILI9341 geometry on the WROVER-KIT (portrait). */
#define LCD_H_RES       240
#define LCD_V_RES       320
#define LCD_BUF_LINES   30      /* draw-buffer height */

/* Instrumentation: draw solid colour bars straight to the panel (no LVGL) so we
 * can see whether SPI + the panel driver actually light pixels. Set to 0 to
 * disable once the display is trusted. */
#define LOGGER_DISPLAY_SELFTEST   1

static esp_lcd_panel_handle_t s_panel;

/* --- direct-draw self-test (bypasses LVGL) --------------------------------- */

/* Fill the whole panel with one RGB565 colour, band by band, straight through
 * esp_lcd. `buf` holds LCD_H_RES * LCD_BUF_LINES pre-byte-swapped pixels. */
static void fill_screen(uint16_t *buf, size_t buf_px, uint16_t color)
{
    const uint16_t v = __builtin_bswap16(color);   /* RGB565 big-endian on the wire */
    for (size_t i = 0; i < buf_px; i++) {
        buf[i] = v;
    }
    for (int y = 0; y < LCD_V_RES; y += LCD_BUF_LINES) {
        int y1 = y + LCD_BUF_LINES;
        if (y1 > LCD_V_RES) {
            y1 = LCD_V_RES;
        }
        esp_err_t e = esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y1, buf);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "selftest draw_bitmap y=%d: %s", y, esp_err_to_name(e));
            return;
        }
    }
}

static void display_selftest(void)
{
    static const struct { const char *name; uint16_t rgb565; } bars[] = {
        { "RED",   0xF800 },
        { "GREEN", 0x07E0 },
        { "BLUE",  0x001F },
        { "WHITE", 0xFFFF },
        { "BLACK", 0x0000 },
    };
    const size_t buf_px = (size_t)LCD_H_RES * LCD_BUF_LINES;
    uint16_t *buf = heap_caps_malloc(buf_px * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (buf == NULL) {
        ESP_LOGE(TAG, "selftest: no DMA buffer");
        return;
    }
    ESP_LOGI(TAG, "selftest: drawing solid colour bars directly (no LVGL)");
    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); i++) {
        ESP_LOGI(TAG, "selftest: fill %-5s (0x%04X)", bars[i].name, bars[i].rgb565);
        fill_screen(buf, buf_px, bars[i].rgb565);
        vTaskDelay(pdMS_TO_TICKS(1000));   /* long enough for DMA + eyeballs */
    }
    ESP_LOGI(TAG, "selftest: done — if you saw R/G/B/W/K the panel + SPI work");
    free(buf);
}

/* --- bring-up -------------------------------------------------------------- */

lv_display_t *logger_display_start(void)
{
    ESP_LOGI(TAG, "LCD pins: CLK=%d MOSI=%d MISO=%d CS=%d DC=%d RST=%d BL=%d pclk=%dHz",
             BSP_LCD_SPI_CLK, BSP_LCD_SPI_MOSI, BSP_LCD_SPI_MISO, BSP_LCD_SPI_CS,
             BSP_LCD_DC, BSP_LCD_RST, BSP_LCD_BACKLIGHT, BSP_LCD_PIXEL_CLOCK_HZ);

    /* Backlight up front so the self-test is visible. */
    ESP_ERROR_CHECK(bsp_display_brightness_init());
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = BSP_LCD_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_BUF_LINES * (int)sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI%d bus initialised", BSP_LCD_SPI_NUM + 1);

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, &io));
    ESP_LOGI(TAG, "panel IO (SPI) installed: io=%p", (void *)io);

    /* Turn on the ILI9341 driver's own DEBUG logging (prints its init command
     * sequence) so we can see the panel actually being configured. */
    esp_log_level_set("ili9341", ESP_LOG_DEBUG);

    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,  /* ILI9341 panel is wired BGR */
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_config, &panel));
    s_panel = panel;
    ESP_LOGI(TAG, "ILI9341 panel created: panel=%p", (void *)panel);

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ESP_LOGI(TAG, "panel reset/init/on complete");

#if LOGGER_DISPLAY_SELFTEST
    display_selftest();
#endif

    /* LVGL port (owns the flush task + the lock bsp_display_lock() wraps). */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LCD_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = { .swap_xy = false, .mirror_x = true, .mirror_y = false },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,   /* RGB565 big-endian over SPI */
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_LOGI(TAG, "LVGL display added: disp=%p (handing panel to LVGL)", (void *)disp);

    return disp;
}
