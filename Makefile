# Makefile for libamy , including an example

TARGET = amy-example amy-message
LIBS = -lpthread  -lm

# on Raspberry Pi, at least under 32-bit mode, libatomic and libdl are needed.
ifeq ($(shell uname -m), armv7l)
LIBS += -ldl  -latomic
endif

CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing 
EMSCRIPTEN_OPTIONS = -s WASM=1 \
-s ALLOW_MEMORY_GROWTH=1 \
-s EXPORTED_FUNCTIONS="['_web_audio_buffer', '_amy_play_message', '_amy_sample_rate', '_amy_start_web', '_malloc', '_free']" \
-s EXPORTED_RUNTIME_METHODS="['cwrap','ccall']"

.PHONY: default all clean

default: $(TARGET)
all: default

SOURCES = src/algorithms.c src/amy.c src/envelope.c \
	src/filters.c src/oscillators.c src/pcm.c src/partials.c
OBJECTS = $(patsubst %.c, %.o, src/algorithms.c src/amy.c src/envelope.c \
	src/filters.c src/oscillators.c src/pcm.c src/partials.c src/libminiaudio-audio.c src/amy-example-esp32.c)
HEADERS = $(wildcard src/*.h)

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
