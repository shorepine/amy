// examples.c
// sound examples

#include "amy.h"

// set by the arch
extern void delay_ms(uint32_t ms);

void example_reset(uint32_t start) {
    struct event e = amy_default_event();
    e.osc = 0;
    e.reset_osc = RESET_ALL_OSCS;
    e.time = start;
    amy_add_event(e);
}

void example_voice_alloc() {
    // alloc 2 juno voices, then try to alloc a dx7 voice on voice 0
    struct event e = amy_default_event();
    e.load_patch = 1;
    strcpy(e.voices, "0,1");
    amy_add_event(e);
    delay_ms(250);

    e = amy_default_event();
    e.load_patch = 131;
    strcpy(e.voices, "0");
    amy_add_event(e);
    delay_ms(250);

    // play the same note on both
    e = amy_default_event();
    e.velocity = 1;
    e.midi_note = 60;
    strcpy(e.voices,"0");
    amy_add_event(e);
    delay_ms(2000);

    e = amy_default_event();
    e.velocity = 1;
    e.midi_note = 60;
    strcpy(e.voices,"1");
    amy_add_event(e);
    delay_ms(2000);


    // now try to alloc voice 0 with a juno, should use oscs 0-4 again
    e = amy_default_event();
    e.load_patch = 2;
    strcpy(e.voices, "0");
    amy_add_event(e);
    delay_ms(250);
}


void example_voice_chord(uint32_t start, uint16_t patch) {
    struct event e = amy_default_event();
    e.time = start;
    e.load_patch = patch;
    strcpy(e.voices, "0,1,2");
    amy_add_event(e);
    start += 250;

    e = amy_default_event();
    e.time = start;
    e.velocity=0.5;

    strcpy(e.voices, "0");
    e.midi_note = 50;
    amy_add_event(e);
    start += 1000;

    strcpy(e.voices, "1");
    e.midi_note = 54;
    e.time = start;
    amy_add_event(e);
    start += 1000;

    strcpy(e.voices, "2");
    e.midi_note = 56;
    e.time = start;
    amy_add_event(e);
    start += 2000;
    
    strcpy(e.voices, "0,1,2");
    e.velocity = 0;
    e.time = start;
    amy_add_event(e);
}   


void example_patches() {
    struct event e = amy_default_event();
    for(uint16_t i=0;i<256;i++) {
        e.load_patch = i;
        strcpy(e.voices, "0");
        fprintf(stderr, "sending patch %d\n", i);
        amy_add_event(e);
        delay_ms(250);

        e = amy_default_event();
        strcpy(e.voices, "0");
        e.osc = 0;
        e.midi_note = 50;
        e.velocity = 0.5;
        amy_add_event(e);

        delay_ms(1000);
        strcpy(e.voices, "0");
        e.velocity = 0;
        amy_add_event(e);

        delay_ms(250);

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
        config_chorus(0.8, CHORUS_DEFAULT_MAX_DELAY, CHORUS_DEFAULT_LFO_FREQ, CHORUS_DEFAULT_MOD_DEPTH);
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
    e.osc = 0;
    e.time = start;
    e.wave = SINE;
    e.freq_coefs[COEF_CONST] = 220;
    e.pan_coefs[COEF_CONST] = 0.9;
    e.velocity = 1;
    amy_add_event(e);
    e.time = start + 150;
    e.freq_coefs[COEF_CONST] = 440;
    e.pan_coefs[COEF_CONST] = 0.1;
    amy_add_event(e);
    e.time = start + 300;
    e.velocity = 0;
    e.pan_coefs[COEF_CONST] = 0.5;  // Restore default pan to osc 0.
    amy_add_event(e);
}


void example_multimbral_fm() {
    struct event e = amy_default_event();
    char *voices[] = {"0","1","2","3","4","5"};
    int notes[] = {60, 70, 64, 68, 72, 82};
    e.velocity = 0.5;
    e.load_patch = 128;
    for (unsigned int i = 0; i < sizeof(notes) / sizeof(int); ++i) {
        e.midi_note = notes[i];
        e.pan_coefs[0] = (i%2);
        e.load_patch++;
        strcpy(e.voices, voices[i]);
        amy_add_event(e);
        delay_ms(1000);
    }
}


// Emulate the Tulip "drums()" example via event calls.
void example_drums(uint32_t start, int loops) {

    // bd, snare, hat, cow, hicow
    int oscs[] = {0, 1, 2, 3, 4};
    int patches[] = {1, 5, 0, 10, 10};

    // Reset all used oscs first, just in case
    for (unsigned int i = 0; i < sizeof(oscs) / sizeof(int); ++i) {
        struct event e_reset = amy_default_event();
        e_reset.time = start;
        e_reset.osc = oscs[i];    
        e_reset.reset_osc = oscs[i];
        amy_add_event(e_reset);
    }

    struct event e = amy_default_event();
    e.time = start + 1;
    float volume = 0.5;
    e.wave = PCM;
    e.velocity = 0;

    for (unsigned int i = 0; i < sizeof(oscs) / sizeof(int); ++i) {
        e.osc = oscs[i];
        e.patch = patches[i];
        amy_add_event(e);
    }
    // Update high cowbell.
    e = amy_default_event();
    e.time = start+1;
    e.osc = 4;
    e.midi_note = 70;
    amy_add_event(e);

    // osc 5 : bass
    e = amy_default_event();
    e.time = start+1;
    e.osc = 5;
    e.wave = SAW_DOWN;
    e.filter_freq_coefs[COEF_CONST] = 650.0;  // LOWEST filter center frequency.
    e.filter_freq_coefs[COEF_EG0] = 2.0;  // When env0 is 1.0, filter is shifted up by 2.0 octaves (x4, so 2600.0).
    e.resonance = 5.0;
    e.filter_type = FILTER_LPF;
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
    e.time = start+1;
    while (loops--) {
        for (unsigned int i = 0; i < sizeof(pattern) / sizeof(int); ++i) {
            e.time += 250;
            AMY_UNSET(e.freq_coefs[0]);
            AMY_UNSET(e.midi_note);
            
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
        }
    }
}

void example_fm(uint32_t start) {
    // Direct construction of an FM tone, as in the documentation.
    struct event e;

    // Modulating oscillator (op 2)
    e = amy_default_event();
    e.time = start;
    e.osc = 9;
    e.wave = SINE;
    e.ratio = 1.0f;
    e.amp_coefs[COEF_CONST] = 1.0f;
    e.amp_coefs[COEF_VEL] = 0;
    e.amp_coefs[COEF_EG0] = 0;
    amy_add_event(e);

    // Output oscillator (op 1)
    e = amy_default_event();
    e.time = start;
    e.osc = 8;
    e.wave = SINE;
    e.ratio = 0.2f;
    e.amp_coefs[COEF_CONST] = 1.0f;
    e.amp_coefs[COEF_VEL] = 0;
    e.amp_coefs[COEF_EG0] = 1.0f;
    strcpy(e.bp0, "0,1,1000,0,0,0");
    amy_add_event(e);

    // ALGO control oscillator
    e = amy_default_event();
    e.time = start;
    e.osc = 7;
    e.wave = ALGO;
    e.algorithm = 1;  // algo 1 has op 2 driving op 1 driving output (plus a second chain for ops 6,5,4,3).
    strcpy(e.algo_source, ",,,,9,8");
    amy_add_event(e);

    // Add a note on event.
    e = amy_default_event();
    e.time = start + 100;
    e.osc = 7;
    e.midi_note = 60;
    e.velocity = 1.0f;
    amy_add_event(e);
}


// Minimal custom oscillator

#if AMY_HAS_CUSTOM == 1

void beeper_init(void) {
    //printf("Beeper init\n");
}

void beeper_note_on(uint16_t osc, float freq) {
    saw_down_note_on(osc, freq);
}

void beeper_note_off(uint16_t osc) {
    synth[osc].note_off_clock = total_samples;
}

void beeper_mod_trigger(uint16_t osc) {
    saw_down_mod_trigger(osc);
}

SAMPLE beeper_render(SAMPLE* buf, uint16_t osc) {
    return render_saw_down(buf, osc);
}

SAMPLE beeper_compute_mod(uint16_t osc) {
    return compute_mod_saw_down(osc);
}

struct custom_oscillator beeper = {
    beeper_init,
    beeper_note_on,
    beeper_note_off,
    beeper_mod_trigger,
    beeper_render,
    beeper_compute_mod
};

void example_init_custom() {
    amy_set_custom(&beeper);
}

void example_custom_beep() {
    struct event e = amy_default_event();
    e.osc = 50;
    e.time = amy_sysclock();
    e.freq_coefs[0] = 880;
    e.wave = CUSTOM;
    e.velocity = 1;
    amy_add_event(e);

    e.velocity = 0;
    e.time += 500;
    amy_add_event(e);
}

#endif
