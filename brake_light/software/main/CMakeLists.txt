# Declare deps explicitly so `main` does NOT implicitly pull in (and compile)
# every IDF component. A blink only needs the GPIO driver; everything else
# (wifi, lwip, esp_netif, mbedtls, bt, ...) stays out of the build.
idf_component_register(SRCS "blink.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio)
