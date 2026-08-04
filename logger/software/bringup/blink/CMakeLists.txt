# Bring-up scratch app: BRINGUP.md §4.1 status-LED check on the logger PCB.
# Standalone from ../../ (the logger app) so the esp32s3 port isn't a
# prerequisite for getting a hardware answer on D5/D6.
cmake_minimum_required(VERSION 3.16)

set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bringup_blink)
