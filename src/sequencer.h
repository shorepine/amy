//  sequencer.h
#ifndef __SEQUENCERH
#define __SEQUENCERH

#include "amy.h"
#define MIDI_SEQUENCER_PPQ 24  // MIDI clocks per quarter note

uint32_t sequencer_ticks();
void sequencer_init(int max_num_sequences, uint32_t max_patterns,
                    uint32_t max_pattern_tags,
                    uint32_t max_pattern_instances);
void sequencer_deinit();
void sequencer_reset();
void sequencer_debug();

void sequencer_recompute();
// Rebase pattern-instance origins when the shared tick counter is reset.
// Caller holds the AMY lock; root sequence entries deliberately retain their
// existing RESET_TIMEBASE semantics.
void sequencer_rebase_patterns(uint32_t old_tick);
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

// Immutable, two-level nested sequences.  Build into a staging definition,
// commit atomically, then trigger the committed definition in one-shot or
// loop mode.  Pattern events are ordinary AMY wire events and cannot trigger
// another pattern.  Existing instances retain the committed version they
// started with, so replacing/clearing a definition never truncates playback.
uint8_t amy_pattern_begin(uint32_t pattern, uint32_t length_ticks);
uint8_t amy_pattern_add_wire(uint32_t pattern, uint32_t tick,
                             uint32_t period, uint32_t tag, bool has_tag,
                             const char *wire);
uint8_t amy_pattern_add_event(uint32_t pattern, const amy_event *event);
uint8_t amy_pattern_commit(uint32_t pattern);
uint8_t amy_pattern_clear(uint32_t pattern);
uint8_t amy_pattern_trigger(uint32_t pattern, uint8_t mode,
                            uint32_t quantize_ticks, uint32_t instance_tag);
uint8_t amy_pattern_stop(uint32_t instance_tag, uint32_t quantize_ticks);
// Temporarily suppress onsets from every running instance with this tag.  The
// instance keeps advancing, and resumes at its original phase after duration.
uint8_t amy_pattern_mute(uint32_t instance_tag, uint32_t duration_ticks);
// Store a root-sequencer event which triggers a pattern relative to the next
// quantized boundary.  A nonzero period repeats that trigger without making
// the child pattern itself loop through the intervening silence.
uint8_t amy_pattern_schedule(uint32_t pattern, uint8_t mode,
                             uint32_t offset_ticks, uint32_t period_ticks,
                             uint32_t quantize_ticks, uint32_t sequence_tag,
                             uint32_t instance_tag);

// Wire entry points. J<pattern>,<tick>[,<period>[,<tag>]]<event> stores one
// event in the staging definition. zQ handles lifecycle, scheduling and mute.
void handle_pattern_ticks_message(char *message);
uint16_t amy_parse_pattern_control_message(char *message);
void sequencer_midi_clock_tick();
void sequencer_midi_start();
void sequencer_midi_stop();
void sequencer_external_clock_disable();  // drop external-clock mode, resume internal clock

#endif
