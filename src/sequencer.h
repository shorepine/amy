//  sequencer.h
#ifndef __SEQUENCERH
#define __SEQUENCERH

#include "amy.h"
#define MIDI_SEQUENCER_PPQ 24  // MIDI clocks per quarter note
uint32_t sequencer_ticks();
void sequencer_init(int max_num_sequences);
void sequencer_deinit();
void sequencer_reset();
void sequencer_debug();

void sequencer_recompute();
void sequencer_check_and_fill();  // called once per block from amy_execute_deltas()
#ifdef __EMSCRIPTEN__
void sequencer_check_and_call_js_hook();  // called from the browser main loop
#endif
// Store a wire message (with its t/H already stripped) in the sequencer under
// tag; clears the tag if tick and period are both 0.  Takes ownership of wire.
uint8_t sequencer_add_wire(uint32_t tick, uint32_t period, uint32_t tag, char *wire);
// Store a one-shot wire message to play at an absolute ms time.  Takes ownership of wire.
void timed_wire_add(uint32_t time, char *wire);
// Store a copy of an already-parsed event to play at an absolute ms time.
void timed_event_add(uint32_t time, amy_event *e);
// Store a raw (non-sysex) MIDI message to handle at an absolute ms time.
void timed_midi_add(uint32_t time, uint8_t *bytes, uint8_t len);
void timed_wire_check_and_fire();  // called once per block from amy_execute_deltas()
void timed_wire_reset();  // drop all pending timed messages
void sequencer_midi_clock_tick();
void sequencer_midi_start();
void sequencer_midi_stop();
void sequencer_external_clock_disable();  // drop external-clock mode, resume internal clock

#endif
