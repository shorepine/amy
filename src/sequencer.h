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
uint8_t sequencer_add_event(amy_event *e);
void sequencer_midi_clock_tick();
void sequencer_midi_start();
void sequencer_midi_stop();

#endif
