# Host-side tests for the firmware's pure cores (no ESP-IDF):
#   trc_replay     DE-08 decode  -> tools/golden_check.py (compares to cantools)
#   fsm_replay     DE-09 FSM     -> tools/fsm_check.py    (compares to trc_viz.py)
#   log_gate_test  ride-logging gate (kill switch) — self-checking, run directly
# Build:  cmake -B build && cmake --build build
cmake_minimum_required(VERSION 3.16)
project(can_decode_host_test C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)

add_executable(trc_replay
    trc_replay.c
    ../main/can_decode.c
    ../main/bike_profile_triumph_tr.c)
target_include_directories(trc_replay PRIVATE ../main)
target_link_libraries(trc_replay m)

add_executable(fsm_replay
    fsm_replay.c
    ../main/can_decode.c
    ../main/brake_fsm.c
    ../main/bike_profile_triumph_tr.c)
target_include_directories(fsm_replay PRIVATE ../main)
target_link_libraries(fsm_replay m)

add_executable(log_gate_test
    log_gate_test.c
    ../main/log_gate.c)
target_include_directories(log_gate_test PRIVATE ../main)
