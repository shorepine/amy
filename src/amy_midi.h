// midi.h

#ifndef __MIDI_H
#define __MIDI_H

// AMY_HOST_MIDI: the embedding host owns the MIDI device layer (run_midi /
// stop_midi / midi_out) and amy_midi.c compiles only the platform-neutral
// message parsing. MACOS implies it — the macOS desktop host layer is
// macos_midi.m. Other hosts (e.g. the AMYboard VCV Rack plugin, which
// provides its own MIDI on every platform) define AMY_HOST_MIDI directly.
#if defined(MACOS) && !defined(AMY_HOST_MIDI)
#define AMY_HOST_MIDI 1
#endif

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

// Modes for amy_external_midi_sync() (wire command zC / external_midi_sync=).
#define AMY_MIDI_SYNC_OFF 0     // internal clock; ignore and don't send realtime messages (default)
#define AMY_MIDI_SYNC_FOLLOW 1  // sequencer follows incoming 0xF8/0xFA/0xFC
#define AMY_MIDI_SYNC_SEND 2    // sequencer sends 0xF8/0xFA/0xFC (AMY is the clock master)
void amy_external_midi_sync(uint8_t mode);

// MIDI clock out, driven by the sequencer when in AMY_MIDI_SYNC_SEND mode.
// Stubbed in godot/src/amy_platform_stubs.c (amy_midi.c is excluded there).
uint8_t midi_clock_out_enabled();
void midi_clock_out_tick();
void midi_clock_out_start();
void midi_clock_out_stop();
#ifdef __EMSCRIPTEN__
void midi_clock_out_flush();  // called from the browser main loop
#endif

// Track which midi channels not to ignore
void midi_active_channels_reset(void);
void midi_active_channel_set(uint8_t channel, bool state);
void midi_active_channels_debug(void);


#define MAX_MIDI_BYTES_TO_PARSE 1024
#define MAX_MIDI_BYTES_PER_MESSAGE 3
#define MIDI_QUEUE_DEPTH 1024
#define MAX_SYSEX_BYTES (16384)
extern uint8_t *sysex_buffer;
// Every platform allocates the single sysex_buffer above. The sysex copy-slot
// ring buffer below holds backup snapshots so a fast-arriving message isn't lost
// while a previous one waits for the deferred MicroPython mp_sched callback.
// Only AMYBOARD creates and reads these slots (see parse_sysex()): the ring
// exists for the SPSS z* sketch-transfer bursts over USB-gadget MIDI, which
// only AMYboard receives. Size the ring to 0 everywhere else so we don't
// malloc SYSEX_COPY_SLOTS x MAX_SYSEX_BYTES: that alone would exhaust e.g.
// the rp2350's 520KB of SRAM, and on Tulip ESP32-S3 it ate a third of the
// free SPIRAM. With 0 slots, parse_sysex() finds a NULL slot and the
// scheduled callback ACKs SPSS messages without processing them; non-SPSS
// sysex is unaffected.
// 8 slots (128KB SPIRAM) is enough on AMYboard: the sender is flow-controlled
// (the ACK is sent only after a slot is drained, see parse_sysex()), so it
// can never run more than the ring depth ahead. 32 slots cost 512KB of
// SPIRAM better spent on memorypcm samples.
#if defined(AMYBOARD)
#define SYSEX_COPY_SLOTS 8
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
