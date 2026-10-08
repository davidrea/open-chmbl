# ESP-IDF drivers we use (TWAI, timers, FAT + SDMMC, GPIO) plus the two shared
# repo-root components: chmbl_can (DE-08 profile decode) and brake_fsm (DE-09
# state machine). `main` stays lean — nothing else is pulled into the build (see
# the COMPONENTS trim in the top-level CMakeLists.txt).
#
# No managed components: the espressif/button (iot_button) dependency is gone
# with the pushbutton, since recording is now gated automatically on the decoded
# kill switch and nothing else needed debounced button input.
idf_component_register(SRCS "logger_main.c"
                            "trc_format.c"
                            "ui_log.c"
                            "can_tap.c"
                            "fsm_preview.c"
                            "fault_led.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES driver esp_timer fatfs sdmmc esp_driver_sdmmc
                                     chmbl_can brake_fsm)
