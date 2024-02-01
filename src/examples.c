// examples.c
// sound examples

#include "amy.h"

void delay_ms(uint32_t ms) {
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < ms) usleep(THREAD_USLEEP);
}

void example_patches() {
    struct event e = amy_default_event();
    for(uint16_t i=128;i<256;i++) {
        e.load_patch = i;
        e.osc = 0;
        amy_add_event(e);
        delay_ms(250);

        e = amy_default_event();
        e.osc = 0;
        e.midi_note = 50;
        e.velocity = 1;
        amy_add_event(e);
        delay_ms(1000);

        amy_reset_oscs();
    }
}
void example_reverb() {
    if(AMY_HAS_REVERB == 1) {
        config_reverb(2, REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING, REVERB_DEFAULT_XOVER_HZ); 
    }
}

void example_chorus() {
    if(AMY_HAS_CHORUS == 1) {
        config_chorus(0.8, CHORUS_DEFAULT_MAX_DELAY);
    }
}

// Play a KS tone
void example_ks(uint32_t start) {
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

// make a 440hz sine
void example_sine(uint32_t start) {
    struct event e = amy_default_event();
    e.time = start;
    e.freq_coefs[0] = 440;
    e.wave = SINE;
    e.velocity = 1;
    amy_add_event(e);
}

// Schedule a bleep now
void bleep(uint32_t start) {
    struct event e = amy_default_event();
    int64_t sysclock = amy_sysclock();
    e.osc = 0;
    e.time = start;
    e.wave = SINE;
    e.freq_coefs[COEF_CONST] = 220;
    amy_add_event(e);
    e.velocity = 1;
    e.pan_coefs[COEF_CONST] = 0.9;
    amy_add_event(e);
    e.time = sysclock + 150;
    e.freq_coefs[COEF_CONST] = 440;
    e.pan_coefs[COEF_CONST] = 0.1;
    amy_add_event(e);
    e.time = sysclock + 300;
    e.velocity = 0;
    e.pan_coefs[COEF_CONST] = 0.5;  // Restore default pan to osc 0.
    amy_add_event(e);
}

void example_fm(uint32_t start) {
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

void example_multimbral_fm(int64_t start, int start_osc) {
    struct event e = amy_default_event();
    int osc_inc;
    e.time = start;
    e.osc = start_osc;
    e.wave = ALGO;
    e.patch = 20;
    osc_inc = 9;
    int notes[] = {60, 70, 64, 68, 72, 82};
    e.velocity = 0.2;

    for (unsigned int i = 0; i < sizeof(notes) / sizeof(int); ++i) {
        e.midi_note = notes[i];
        e.pan_coefs[0] = (i%2);
        //e.pan = 0.5 + 0.5 * ((2 * (i %2)) - 1);
        e.patch++;
        amy_add_event(e);
        e.osc += osc_inc;
        e.time += 1000;
    }
}


// Emulate the Tulip "drums()" example via event calls.
void example_drums(uint32_t start, int loops) {
    struct event e = amy_default_event();
    e.time = start;

    float volume = 0.5;

    // bd, snare, hat, cow, hicow
    int oscs[] = {0, 1, 2, 3, 4};
    int patches[] = {1, 5, 0, 10, 10};
    e.wave = PCM;
    //e.freq = 0;
    e.velocity = 0;
    for (unsigned int i = 0; i < sizeof(oscs) / sizeof(int); ++i) {
        e.osc = oscs[i];
        e.patch = patches[i];
        amy_add_event(e);
    }
    // Update high cowbell.
    e = amy_default_event();
    e.time = start;
    e.osc = 4;
    e.midi_note = 70;
    amy_add_event(e);

    // osc 5 : bass
    e = amy_default_event();
    e.time = start;
    e.osc = 5;
    e.wave = SAW_DOWN;
    e.filter_freq_coefs[0] = 650.0;  // LOWEST filter center frequency.
    e.filter_freq_coefs[3] = 2.0;  // When env0 is 1.0, filter is shifted up by 2.0 octaves (x4, so 2600.0).
    e.resonance = 5.0;
    e.filter_type = FILTER_LPF;
    e.bp0_target = TARGET_AMP + TARGET_FILTER_FREQ;
    strcpy(e.bp0, "0,1,500,0.2,25,0");
    amy_add_event(e);


    const int bd = 1 << 0;
    const int snare = 1 << 1;
    const int hat = 1 << 2;
    const int cow = 1 << 3;
    const int hicow = 1 << 4;

    int pattern[] = {bd+hat, hat+hicow, bd+hat+snare, hat+cow, hat, bd+hat, snare+hat, hat};
    int bassline[] = {50, 0, 0, 0, 50, 52, 51, 0};

    e = amy_default_event();
    e.time = start;
    while (loops--) {
        for (unsigned int i = 0; i < sizeof(pattern) / sizeof(int); ++i) {
            e.time += 250;
            AMY_UNSET(e.freq_coefs[0]);
            
            int x = pattern[i];
            if(x & bd) {
                e.osc = 0;
                e.velocity = 4.0 * volume;
                amy_add_event(e);
            }
            if(x & snare) {
                e.osc = 1;
                e.velocity = 1.5 * volume;
                amy_add_event(e);
            }
            if(x & hat) {
                e.osc = 2;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }
            if(x & cow) {
                e.osc = 3;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }
            if(x & hicow) {
                e.osc = 4;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }

            e.osc = 5;
            if(bassline[i]>0) {
                e.velocity = 0.5 * volume;
                e.midi_note = bassline[i] - 12;
            } else {
                e.velocity = 0;
            }
            amy_add_event(e);
            AMY_UNSET(e.midi_note);
        }
    }
}
