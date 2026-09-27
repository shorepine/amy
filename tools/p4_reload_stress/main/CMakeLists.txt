# AMY's portable sources plus its ESP platform layer (i2s.c). Left out:
# libminiaudio-audio.c (desktop audio), examples.c (needs a delay_ms and
# nothing here calls it), usb.c (AMYboard's USB gadget).
set(AMY_SRC
    ${AMY_DIR}/src/algorithms.c
    ${AMY_DIR}/src/amy.c
    ${AMY_DIR}/src/amy_midi.c
    ${AMY_DIR}/src/api.c
    ${AMY_DIR}/src/custom.c
    ${AMY_DIR}/src/cv_trigger.c
    ${AMY_DIR}/src/delay.c
    ${AMY_DIR}/src/envelope.c
    ${AMY_DIR}/src/filters.c
    ${AMY_DIR}/src/i2s.c
    ${AMY_DIR}/src/instrument.c
    ${AMY_DIR}/src/interp_partials.c
    ${AMY_DIR}/src/log2_exp2.c
    ${AMY_DIR}/src/midi_mappings.c
    ${AMY_DIR}/src/note_output.c
    ${AMY_DIR}/src/oscillators.c
    ${AMY_DIR}/src/parse.c
    ${AMY_DIR}/src/patches.c
    ${AMY_DIR}/src/pcm.c
    ${AMY_DIR}/src/sequencer.c
    ${AMY_DIR}/src/transfer.c
)

idf_component_register(
    SRCS reload_stress.c ${AMY_SRC}
    INCLUDE_DIRS . ${AMY_DIR}/src
    REQUIRES esp_driver_i2s esp_driver_uart esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    AMY_WAVETABLE
    STRESS_MODE=${STRESS_MODE}
)
# Quiet the warnings AMY's own Makefile turns off, and don't let IDF's
# -Werror=all stop a test build over a warning in AMY. (Optimisation comes
# from CONFIG_COMPILER_OPTIMIZATION_PERF in sdkconfig.defaults.)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-parameter -Wno-missing-field-initializers
    -Wno-strict-aliasing -Wno-unused-variable -Wno-unused-function
)
message(STATUS "reload_stress: AMY_DIR=${AMY_DIR} STRESS_MODE=${STRESS_MODE}")
