# Makefile for libamy , including an example

TARGET = amy-example
LIBS = -lpthread -lsoundio -lm 
CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing 
EMSCRIPTEN_OPTIONS = -s WASM=1 \
-s ALLOW_MEMORY_GROWTH=1 \
-s EXPORTED_FUNCTIONS="['_web_audio_buffer', '_amy_play_message', '_amy_start_web', '_malloc', '_free']" \
-s EXPORTED_RUNTIME_METHODS="['cwrap','ccall']"

.PHONY: default all clean

default: $(TARGET)
all: default

SOURCES = src/algorithms.c src/amy.c src/envelope.c \
	src/filters.c src/oscillators.c src/pcm.c src/partials.c
OBJECTS = $(patsubst %.c, %.o, src/amy-example.c src/algorithms.c src/amy.c src/envelope.c \
	src/filters.c src/oscillators.c src/pcm.c src/partials.c src/libsoundio-audio.c src/amy-example-esp32.c)
HEADERS = $(wildcard src/*.h)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LIBS += -L/opt/homebrew/lib -lstdc++ 
	CFLAGS += -I/opt/homebrew/include
endif

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.mm $(HEADERS)
	clang $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

web: $(TARGET)
	 emcc $(SOURCES) $(EMSCRIPTEN_OPTIONS) -O3 -o src/www/amy.js

clean:
	-rm -f src/*.o
	-rm -f $(TARGET)
