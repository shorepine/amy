//  sequencer.h
#ifndef __SEQUENCERH
#define __SEQUENCERH

#include "amy.h"
#define MIDI_SEQUENCER_PPQ 24  // MIDI clocks per quarter note
uint32_t sequencer_ticks();
void sequencer_init(int max_num_sequences, uint32_t max_groups,
                    uint32_t max_group_tags, uint32_t max_group_executions);
void sequencer_deinit();
void sequencer_reset();
void sequencer_debug();

void sequencer_recompute();
void sequencer_check_and_fill();  // called once per block from amy_execute_deltas()
#ifdef __EMSCRIPTEN__
void sequencer_check_and_call_js_hook();  // called from the browser main loop
#endif
// Store a wire message (with its leading 'H' already stripped) in the
// sequencer.  If has_tag is true, it's stored under tag (replacing/clearing
// any existing entry there, addressable later by that same tag); clears the
// tag if tick and period are both 0. If has_tag is false, it's stored
// anonymously (round-robin in a small reserved pool) and can't be addressed
// or cancelled by any tag. Takes ownership of wire.
uint8_t sequencer_add_wire(uint32_t tick, uint32_t period, uint32_t tag, bool has_tag, char *wire);
// Store one ordinary ticks event in a group's unpublished revision. Takes
// ownership of wire. Group zero is reserved for sequencer_add_wire().
uint8_t sequencer_group_add_wire(uint32_t tick, uint32_t period,
                                 uint32_t tag, uint32_t group, char *wire);

// sequence_control actions. The wire/API representation is always
// [group, action, value, quantize, optional execution_tag].
uint8_t sequencer_group_control(uint32_t group, uint32_t action,
                                uint32_t value, uint32_t quantize,
                                uint32_t execution_tag,
                                bool has_execution_tag);
void sequencer_group_reset_timebase();
void sequencer_midi_clock_tick();
void sequencer_midi_start();
void sequencer_midi_stop();
void sequencer_external_clock_disable();  // drop external-clock mode, resume internal clock

#endif
