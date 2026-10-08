# Declare deps explicitly so `main` does NOT implicitly pull in (and compile)
# every IDF component. We need GPIO (brake-bar driver enables + status LEDs),
# the console/REPL, esp_hw_support (chip info + base MAC), and the ESP-NOW link
# (DE-01): esp_wifi (Wi-Fi + esp_now), esp_netif/esp_event (its init
# boilerplate), nvs_flash (peer persistence), and esp_timer (render/link
# timestamps). `console` transitively pulls the USB Serial/JTAG and UART
# console drivers it needs.
#
# Note there is no LEDC/PWM dependency: the brake bar is a binary GPIO output
# and brightness/dimming is deferred (docs/design/de-04-led-render.md).
# render_core.c is pure C and is also compiled by the host test harness in
# ../test_host/.
idf_component_register(SRCS "main.c"
                            "console.c"
                            "cmd_system.c"
                            "render_core.c"
                            "render.c"
                            "cmd_render.c"
                            "cmd_light.c"
                            "status.c"
                            "cmd_ind.c"
                            "pairing.c"
                            "cmd_pair.c"
                            "net.c"
                            "link.c"
                            "cmd_link.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio console esp_hw_support
                                     esp_wifi esp_netif esp_event nvs_flash esp_timer)
