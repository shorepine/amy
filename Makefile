# Makefile for AMY , including an example

TARGET = amy-example amy-message amy-piano
LIBS =  -lm  -pthread

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	SOURCES = src/macos_midi.m
	CFLAGS += -DMACOS
	LIBS = -framework AudioUnit -framework CoreAudio -framework CoreFoundation -framework CoreMIDI -framework Cocoa -lstdc++ 
	# Needed for brew's python3.12+ on MacOS
	EXTRA_PIP_ENV = PIP_BREAK_SYSTEM_PACKAGES=1 
	CC = clang
else
	CC = gcc
endif


# on Raspberry Pi, at least under 32-bit mode, libatomic and libdl are needed.
ifeq ($(shell uname -m), armv7l)
LIBS += -ldl  -latomic
endif

ifeq ($(shell uname -m), armv6l)
LIBS += -ldl  -latomic
endif

CFLAGS += -O3 -Wall -Wno-strict-aliasing -Wextra -Wno-unused-parameter -Wpointer-arith -Wno-float-conversion -Wno-missing-declarations 
#CFLAGS += -DAMY_DEBUG
# -Wdouble-promotion
EMSCRIPTEN_OPTIONS = -s WASM=1 --bind \
-DMA_ENABLE_AUDIO_WORKLETS -sAUDIO_WORKLET=1 -sWASM_WORKERS=1  \
-sSTACK_SIZE=128000\
-sMODULARIZE -s 'EXPORT_NAME="amyModule"' \
-s EXPORTED_RUNTIME_METHODS="['cwrap','ccall']" \
-s EXPORTED_FUNCTIONS="['_amy_add_message', '_amy_add_event', '_amy_reset_sysclock', '_amy_sysclock', '_amy_process_single_midi_byte', '_amy_live_stop', '_amy_get_input_buffer', '_amy_set_external_input_buffer', '_amy_live_start_web', '_amy_live_start_web_audioin', '_amy_start_web', '_amy_start_web_no_synths', '_sequencer_ticks', '_malloc', '_free', '_amy_bleep']" \
-s SUPPORT_LONGJMP=emscripten \
-s INITIAL_MEMORY=128mb -s TOTAL_STACK=64mb -s ALLOW_MEMORY_GROWTH=1  \
-s ASYNCIFY -s ASYNCIFY_STACK_SIZE=128000 

PYTHON = python3

.PHONY: default all clean amy-module test

default: $(TARGET)
all: default

SOURCES += src/algorithms.c src/amy.c src/envelope.c src/examples.c src/parse.c \
	src/filters.c src/oscillators.c src/pcm.c src/interp_partials.c src/custom.c \
	src/delay.c src/log2_exp2.c src/patches.c src/transfer.c src/sequencer.c \
	src/libminiaudio-audio.c src/instrument.c src/amy_midi.c src/api.c src/midi_mappings.c

OBJECTS = $(patsubst %.c, %.o, $(SOURCES)) 
 
HEADERS = $(wildcard src/*.h)
HEADERS_BUILD := $(filter-out src/patches.h,$(HEADERS))

PYTHONS = $(wildcard *.py)

src/patches.h: $(PYTHONS) $(HEADERS_BUILD)
	cat src/amy.h  | sed -e 's@^//.*@@' | egrep 'define +[^ ]+ +[.0-9-]+' | sed -e 's/\([.0-9]\)f$$/\1/' | awk '{print $$2 "=" $$3}' > amy/constants.py
	${PYTHON} -m amy.headers

%.o: %.c $(HEADERS) src/patches.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.mm $(HEADERS)
	clang $(CFLAGS) -c $< -o $@

%.o: %.m
	clang  -I$(INC) $(CFLAGS) -c -o $@ $<

.PRECIOUS: $(TARGET) $(OBJECTS)

amy-example: $(OBJECTS) src/amy-example.o
	$(CC) $(CFLAGS) $(OBJECTS) src/amy-example.o -Wall $(LIBS) -o $@


amy-piano: $(OBJECTS) src/amy-piano.o
	$(CC) $(CFLAGS) $(OBJECTS) src/amy-piano.o -Wall $(LIBS) -o $@

amy-message: $(OBJECTS) src/amy-message.o
	$(CC) $(CFLAGS) $(OBJECTS) src/amy-message.o -Wall $(LIBS) -o $@

amy-module: amy-example
	${EXTRA_PIP_ENV} ${PYTHON} -m pip install -r requirements.txt; touch src/amy.c; ${EXTRA_PIP_ENV} ${PYTHON} -m pip install . --force-reinstall; cd ..

test: amy-module
	${PYTHON} -m amy.test

# Report the median FILTER_PROCESS timing over 50 runs.
timing: amy-module
	for a in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40; do ${PYTHON} -m amy.timing 2>&1 ; done > /tmp/timings.txt
	cat /tmp/timings.txt | grep AMY_RENDER: | sed -e 's/us//' | sort -n | awk ' { a[i++]=$$4; } END { print a[int(i/2)]; }'
	cat /tmp/timings.txt | grep FILTER_PROCESS: | sed -e 's/us//' | sort -n | awk ' { a[i++]=$$4; } END { print a[int(i/2)]; }'
	cat /tmp/timings.txt | grep PARAMETRIC_EQ_PROCESS: | sed -e 's/us//' | sort -n | awk ' { a[i++]=$$4; } END { print a[int(i/2)]; }'

docs/amy.js: $(TARGET)
	 emcc $(SOURCES) src/amy_bindings.cpp $(CFLAGS) $(EMSCRIPTEN_OPTIONS) -O3 -o $@

clean:
	-rm -f src/*.o
	-rm -r src/patches.h
	-rm -f amy/constants.py
	-rm -f $(TARGET)
