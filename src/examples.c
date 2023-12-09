// examples.c
// sound examples

#include "amy.h"


void example_reverb() {
    #if AMY_HAS_REVERB == 1
    config_reverb(2, REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING, REVERB_DEFAULT_XOVER_HZ); 
    #endif
}

void example_chorus() {
    #if AMY_HAS_CHORUS == 1
    config_chorus(0.8, CHORUS_DEFAULT_MAX_DELAY);
    #endif
}

// Play a KS tone
void example_ks(int64_t start) {
    struct event e = amy_default_event();
    e.time = start;

    e.velocity = 1;
    e.wave = KS;
    e.feedback = 0.996f;
    e.patch = 15;
    e.osc = 0;
    e.midi_note = 60;
    amy_add_event(e);
}

void example_fm(int64_t start) {
    // Play a few notes in FM
    struct event e = amy_default_event();
    e.time = start;
    e.velocity = 1;
    e.wave = ALGO;
    e.patch = 15;
    e.midi_note = 60;
    amy_add_event(e);

    e.time = start + 500;
    e.osc += 9; // remember that an FM patch takes up 9 oscillators
    e.midi_note = 64;
    amy_add_event(e);
    
    e.time = start + 1000;
    e.osc += 9;
    e.midi_note = 68;
    amy_add_event(e);
}

void example_multimbral_fm(int64_t start) {
    struct event e = amy_default_event();
    int osc_inc;
    e.time = start;
    e.osc = 0;
    e.wave = ALGO;
    e.patch = 20;
    osc_inc = 9;
    int notes[] = {60, 70, 64, 68, 72, 82, 76, 80, 74, 78, 80, 58};
    e.velocity = 0.2;

    for (unsigned int i = 0; i < sizeof(notes) / sizeof(int); ++i) {
        e.midi_note = notes[i];
        e.pan = 0.5 + 0.5 * ((2 * (i %2)) - 1);
        e.patch++;
        amy_add_event(e);
        e.osc += osc_inc;
        e.time += 1000;
    }
}


// Emulate the Tulip "drums()" example via event calls.
void example_drums(int64_t start, int loops) {
    //int64_t start = amy_sysclock();
    struct event e = amy_default_event();
    e.time = start;

    float volume = 0.2;

    int oscs[] = {0, 2, 3, 4, 5, 6};
    int patches[] = {1, 5, 0, 10, 10, 5};
    e.wave = PCM;
    e.freq = 0;
    e.velocity = 0;
    for (unsigned int i = 0; i < sizeof(oscs) / sizeof(int); ++i) {
        e.osc = oscs[i];
        e.patch = patches[i];
        amy_add_event(e);
    }
    // Update high cowbell.
    e = amy_default_event();
    e.time = start;
    e.osc = 5;
    e.midi_note = 70;
    amy_add_event(e);

    // osc 7 : bass
    e = amy_default_event();
    e.time = start;
    e.osc = 7;
    e.wave = SAW_DOWN;
    e.filter_freq = 2500.0;
    e.resonance = 5.0;
    e.filter_type = FILTER_LPF;
    e.bp0_target = TARGET_AMP + TARGET_FILTER_FREQ;
    strcpy(e.bp0, "0,1,500,0.2,25,0");
    amy_add_event(e);


    const int bass = 1 << 0;
    const int snare = 1 << 1;
    const int hat = 1 << 2;
    const int cow = 1 << 3;
    const int hicow = 1 << 4;

    int pattern[] = {bass+hat, hat+hicow, bass+hat+snare, hat+cow, hat, hat+bass, snare+hat, hat};
    int bassline[] = {50, 0, 0, 0, 50, 52, 51, 0};

    e = amy_default_event();
    e.time = start;
    while (loops--) {
        for (unsigned int i = 0; i < sizeof(pattern) / sizeof(int); ++i) {
            e.time += 250;

            int x = pattern[i];
            if(x & bass) {
                e.osc = 0;
                e.velocity = 4.0 * volume;
                amy_add_event(e);
            }
            if(x & snare) {
                e.osc = 2;
                e.velocity = 1.5 * volume;
                amy_add_event(e);
            }
            if(x & hat) {
                e.osc = 3;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }
            if(x & cow) {
                e.osc = 4;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }
            if(x & hicow) {
                e.osc = 5;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }

            e.osc = 7;
            if(bassline[i]>0) {
                e.velocity = 0.5 * volume;
                e.midi_note = bassline[i] - 12;
            } else {
                e.velocity = 0;
            }
            amy_add_event(e);
            e.midi_note = -1;
        }
    }
}
