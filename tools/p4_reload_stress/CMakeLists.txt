# ESP-IDF project: patch-reload stress test for the ESP32-P4 (#1185, #1190).
#
# Builds AMY straight from source, like tulipcc's AMYboard build. AMY_DIR
# picks which AMY tree goes in, so one checkout of this tool can bench main
# and each candidate fix:
#   idf.py -DAMY_DIR=/path/to/amy-worktree build
# It defaults to the tree this directory sits in.
cmake_minimum_required(VERSION 3.16)

if(NOT AMY_DIR)
    get_filename_component(AMY_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
endif()
if(NOT STRESS_MODE)
    set(STRESS_MODE 0)
endif()
set(AMY_DIR ${AMY_DIR} CACHE PATH "AMY source tree to build against")
set(STRESS_MODE ${STRESS_MODE} CACHE STRING "0 = #1185 protocol, 1 = back-to-back stress")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(p4_reload_stress)
