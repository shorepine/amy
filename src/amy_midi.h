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


#define MAX_MIDI_BYTES_TO_PARSE 1024
#define MAX_MIDI_BYTES_PER_MESSAGE 3
#define MIDI_QUEUE_DEPTH 1024
#define MAX_SYSEX_BYTES (16384)
extern uint8_t *sysex_buffer;
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
#ifdef MACOS
void *run_midi_macos(void*vargp);
#endif

void check_tusb_midi();
void init_tusb_midi();

#endif // __MIDI_H
