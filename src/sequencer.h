//  sequencer.h
#ifndef __SEQUENCERH
#define __SEQUENCERH

#include "amy.h"

// Our internal accounting
extern uint32_t sequencer_tick_count ;
extern uint64_t next_amy_tick_us ;
extern uint32_t us_per_tick ;


void sequencer_init();
void sequencer_recompute();
void parse_tick_and_tag(char * message, uint32_t *tick, uint16_t *divider, uint32_t *tag);
uint8_t sequencer_add_event(struct event e, uint32_t tick, uint16_t divider, uint32_t tag);
extern void (*amy_external_sequencer_hook)(uint32_t);

#define AMY_SEQUENCER_PPQ 48

#endif
