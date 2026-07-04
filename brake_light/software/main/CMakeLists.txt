# Declare deps explicitly so `main` does NOT implicitly pull in (and compile)
# every IDF component. We need GPIO (stand-in light), the console/REPL, and
# esp_hw_support (chip info + base MAC); everything else (wifi, lwip, bt, ...)
# stays out of the build. `console` transitively pulls the USB Serial/JTAG and
# UART console drivers it needs.
idf_component_register(SRCS "main.c"
                            "console.c"
                            "cmd_system.c"
                            "cmd_light.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio console esp_hw_support)
