# Project Name
TARGET = amy_daisy
AMY = ${HOME}/github/shorepine/tulipcc/amy/src
APP_TYPE=BOOT_SRAM
OPT = -O3


# Sources
CPP_SOURCES = amy_daisy.cpp
C_SOURCES = ${AMY}/amy.c ${AMY}/oscillators.c ${AMY}/algorithms.c ${AMY}/envelope.c ${AMY}/examples.c ${AMY}/filters.c ${AMY}/pcm.c ${AMY}/custom.c ${AMY}/delay.c ${AMY}/log2_exp2.c ${AMY}/patches.c ${AMY}/api.c ${AMY}/sequencer.c ${AMY}/amy_midi.c ${AMY}/instrument.c ${AMY}/parse.c ${AMY}/transfer.c ${AMY}/interp_partials.c ${AMY}/midi_mappings.c


C_INCLUDES += -I${AMY} -DAMY_DAISY -Wno-strict-aliasing -Wextra -Wno-unused-parameter -Wpointer-arith -Wno-float-conversion -Wno-missing-declarations

# Library Locations
LIBDAISY_DIR = ${HOME}/Desktop/DaisyExamples/libDaisy
DAISYSP_DIR = ${HOME}/Desktop/DaisyExamples/DaisySP

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

