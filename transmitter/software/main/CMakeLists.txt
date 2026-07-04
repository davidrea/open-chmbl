# Declare deps explicitly so `main` does NOT implicitly pull in (and compile)
# every IDF component. We need GPIO (stand-in indicator), the console/REPL,
# esp_hw_support (chip info + base MAC), and now the ESP-NOW link (DE-01):
# esp_wifi (Wi-Fi + esp_now), esp_netif/esp_event (its init boilerplate),
# nvs_flash (peer persistence), and now the CAN decode (DE-08): esp_driver_twai
# (listen-only reception) and esp_timer (signal timestamps, sig ramp).
# `console` transitively pulls the USB Serial/JTAG and UART console drivers it
# needs. can_decode.c / bike_profile_triumph_tr.c are pure C, also compiled on
# the host by test_host/ for the golden test.
idf_component_register(SRCS "main.c"
                            "console.c"
                            "cmd_system.c"
                            "cmd_state.c"
                            "pairing.c"
                            "cmd_pair.c"
                            "net.c"
                            "cmd_net.c"
                            "bike_profile_triumph_tr.c"
                            "can_decode.c"
                            "can_rx.c"
                            "cmd_can.c"
                            "cmd_sig.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio console esp_hw_support
                                     esp_driver_twai esp_timer
                                     esp_wifi esp_netif esp_event nvs_flash)
