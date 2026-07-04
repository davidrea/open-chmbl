# Declare deps explicitly so `main` does NOT implicitly pull in (and compile)
# every IDF component. We need GPIO (stand-in indicator), the console/REPL,
# esp_hw_support (chip info + base MAC), and now the ESP-NOW link (DE-01):
# esp_wifi (Wi-Fi + esp_now), esp_netif/esp_event (its init boilerplate), and
# nvs_flash (peer persistence). `console` transitively pulls the USB
# Serial/JTAG and UART console drivers it needs.
idf_component_register(SRCS "main.c"
                            "console.c"
                            "cmd_system.c"
                            "cmd_state.c"
                            "pairing.c"
                            "cmd_pair.c"
                            "net.c"
                            "cmd_net.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio console esp_hw_support
                                     esp_wifi esp_netif esp_event nvs_flash)
