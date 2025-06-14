// examples.h
// examples.c
// sound examples

#include "amy.h"


void example_reverb();
void example_chorus();
void example_ks(uint32_t start);
void bleep(uint32_t start);
void bleep_synth(uint32_t start);
void example_fm(uint32_t start);
void example_multimbral_fm();
void example_drums(uint32_t start, int loops);
void example_sequencer_drums(uint32_t start);
void example_sequencer_drums_synth(uint32_t start);
void example_sine(uint32_t start);
void example_init_custom();
void example_custom_beep();
void example_patches();
void example_voice_chord(uint32_t start, uint16_t patch_number);
void example_synth_chord(uint32_t start, uint16_t patch_number);
void example_sustain_pedal(uint32_t start, uint16_t patch_number);
void example_voice_alloc();
void example_reset(uint32_t start);
void example_patch_from_events();
