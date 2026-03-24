cmake_minimum_required(VERSION 3.10)
project(amy_sine C)

set(CMAKE_C_STANDARD 11)

set(AMY_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

set(AMY_SOURCES
    ${AMY_SRC_DIR}/algorithms.c
    ${AMY_SRC_DIR}/amy.c
    ${AMY_SRC_DIR}/envelope.c
    ${AMY_SRC_DIR}/examples.c
    ${AMY_SRC_DIR}/parse.c
    ${AMY_SRC_DIR}/filters.c
    ${AMY_SRC_DIR}/oscillators.c
    ${AMY_SRC_DIR}/pcm.c
    ${AMY_SRC_DIR}/interp_partials.c
    ${AMY_SRC_DIR}/custom.c
    ${AMY_SRC_DIR}/delay.c
    ${AMY_SRC_DIR}/log2_exp2.c
    ${AMY_SRC_DIR}/patches.c
    ${AMY_SRC_DIR}/transfer.c
    ${AMY_SRC_DIR}/sequencer.c
    ${AMY_SRC_DIR}/libminiaudio-audio.c
    ${AMY_SRC_DIR}/instrument.c
    ${AMY_SRC_DIR}/amy_midi.c
    ${AMY_SRC_DIR}/api.c
    ${AMY_SRC_DIR}/midi_mappings.c
)

add_executable(amy_sine amy_sine.c ${AMY_SOURCES})

target_include_directories(amy_sine PRIVATE ${AMY_SRC_DIR})

target_compile_definitions(amy_sine PRIVATE AMY_WAVETABLE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(amy_sine PRIVATE /O2 /W3 /std:c11)
else()
    target_compile_options(amy_sine PRIVATE
        -O3 -Wall -Wno-strict-aliasing -Wextra -Wno-unused-parameter
        -Wpointer-arith -Wno-float-conversion -Wno-missing-declarations
    )
    target_link_libraries(amy_sine PRIVATE m pthread)
endif()
