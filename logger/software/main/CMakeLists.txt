# The debounced-button component is added from idf_component.yml; declare the
# ESP-IDF drivers we use (TWAI, timers, FAT + SDMMC, GPIO) here. `main` stays
# lean — nothing else is pulled into the build (see the COMPONENTS trim in the
# top-level CMakeLists.txt).
idf_component_register(SRCS "logger_main.c"
                            "trc_format.c"
                            "ui_log.c"
                            "status_led.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES driver esp_timer fatfs sdmmc esp_driver_sdmmc)
