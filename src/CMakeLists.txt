cmake_minimum_required(VERSION 3.13)

# Pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

# We also need PICO EXTRAS
include(pico_extras_import.cmake)

project(amy_example C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# Initialize the Pico SDK
pico_sdk_init()


project(amy_example)

if (TARGET tinyusb_device)

    add_executable(amy_example
            amy-example-rp2040.c
            algorithms.c
            amy.c
            delay.c
            envelope.c
            filters.c
            patches.c
            log2_exp2.c
            examples.c
            oscillators.c
            partials.c
            pcm.c
    )

    # pull in common dependencies
    target_link_libraries(amy_example
        pico_stdlib
        pico_audio_i2s
        pico_multicore
        hardware_dma
        hardware_pio
        hardware_irq
        )

    target_compile_definitions(amy_example PRIVATE
            PICO_DEFAULT_FLOAT_IMPL=pico
            # compile time configuration of I2S
            #PICO_AUDIO_I2S_MONO_INPUT=0
            #define for our example code
            USE_AUDIO_I2S=1
#DIN
            PICO_AUDIO_I2S_DATA_PIN=8
#LRC is BCK+1 always
            PICO_AUDIO_I2S_CLOCK_PIN_BASE=9
    )

    # enable usb output, disable uart output
    pico_enable_stdio_usb(amy_example 1)
    pico_enable_stdio_uart(amy_example 0)

    # create map/bin/hex/uf2 file etc.
    pico_add_extra_outputs(amy_example)
elseif(PICO_ON_DEVICE)
    message(WARNING "not building amy_example because TinyUSB submodule is not initialized in the SDK")
endif()


pico_set_float_implementation(amy_example pico)
