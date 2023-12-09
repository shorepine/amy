# Makefile for libamy , including an example

TARGET = amy-example amy-message
LIBS = -lpthread  -lm 


# on macOS, need to link to AudioUnit, CoreAudio, and CoreFoundation
ifeq ($(shell uname -s), Darwin)
LIBS += -framework AudioUnit -framework CoreAudio -framework CoreFoundation
endif

# on Raspberry Pi, at least under 32-bit mode, libatomic and libdl are needed.
ifeq ($(shell uname -m), armv7l)
LIBS += -ldl  -latomic
endif

CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing -Wextra -Wno-unused-parameter -Wpointer-arith -Wfloat-conversion
# -Wdouble-promotion
EMSCRIPTEN_OPTIONS = -s WASM=1 \
-s ALLOW_MEMORY_GROWTH=1 \
-s EXPORTED_FUNCTIONS="['_web_audio_buffer', '_amy_play_message', '_amy_AMY_SAMPLE_RATE', '_amy_start_web', '_malloc', '_free']" \
-s EXPORTED_RUNTIME_METHODS="['cwrap','ccall']"

.PHONY: default all clean

default: $(TARGET)
all: default

SOURCES = src/algorithms.c src/amy.c src/envelope.c src/examples.c \
	src/filters.c src/oscillators.c src/pcm.c src/partials.c src/delay.c 

OBJECTS = $(patsubst %.c, %.o, src/algorithms.c src/amy.c src/envelope.c src/delay.c src/partials.c \
	src/examples.c src/filters.c src/oscillators.c src/pcm.c src/libminiaudio-audio.c src/amy-example-esp32.c) 

HEADERS = $(wildcard src/*.h) src/amy_config.h

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.mm $(HEADERS)
	clang $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

amy-example: $(OBJECTS) src/amy-example.o
	$(CC) $(OBJECTS) src/amy-example.o -Wall $(LIBS) -o $@

amy-message: $(OBJECTS) src/amy-message.o
	$(CC) $(OBJECTS) src/amy-message.o -Wall $(LIBS) -o $@

web: $(TARGET)
	 emcc $(SOURCES) $(EMSCRIPTEN_OPTIONS) -O3 -o src/www/amy.js

clean:
	-rm -f src/*.o
	-rm -f $(TARGET)
