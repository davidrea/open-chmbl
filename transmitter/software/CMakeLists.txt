# Top-level project CMakeLists for the transmitter (bike-side) firmware.
# Standard ESP-IDF project layout — see https://docs.espressif.com/projects/esp-idf
cmake_minimum_required(VERSION 3.16)

# Trim the build to the app and its dependency closure. By default ESP-IDF
# COMPILES every discoverable component (wifi, lwip, mqtt, spiffs, ...) whether
# or not it is used — the REQUIRES graph only governs linking, not what gets
# compiled. Setting COMPONENTS to `main` expands to main's declared deps
# (esp_driver_gpio) plus the always-on common components, and nothing else.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(transmitter)
