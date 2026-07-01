#include "display_init.h"

#include "bsp/esp_wrover_kit.h"   /* pin macros + bsp_display_brightness_init/backlight_on */
#include "driver/spi_master.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "display";

/* Native ILI9341 geometry on the WROVER-KIT (portrait). */
#define LCD_H_RES       240
#define LCD_V_RES       320
#define LCD_BUF_LINES   30      /* draw-buffer height; matches the BSP's sizing */

lv_display_t *logger_display_start(void)
{
    /* Backlight LEDC (GPIO5) — set up here since we bypass bsp_display_new(). */
    ESP_ERROR_CHECK(bsp_display_brightness_init());

    /* SPI bus for the LCD (SPI2 / pins from the BSP). */
    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = BSP_LCD_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_BUF_LINES * (int)sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO));

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

    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,  /* ILI9341 panel is wired BGR */
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_config, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

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

    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_LOGI(TAG, "ILI9341 LCD up (%dx%d)", LCD_H_RES, LCD_V_RES);
    return disp;
}
