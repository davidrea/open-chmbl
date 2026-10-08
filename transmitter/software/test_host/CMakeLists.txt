# Host-side tests for the shared, ESP-IDF-free firmware cores.
# Build:  cmake -B build && cmake --build build
# Run:    tools/golden_check.py   (trc_replay vs. cantools    — DE-08)
#         tools/fsm_check.py      (fsm_replay vs. trc_viz.py  — DE-09)
#
# The sources under ../../../components/ are the exact files the firmware
# compiles; nothing is copied or re-implemented here (components/README.md).
cmake_minimum_required(VERSION 3.16)
project(chmbl_host_tests C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)

set(CHMBL_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../../components")

# DE-08: decode golden test against cantools.
add_executable(trc_replay
    trc_replay.c
    ${CHMBL_COMPONENTS}/chmbl_can/can_decode.c
    ${CHMBL_COMPONENTS}/chmbl_can/bike_profile_triumph_tr.c)
target_include_directories(trc_replay PRIVATE ${CHMBL_COMPONENTS}/chmbl_can)
target_link_libraries(trc_replay PRIVATE m)

# DE-09: replay a capture through the real decode + the real FSM and report
# transitions / on-time, for comparison against the tuned Python reference.
add_executable(fsm_replay
    fsm_replay.c
    ${CHMBL_COMPONENTS}/chmbl_can/can_decode.c
    ${CHMBL_COMPONENTS}/chmbl_can/bike_profile_triumph_tr.c
    ${CHMBL_COMPONENTS}/brake_fsm/brake_fsm.c)
target_include_directories(fsm_replay PRIVATE
    ${CHMBL_COMPONENTS}/chmbl_can
    ${CHMBL_COMPONENTS}/brake_fsm)
target_link_libraries(fsm_replay PRIVATE m)
