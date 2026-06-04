// midi.h

#ifndef __MIDI_H
#define __MIDI_H

#ifdef ESP_PLATFORM 
#include "driver/uart.h"
#include "soc/uart_reg.h"
#include "esp_task.h"
#else
// virtualmidi Cocoa stubs
#endif
#define MIDI_SLOTS 4

void convert_midi_bytes_to_messages(uint8_t * data, size_t len, uint8_t usb);
void amy_process_single_midi_byte(uint8_t byte, uint8_t from_web_or_usb);
void amy_external_midi_output(uint8_t * data, uint32_t len);
void amy_external_midi_sync(uint8_t enabled);


#define MAX_MIDI_BYTES_TO_PARSE 1024
#define MAX_MIDI_BYTES_PER_MESSAGE 3
#define MIDI_QUEUE_DEPTH 1024
#define MAX_SYSEX_BYTES (16384)
extern uint8_t *sysex_buffer;
// Every platform allocates the single sysex_buffer above. The sysex copy-slot
// ring buffer below holds backup snapshots so a fast-arriving message isn't lost
// while a previous one waits for the deferred MicroPython mp_sched callback.
// Only the MicroPython-hosted boards (AMYBOARD, TULIP) create and read these
// slots (see parse_sysex()); other platforms handle sysex synchronously from
// sysex_buffer. Size the ring to 0 elsewhere so we don't malloc
// SYSEX_COPY_SLOTS x MAX_SYSEX_BYTES (32 x 16KB = 512KB), which alone would
// exhaust e.g. the rp2350's 520KB of SRAM.
#if defined(AMYBOARD) || defined(TULIP)
#define SYSEX_COPY_SLOTS 32
#else
#define SYSEX_COPY_SLOTS 0
#endif
// The alloc/free loops use SYSEX_COPY_SLOTS directly (0 => no slots off-board),
// but the array dimension must be >= 1: MSVC (the Godot Windows build includes
// this header transitively via amy.h) rejects a zero-length array with C2466,
// unlike the GCC/Clang extension. Floor the dimension at 1; that lone slot is
// never touched off-board because the loops iterate 0 times.
#define SYSEX_COPY_SLOTS_DIM ((SYSEX_COPY_SLOTS) > 0 ? (SYSEX_COPY_SLOTS) : 1)
extern char *sysex_message_copies[SYSEX_COPY_SLOTS_DIM];
extern uint8_t sysex_copy_write_idx;
extern uint8_t sysex_copy_read_idx;
extern uint16_t sysex_len;
extern void parse_sysex();
extern uint8_t last_midi[MIDI_QUEUE_DEPTH][MAX_MIDI_BYTES_PER_MESSAGE];
extern uint8_t last_midi_len[MIDI_QUEUE_DEPTH];
extern int16_t midi_queue_tail;
extern int16_t midi_queue_head;

void midi_out(uint8_t * bytes, uint16_t len);
void midi_local(uint8_t * bytes, uint16_t len);
void amy_send_midi_note_off(uint16_t osc);
void amy_send_midi_note_on(uint16_t osc);
// For pyamy inject_midi
void amy_event_midi_message_received(uint8_t * data, uint32_t len, uint8_t sysex, uint32_t time);

#ifdef ESP_PLATFORM
#define MIDI_TASK_COREID (0)
#define MIDI_TASK_STACK_SIZE (8 * 1024)
#define MIDI_TASK_NAME      "amy_midi_task"
#define MIDI_TASK_PRIORITY (ESP_TASK_PRIO_MAX - 2)
#endif

void run_midi();
void stop_midi();
#ifdef MACOS
void *run_midi_macos(void*vargp);
#endif

void check_tusb_midi();
void init_tusb_midi();

#endif // __MIDI_H
