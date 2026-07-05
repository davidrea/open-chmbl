# Host-side golden test for the DE-08 decoder core (no ESP-IDF).
# Build:  cmake -B build && cmake --build build
# Run:    tools/golden_check.py (drives trc_replay and compares to cantools)
cmake_minimum_required(VERSION 3.16)
project(can_decode_host_test C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)

add_executable(trc_replay
    trc_replay.c
    ../main/can_decode.c
    ../main/bike_profile_triumph_tr.c)
target_include_directories(trc_replay PRIVATE ../main)
