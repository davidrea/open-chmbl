# Top-level project CMakeLists for the transmitter (bike-side) firmware.
# Standard ESP-IDF project layout — see https://docs.espressif.com/projects/esp-idf
cmake_minimum_required(VERSION 3.16)

# Shared, ESP-IDF-free cores live in the repo-root components/ directory so the
# transmitter and the logger compile one copy rather than duplicating them (see
# components/README.md). Must be set before project.cmake is included.
get_filename_component(CHMBL_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../components"
                       ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${CHMBL_COMPONENTS}")

# Trim the build to the app and its dependency closure. By default ESP-IDF
# COMPILES every discoverable component (wifi, lwip, mqtt, spiffs, ...) whether
# or not it is used — the REQUIRES graph only governs linking, not what gets
# compiled. Setting COMPONENTS to `main` expands to main's declared deps
# (esp_driver_gpio, chmbl_can) plus the always-on common components, and
# nothing else.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(transmitter)
