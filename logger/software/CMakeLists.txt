# Top-level project CMakeLists for the CAN data logger + DE-09 brake-preview
# firmware (custom logger PCB, ESP32-S3-WROOM-1-N4).
# Standard ESP-IDF project layout — see https://docs.espressif.com/projects/esp-idf
cmake_minimum_required(VERSION 3.16)

# Shared, ESP-IDF-free cores live in the repo-root components/ directory, so the
# logger and the transmitter compile one copy of the CAN decoder (chmbl_can) and
# the braking state machine (brake_fsm) rather than duplicating them. Must be
# set before project.cmake is included. See components/README.md.
get_filename_component(CHMBL_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../components"
                       ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${CHMBL_COMPONENTS}")

# Trim the build to the app and its dependency closure. By default ESP-IDF
# COMPILES every discoverable component whether or not it is used — the REQUIRES
# graph only governs linking, not what gets compiled. Setting COMPONENTS to
# `main` expands to main's declared deps (chmbl_can, brake_fsm, and the
# twai/fatfs/sdmmc/gpio drivers) and the always-on common components, and
# nothing else.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(logger)
