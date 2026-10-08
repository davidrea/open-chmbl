# Declare deps explicitly so `main` does NOT implicitly pull in (and compile)
# every IDF component. We need GPIO (stand-in indicator), the console/REPL,
# esp_hw_support (chip info + base MAC), and now the ESP-NOW link (DE-01):
# esp_wifi (Wi-Fi + esp_now), esp_netif/esp_event (its init boilerplate),
# nvs_flash (peer persistence), and now the CAN decode (DE-08): driver
# (legacy TWAI listen-only reception via driver/twai.h) and esp_timer (signal
# timestamps, sig ramp). NOTE: the standalone esp_driver_twai component only
# exists on IDF >= 5.5; on the v5.3 toolchain used in CI, driver/twai.h is
# provided by the umbrella `driver` component.
# `console` transitively pulls the USB Serial/JTAG and UART console drivers it
# needs.
#
# The decode core itself (can_decode.[ch], bike_profile*.h, the generated
# profile table) is no longer compiled here: it moved to the shared
# components/chmbl_can component, which the logger also uses and which the host
# golden test compiles with plain gcc. `PRIV_REQUIRES chmbl_can` both links it
# and puts its headers on the include path, so the #includes in can_rx.c /
# cmd_can.c / cmd_sig.c are unchanged.
idf_component_register(SRCS "main.c"
                            "console.c"
                            "cmd_system.c"
                            "cmd_state.c"
                            "pairing.c"
                            "cmd_pair.c"
                            "net.c"
                            "cmd_net.c"
                            "can_rx.c"
                            "cmd_can.c"
                            "cmd_sig.c"
                       INCLUDE_DIRS "."
                       # chmbl_can is a PUBLIC requirement, not a private one:
                       # main's own public header can_rx.h includes can_decode.h,
                       # so anything that includes can_rx.h needs chmbl_can's
                       # include path too.
                       REQUIRES chmbl_can
                       PRIV_REQUIRES esp_driver_gpio console esp_hw_support
                                     driver esp_timer
                                     esp_wifi esp_netif esp_event nvs_flash)
