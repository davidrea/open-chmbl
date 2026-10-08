# Host-side test for the DE-04 render core (no ESP-IDF). Proves the
# state->binary-output map and the anti-strobe dwell floor without hardware.
# Build:  cmake -B build && cmake --build build
# Run:    ./build/render_core_test   (exit 0 = pass)
cmake_minimum_required(VERSION 3.16)
project(brake_light_render_host_test C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)

add_executable(render_core_test
    render_core_test.c
    ../main/render_core.c)
target_include_directories(render_core_test PRIVATE ../main)

enable_testing()
add_test(NAME render_core COMMAND render_core_test)
