# The esp_wrover_kit BSP (display + microSD + button) is added as a requirement
# automatically from idf_component.yml; declare the ESP-IDF drivers we use on top
# of it. `main` stays lean — nothing else is pulled into the build (see the
# COMPONENTS trim in the top-level CMakeLists.txt).
idf_component_register(SRCS "logger_main.c"
                            "trc_format.c"
                            "ui_log.c"
                            "display_init.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES driver esp_timer fatfs sdmmc
                                     esp_lcd esp_driver_spi)
