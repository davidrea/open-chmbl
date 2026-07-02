#include "lcd_console.h"

#include "sdkconfig.h"

#if CONFIG_LOGGER_LCD_ENABLE

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_LOGGER_LCD_CONTROLLER_ILI9341
#include "esp_lcd_ili9341.h"
#endif

#include "font8x8.h"

static const char *TAG = "lcd";

/* Native ST7789/ILI9341 panel geometry on the WROVER-KIT (portrait). */
#define LCD_H_RES 240
#define LCD_V_RES 320

/* ESP-WROVER-KIT v4.1 LCD pins (free per README.md hardware table: LCD uses
 * 5/18/19/21/22/23/25; microSD, PSRAM and console use the rest). */
#define LCD_SPI_HOST SPI2_HOST
#define LCD_PIN_CLK  19
#define LCD_PIN_MOSI 23
#define LCD_PIN_MISO 25
#define LCD_PIN_CS   22
#define LCD_PIN_DC   21
#define LCD_PIN_RST  18
#define LCD_PIN_BL   5   /* active-low backlight enable */

#define FONT_W 8
#define FONT_H 8
#define CONSOLE_COLS     (LCD_H_RES / FONT_W)
#define CONSOLE_ROWS     (LCD_V_RES / FONT_H)
#define CONSOLE_LINE_LEN (CONSOLE_COLS + 1)

static char              s_lines[CONSOLE_ROWS][CONSOLE_LINE_LEN];
static int                s_count;   /* number of populated lines */
static SemaphoreHandle_t  s_mutex;
static bool               s_ready;

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;

/* Best-effort panel ID readback (requires MISO wired, which it is on this
 * kit). Purely diagnostic -- logs whatever comes back, or a warning if the
 * transport doesn't support reads, so a mis-set LOGGER_LCD_CONTROLLER choice
 * shows up in the serial log instead of just a blank/garbled screen. */
static void lcd_read_id(void)
{
    uint8_t rddid[4] = {0};
    esp_err_t err = esp_lcd_panel_io_rx_param(s_io, 0x04, rddid, sizeof(rddid));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RDDID: %02X %02X %02X %02X (ST7789 ~= 85 85 52)",
                 rddid[0], rddid[1], rddid[2], rddid[3]);
    } else {
        ESP_LOGW(TAG, "RDDID readback failed: %s", esp_err_to_name(err));
    }

    uint8_t rdid4[4] = {0};
    err = esp_lcd_panel_io_rx_param(s_io, 0xD3, rdid4, sizeof(rdid4));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RDID4: %02X %02X %02X %02X (ILI9341 ~= 00 93 41)",
                 rdid4[0], rdid4[1], rdid4[2], rdid4[3]);
    }
}

#if CONFIG_LOGGER_LCD_SELFTEST
/* Fill the whole panel with one RGB565 colour, one FONT_H-row band at a time. */
static void lcd_fill(uint16_t *buf, size_t buf_px, uint16_t color)
{
    uint16_t v = __builtin_bswap16(color);   /* RGB565 big-endian on the wire */
    for (size_t i = 0; i < buf_px; i++) {
        buf[i] = v;
    }
    for (int y = 0; y < LCD_V_RES; y += FONT_H) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + FONT_H, buf);
    }
}

static void lcd_selftest(void)
{
    static const struct { const char *name; uint16_t rgb565; } bars[] = {
        { "RED",   0xF800 }, { "GREEN", 0x07E0 }, { "BLUE",  0x001F },
        { "WHITE", 0xFFFF }, { "BLACK", 0x0000 },
    };
    size_t buf_px = (size_t)LCD_H_RES * FONT_H;
    uint16_t *buf = heap_caps_malloc(buf_px * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (buf == NULL) {
        ESP_LOGE(TAG, "selftest: no DMA buffer");
        return;
    }
    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); i++) {
        ESP_LOGI(TAG, "selftest: fill %s", bars[i].name);
        lcd_fill(buf, buf_px, bars[i].rgb565);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
    free(buf);
}
#endif /* CONFIG_LOGGER_LCD_SELFTEST */

/* Render one FONT_H-row-tall text line into `buf` (white on black) and push
 * it to the panel at screen row `row`. */
static void lcd_draw_row(uint16_t *buf, int row, const char *text)
{
    memset(buf, 0, (size_t)LCD_H_RES * FONT_H * sizeof(uint16_t));

    size_t len = strlen(text);
    uint16_t on = __builtin_bswap16(0xFFFF);
    for (size_t col = 0; col < len && col < CONSOLE_COLS; col++) {
        unsigned char c = (unsigned char)text[col];
        if (c > 0x7F) {
            c = '?';
        }
        const uint8_t *glyph = font8x8_basic[c];
        for (int gy = 0; gy < FONT_H; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < FONT_W; gx++) {
                if (bits & (1u << gx)) {
                    buf[gy * LCD_H_RES + col * FONT_W + gx] = on;
                }
            }
        }
    }

    int y0 = row * FONT_H;
    esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_H_RES, y0 + FONT_H, buf);
}

/* Redraw every console row from the ring buffer (caller holds s_mutex). Log
 * lines are infrequent (boot/button/file events, never per-CAN-frame), so a
 * full-screen redraw per line keeps the renderer simple. */
static void lcd_console_redraw(void)
{
    uint16_t *buf = heap_caps_malloc((size_t)LCD_H_RES * FONT_H * sizeof(uint16_t),
                                      MALLOC_CAP_DMA);
    if (buf == NULL) {
        return;
    }
    for (int row = 0; row < CONSOLE_ROWS; row++) {
        lcd_draw_row(buf, row, row < s_count ? s_lines[row] : "");
    }
    free(buf);
}

void lcd_console_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    /* Backlight on first, independent of whether panel bring-up below
     * succeeds -- confirms the kit is powered and the pin is wired even if
     * everything after this fails. */
    const gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_PIN_BL, 0);   /* active-low: 0 = backlight on */

    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_CLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * FONT_H * (int)sizeof(uint16_t),
    };
    if (spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed; LCD console disabled");
        return;
    }

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = CONFIG_LOGGER_LCD_PCLK_MHZ * 1000000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config,
                                  &s_io) != ESP_OK) {
        ESP_LOGE(TAG, "panel IO init failed; LCD console disabled");
        return;
    }

    lcd_read_id();

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = CONFIG_LOGGER_LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR
                                                : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_err_t err;
#if CONFIG_LOGGER_LCD_CONTROLLER_ILI9341
    err = esp_lcd_new_panel_ili9341(s_io, &panel_config, &s_panel);
#else
    err = esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel create failed (%s); LCD console disabled",
                  esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    esp_lcd_panel_invert_color(s_panel, CONFIG_LOGGER_LCD_INVERT_COLOR);
    esp_lcd_panel_mirror(s_panel, CONFIG_LOGGER_LCD_MIRROR_X, false);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    s_ready = true;
    ESP_LOGI(TAG, "LCD console ready (%dx%d chars)", CONSOLE_COLS, CONSOLE_ROWS);

#if CONFIG_LOGGER_LCD_SELFTEST
    lcd_selftest();
#endif
    lcd_console_redraw();   /* clears the screen -- ring buffer starts empty */
}

void lcd_console_write(const char *line)
{
    if (!s_ready) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_count < CONSOLE_ROWS) {
        strncpy(s_lines[s_count++], line, CONSOLE_LINE_LEN - 1);
        s_lines[s_count - 1][CONSOLE_LINE_LEN - 1] = '\0';
    } else {
        /* Scroll: drop the oldest, append at the bottom. */
        memmove(s_lines[0], s_lines[1], (CONSOLE_ROWS - 1) * CONSOLE_LINE_LEN);
        strncpy(s_lines[CONSOLE_ROWS - 1], line, CONSOLE_LINE_LEN - 1);
        s_lines[CONSOLE_ROWS - 1][CONSOLE_LINE_LEN - 1] = '\0';
    }
    lcd_console_redraw();
    xSemaphoreGive(s_mutex);
}

#else /* !CONFIG_LOGGER_LCD_ENABLE */

void lcd_console_init(void)
{
}

void lcd_console_write(const char *line)
{
    (void)line;
}

#endif
