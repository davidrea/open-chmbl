/*
 * Developer console (DE-00) — REPL bootstrap.
 *
 * Builds an esp_console REPL on the active console transport and starts it in
 * its own task. On the ESP32-C3 the default transport is the built-in USB
 * Serial/JTAG controller, which enumerates as a virtual COM port over the
 * native USB pins (no external USB-TTL adapter) while still exposing JTAG for
 * debugging on the same cable. UART is kept as a compile-time fallback for
 * boards wired that way.
 *
 * See ../../../docs/cli.md for the command grammar and the source-override model.
 */
#include "esp_console.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "console.h"

static const char *TAG = "console";

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "chmbl>";
    repl_config.max_cmdline_length = 256;

    /* `help` plus our own domains. Hardware/link bring-up already happened
     * in app_main() (light_init(), pairing_init(), net_init(), link_init())
     * so it works without the CLI too; this just wires up the commands that
     * fake/inspect it. Registration is independent of the transport, so new
     * commands just add a cmd_*_register() call here. */
    ESP_ERROR_CHECK(esp_console_register_help_command());
    cmd_system_register();
    cmd_light_register();
    cmd_pair_register();
    cmd_link_register();

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
    ESP_LOGI(TAG, "console on USB Serial/JTAG (virtual COM port)");
#elif defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_LOGI(TAG, "console on UART");
#else
#error "No supported console transport selected (set ESP_CONSOLE_USB_SERIAL_JTAG or a UART console)"
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "developer CLI ready — type 'help'");
}
