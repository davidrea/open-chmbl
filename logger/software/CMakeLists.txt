# Top-level project CMakeLists for the CAN data logger (ESP-WROVER-KIT) firmware.
# Standard ESP-IDF project layout — see https://docs.espressif.com/projects/esp-idf
cmake_minimum_required(VERSION 3.16)

# Trim the build to the app and its dependency closure. By default ESP-IDF
# COMPILES every discoverable component whether or not it is used — the REQUIRES
# graph only governs linking, not what gets compiled. Setting COMPONENTS to
# `main` expands to main's declared deps (the esp_wrover_kit BSP — which pulls in
# esp_lcd, LVGL, esp_lvgl_port and iot_button — plus twai/fatfs/sdmmc) and the
# always-on common components, and nothing else.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(logger)
