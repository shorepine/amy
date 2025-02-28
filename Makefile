# Makefile for libamy , including an example

TARGET = amy-example amy-message
LIBS = -lpthread  -lm 


# on macOS, need to link to AudioUnit, CoreAudio, and CoreFoundation
ifeq ($(shell uname -s), Darwin)
LIBS += -framework AudioUnit -framework CoreAudio -framework CoreFoundation

# Needed for brew's python3.12+ on MacOS
EXTRA_PIP_ENV = PIP_BREAK_SYSTEM_PACKAGES=1 
endif

# on Raspberry Pi, at least under 32-bit mode, libatomic and libdl are needed.
ifeq ($(shell uname -m), armv7l)
LIBS += -ldl  -latomic
endif

ifeq ($(shell uname -m), armv6l)
LIBS += -ldl  -latomic
endif

CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing -Wextra -Wno-unused-parameter -Wpointer-arith -Wno-float-conversion -Wno-missing-declarations
CFLAGS += -DAMY_DEBUG
# -Wdouble-promotion
EMSCRIPTEN_OPTIONS = -s WASM=1 \
-DMA_ENABLE_AUDIO_WORKLETS -sAUDIO_WORKLET=1 -sWASM_WORKERS=1 -sASYNCIFY -sASSERTIONS \
-s ASYNCIFY_STACK_SIZE=128000 \
-s INITIAL_MEMORY=256mb \
-s TOTAL_STACK=128mb \
-s ALLOW_MEMORY_GROWTH=1 \
-sMODULARIZE -s 'EXPORT_NAME="amyModule"' \
-s EXPORTED_RUNTIME_METHODS="['cwrap','ccall']" \
-s EXPORTED_FUNCTIONS="['_amy_play_message', '_amy_reset_sysclock', '_amy_sysclock', '_amy_live_stop', '_amy_live_start', '_amy_start', '_sequencer_ticks', '_malloc', '_free']"

PYTHON = python3

.PHONY: default all clean amy-module test

default: $(TARGET)
all: default

SOURCES = src/algorithms.c src/amy.c src/envelope.c src/examples.c \
	src/filters.c src/oscillators.c src/pcm.c src/partials.c src/interp_partials.c src/custom.c \
	src/delay.c src/log2_exp2.c src/patches.c src/transfer.c src/sequencer.c \
	src/libminiaudio-audio.c

OBJECTS = $(patsubst %.c, %.o, $(SOURCES)) 
 
HEADERS = $(wildcard src/*.h) src/amy_config.h
HEADERS_BUILD := $(filter-out src/patches.h,$(HEADERS))

PYTHONS = $(wildcard *.py)

src/patches.h: $(PYTHONS) $(HEADERS_BUILD)
	cat src/amy{,_config}.h  | sed -e 's@^//.*@@' | egrep 'define +[^ ]+ +[.0-9-]+' | sed -e 's/\([.0-9]\)f$$/\1/' | awk '{print $$2 "=" $$3}' > amy_constants.py
	${PYTHON} amy_headers.py

%.o: %.c $(HEADERS) src/patches.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.mm $(HEADERS)
	clang $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

amy-example: $(OBJECTS) src/amy-example.o
	$(CC) $(OBJECTS) src/amy-example.o -Wall $(LIBS) -o $@

amy-message: $(OBJECTS) src/amy-message.o
	$(CC) $(OBJECTS) src/amy-message.o -Wall $(LIBS) -o $@

amy-module: amy-example
	${EXTRA_PIP_ENV} ${PYTHON} -m pip install -r requirements.txt; touch src/amy.c; cd src; ${EXTRA_PIP_ENV} ${PYTHON} -m pip install . --force-reinstall; cd ..

test: amy-module
	${PYTHON} test.py

# Report the median FILTER_PROCESS timing over 50 runs.
timing: amy-module
	for a in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40; do ${PYTHON} timing.py 2>&1 ; done > /tmp/timings.txt
	cat /tmp/timings.txt | grep AMY_RENDER: | sed -e 's/us//' | sort -n | awk ' { a[i++]=$$4; } END { print a[int(i/2)]; }'
	cat /tmp/timings.txt | grep FILTER_PROCESS: | sed -e 's/us//' | sort -n | awk ' { a[i++]=$$4; } END { print a[int(i/2)]; }'
	cat /tmp/timings.txt | grep PARAMETRIC_EQ_PROCESS: | sed -e 's/us//' | sort -n | awk ' { a[i++]=$$4; } END { print a[int(i/2)]; }'

docs/amy.js: $(TARGET)
	 emcc $(SOURCES) $(EMSCRIPTEN_OPTIONS) -O3 -o $@

docs/amy-audioin.js: $(TARGET)
	 emcc $(SOURCES) $(EMSCRIPTEN_OPTIONS) -O3 -o $@

clean:
	-rm -f src/*.o
	-rm -r src/patches.h
	-rm -f amy_constants.py
	-rm -f $(TARGET)
