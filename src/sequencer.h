//  sequencer.h
#ifndef __SEQUENCERH
#define __SEQUENCERH

#include "amy.h"
#define MIDI_SEQUENCER_PPQ 24  // MIDI clocks per quarter note
uint32_t sequencer_ticks();
void sequencer_init(int max_num_sequences, uint32_t max_sequence_events,
                    uint32_t max_sequence_executions);
void sequencer_deinit();
void sequencer_reset();
void sequencer_debug();

void sequencer_recompute();
void sequencer_check_and_fill();  // called once per block from amy_execute_deltas()
#ifdef __EMSCRIPTEN__
void sequencer_check_and_call_js_hook();  // called from the browser main loop
#endif
// Store a wire message (with its leading 'H' already stripped) in the
// sequencer. If has_tag is true, append it to the reusable sequence identified
// by tag. An empty tick=period=0 command clears that sequence; the same timing
// with a payload appends a local tick-zero event. If has_tag is false, store it
// anonymously (round-robin in a small reserved pool) for immediate sequencer
// playback. Takes ownership of wire.
uint8_t sequencer_add_wire(uint32_t tick, uint32_t period, uint32_t tag, bool has_tag, char *wire);
// Append one ordinary ticks event to the reusable sequence identified by tag.
// Takes ownership of wire. A tick=period=0 event is a valid one-shot when its
// wire payload is nonempty.
uint8_t sequencer_sequence_add_wire(uint32_t tag, uint32_t tick,
                                    uint32_t period, char *wire);
// Clear the future definition at tag. Executions which already started retain
// their immutable definition and may finish.
uint8_t sequencer_sequence_reset(uint32_t tag);
// sequence_control is [tag, start_or_stop, alignment_period] or
// [tag, gate, duration, alignment_period].
uint8_t sequencer_sequence_control(uint32_t tag, uint32_t action,
                                   uint32_t value,
                                   uint32_t alignment_period);
void sequencer_sequence_reset_timebase();
void sequencer_midi_clock_tick();
void sequencer_midi_start();
void sequencer_midi_stop();
void sequencer_external_clock_disable();  // drop external-clock mode, resume internal clock

#endif
