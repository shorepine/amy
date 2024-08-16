# Project Name
TARGET = Osc
AMY = /Users/bwhitman/outside/amy/src
APP_TYPE=BOOT_SRAM

# Sources
CPP_SOURCES = Osc.cpp
C_SOURCES = ${AMY}/amy.c ${AMY}/oscillators.c ${AMY}/algorithms.c ${AMY}/envelope.c ${AMY}/examples.c ${AMY}/filters.c ${AMY}/pcm.c ${AMY}/partials.c ${AMY}/custom.c ${AMY}/delay.c ${AMY}/log2_exp2.c ${AMY}/patches.c


C_INCLUDES += -I${AMY} -DAMY_DAISY -Wno-strict-aliasing -Wextra -Wno-unused-parameter -Wpointer-arith -Wno-float-conversion -Wno-missing-declarations

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

