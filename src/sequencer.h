//  sequencer.h
#ifndef __SEQUENCERH
#define __SEQUENCERH

#include "amy.h"

uint32_t sequencer_ticks();
void sequencer_init(int max_num_sequences);
void sequencer_free();
void sequencer_reset();
void sequencer_debug();

void sequencer_start();

void sequencer_recompute();
uint8_t sequencer_add_event(amy_event *e);

#endif
