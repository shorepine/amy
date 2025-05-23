//  sequencer.h
#ifndef __SEQUENCERH
#define __SEQUENCERH

#include "amy.h"

// A pointer to a sequence entry and the sequence metadata, for passing to the callback
typedef struct sequence_callback_info_t {
    struct sequence_entry_ll_t ** pointer;
    uint32_t tag;
    uint32_t tick; // 0 means not used 
    uint32_t period; // 0 means not used 
} sequence_callback_info_t;    

uint32_t sequencer_ticks();
void sequencer_init();
void sequencer_recompute();
uint8_t sequencer_add_event(amy_event *e);
void sequencer_reset();
extern void (*amy_external_sequencer_hook)(uint32_t);

#endif
