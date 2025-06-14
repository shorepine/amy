// examples.c
// sound examples

#include "amy.h"

// set by the arch
extern void delay_ms(uint32_t ms);

void example_reset(uint32_t start) {
    amy_event e = amy_default_event();
    e.osc = 0;
    e.reset_osc = RESET_ALL_OSCS;
    e.time = start;
    amy_add_event(&e);
}

void example_voice_alloc() {
    // alloc 2 juno voices, then try to alloc a dx7 voice on voice 0
    amy_event e = amy_default_event();
    e.patch_number = 1;
    e.voices[0] = 0;
    e.voices[1] = 1;
    amy_add_event(&e);
    delay_ms(250);

    e = amy_default_event();
    e.patch_number = 131;
    e.voices[0] = 0;
    amy_add_event(&e);
    delay_ms(250);

    // play the same note on both
    e = amy_default_event();
    e.velocity = 1;
    e.midi_note = 60;
    e.voices[0] = 0;
    amy_add_event(&e);
    delay_ms(2000);

    e = amy_default_event();
    e.velocity = 1;
    e.midi_note = 60;
    e.voices[0] = 1;
    amy_add_event(&e);
    delay_ms(2000);


    // now try to alloc voice 0 with a juno, should use oscs 0-4 again
    e = amy_default_event();
    e.patch_number = 2;
    e.voices[0] = 0;
    amy_add_event(&e);
    delay_ms(250);
}


void example_voice_chord(uint32_t start, uint16_t patch_number) {
    amy_event e = amy_default_event();
    e.time = start;
    e.patch_number = patch_number;
    e.voices[0] = 0;
    e.voices[1] = 1;
    e.voices[2] = 2;
    amy_add_event(&e);
    start += 250;

    e = amy_default_event();
    e.time = start;
    e.velocity=0.5;

    e.voices[0] = 0;
    e.midi_note = 50;
    amy_add_event(&e);
    start += 1000;

    e.voices[0] = 1;
    e.midi_note = 54;
    e.time = start;
    amy_add_event(&e);
    start += 1000;

    e.voices[0] = 2;
    e.midi_note = 56;
    e.time = start;
    amy_add_event(&e);
    start += 2000;

    e.voices[0] = 0;
    e.voices[1] = 1;
    e.voices[2] = 2;
    e.velocity = 0;
    e.time = start;
    amy_add_event(&e);
}

void example_synth_chord(uint32_t start, uint16_t patch_number) {
    // Like example_voice_chord, but use 'synth' to avoid having to keep track of voices.
    amy_event e = amy_default_event();
    e.time = start;
    e.patch_number = patch_number;
    e.num_voices = 3;
    e.synth = 0;
    amy_add_event(&e);
    start += 250;

    e = amy_default_event();
    e.velocity = 0.5;
    e.synth = 0;

    e.time = start;
    e.midi_note = 50;
    amy_add_event(&e);

    e.time += 1000;
    e.midi_note = 54;  // Will get a new voice.
    amy_add_event(&e);

    e.time += 1000;
    e.midi_note = 56;
    amy_add_event(&e);

    e.time += 2000;
    e.velocity = 0;
    // Voices are referenced only by their note, so have to turn them off individually.
    e.midi_note = 50;
    amy_add_event(&e);
    e.midi_note = 54;
    amy_add_event(&e);
    e.midi_note = 56;
    amy_add_event(&e);
}   


void example_sustain_pedal(uint32_t start, uint16_t patch_number) {
    // Reproduce TestSustainPedal, to track segfault.
    amy_event e = amy_default_event();
    e.time = start;
    e.reset_osc = RESET_ALL_OSCS;
    amy_add_event(&e);

    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.num_voices = 4;
    e.patch_number = patch_number;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.midi_note = 76;
    e.velocity = 1.0f;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.midi_note = 76;
    e.velocity = 0;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.pedal = 127;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.midi_note = 63;
    e.velocity = 1.0f;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.midi_note = 63;
    e.velocity = 0;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.midi_note = 67;
    e.velocity = 1.0f;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.midi_note = 67;
    e.velocity = 0;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.midi_note = 72;
    e.velocity = 1.0f;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.pedal = 0;
    amy_add_event(&e);

    start += 50;
    e = amy_default_event();
    e.time = start;
    e.synth = 1;
    e.midi_note = 72;
    e.velocity = 0;
    amy_add_event(&e);
}   



void example_patches() {
    amy_event e = amy_default_event();
    for(uint16_t i=0;i<256;i++) {
        e.patch_number = i;
        e.voices[0] = 0;
        fprintf(stderr, "sending patch %d\n", i);
        amy_add_event(&e);
        delay_ms(250);

        e = amy_default_event();
        e.voices[0] = 0;
        e.osc = 0;
        e.midi_note = 50;
        e.velocity = 0.5;
        amy_add_event(&e);

        delay_ms(1000);
        e.voices[0] = 0;
        e.velocity = 0;
        amy_add_event(&e);

        delay_ms(250);

        amy_reset_oscs();
    }
}
void example_reverb() {
    if(AMY_HAS_REVERB) {
        config_reverb(2, REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING, REVERB_DEFAULT_XOVER_HZ); 
    }
}

void example_chorus() {
    if(AMY_HAS_CHORUS) {
        config_chorus(0.8, CHORUS_DEFAULT_MAX_DELAY, CHORUS_DEFAULT_LFO_FREQ, CHORUS_DEFAULT_MOD_DEPTH);
    }
}

// Play a KS tone
void example_ks(uint32_t start) {
    amy_event e = amy_default_event();
    e.time = start;

    e.velocity = 1;
    e.wave = KS;
    e.feedback = 0.996f;
    e.preset = 15;
    e.osc = 0;
    e.midi_note = 60;
    amy_add_event(&e);
}

// make a 440hz sine
void example_sine(uint32_t start) {
    amy_event e = amy_default_event();
    e.time = start;
    e.freq_coefs[0] = 440;
    e.wave = SINE;
    e.velocity = 1;
    amy_add_event(&e);
}

void example_multimbral_fm() {
    amy_event e0 = amy_default_event();
    amy_event e1 = amy_default_event();
    int notes[] = {60, 70, 64, 68, 72, 82};
    e1.velocity = 0.5;
    e0.patch_number = 128;
    for (unsigned int i = 0; i < sizeof(notes) / sizeof(int); ++i) {
        e1.midi_note = notes[i];
        e1.pan_coefs[0] = (i%2);
        e0.patch_number++;
        e0.voices[0] = i;
        amy_add_event(&e0);
        amy_add_event(&e1);
        delay_ms(1000);
    }
}


// Emulate the Tulip "drums()" example via event calls.
void example_drums(uint32_t start, int loops) {

    // bd, snare, hat, cow, hicow
    int oscs[] = {0, 1, 2, 3, 4};
    int presets[] = {1, 5, 0, 10, 10};

    // Reset all used oscs first, just in case
    for (unsigned int i = 0; i < sizeof(oscs) / sizeof(int); ++i) {
        amy_event e_reset = amy_default_event();
        e_reset.time = start;
        e_reset.osc = oscs[i];    
        e_reset.reset_osc = oscs[i];
        amy_add_event(&e_reset);
    }

    amy_event e = amy_default_event();
    e.time = start + 1;
    float volume = 0.5;
    e.wave = PCM;
    e.velocity = 0;

    for (unsigned int i = 0; i < sizeof(oscs) / sizeof(int); ++i) {
        e.osc = oscs[i];
        e.preset = presets[i];
        amy_add_event(&e);
    }
    // Update high cowbell.
    e = amy_default_event();
    e.time = start+1;
    e.osc = 4;
    e.midi_note = 70;
    amy_add_event(&e);

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
    amy_add_event(&e);


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
                amy_add_event(&e);
            }
            if(x & snare) {
                e.osc = 1;
                e.velocity = 1.5 * volume;
                amy_add_event(&e);
            }
            if(x & hat) {
                e.osc = 2;
                e.velocity = 1 * volume;
                amy_add_event(&e);
            }
            if(x & cow) {
                e.osc = 3;
                e.velocity = 1 * volume;
                amy_add_event(&e);
            }
            if(x & hicow) {
                e.osc = 4;
                e.velocity = 1 * volume;
                amy_add_event(&e);
            }

            e.osc = 5;
            if(bassline[i]>0) {
                e.velocity = 0.5 * volume;
                e.midi_note = bassline[i] - 12;
            } else {
                e.velocity = 0;
            }
            amy_add_event(&e);
        }
    }
}

void example_sequencer_drums(uint32_t start) {
    // Play a drum pattern using the low-level sequencer structure
    amy_event e = amy_default_event();
    e.time = start;
    e.reset_osc = RESET_ALL_OSCS;
    amy_add_event(&e);

    // Set tempo so 16 ticks is 108 BPM (not 48 as default).  So we make the BPM one-sixth
    e = amy_default_event();
    e.tempo = 108.0f/3;
    amy_add_event(&e);

    // Setup oscs for bd, snare, hat, cow, hicow
    int oscs[] = {0, 1, 2, 3, 4};
    int presets[] = {1, 5, 0, 10, 10};
    e = amy_default_event();
    e.time = start + 1;
    e.wave = PCM;
    for (unsigned int i = 0; i < sizeof(oscs) / sizeof(int); ++i) {
        e.osc = oscs[i];
        e.preset = presets[i];
        amy_add_event(&e);
    }
    // Update high cowbell.
    e = amy_default_event();
    e.time = start + 1;
    e.osc = 4;
    e.midi_note = 70;
    amy_add_event(&e);

    // Add patterns.
    // Hi hat every 8 ticks.
    e = amy_default_event();
    e.sequence[SEQUENCE_TAG] = 0;
    e.sequence[SEQUENCE_PERIOD] = 8;
    e.sequence[SEQUENCE_TICK] = 0;
    e.osc = 2;
    e.velocity = 1.0;
    amy_add_event(&e);

    // Bass drum every 32 ticks.
    e.sequence[SEQUENCE_TAG] = 1;
    e.sequence[SEQUENCE_PERIOD] = 32;
    e.sequence[SEQUENCE_TICK] = 0;
    e.osc = 0;
    e.velocity = 1.0;
    amy_add_event(&e);

    // Snare every 32 ticks, counterphase to BD.
    e.sequence[SEQUENCE_TAG] = 2;
    e.sequence[SEQUENCE_PERIOD] = 32;
    e.sequence[SEQUENCE_TICK] = 16;
    e.osc = 1;
    e.velocity = 1.0;
    amy_add_event(&e);

    // Cow once every other cycle.
    e.sequence[SEQUENCE_TAG] = 3;
    e.sequence[SEQUENCE_PERIOD] = 64;
    e.sequence[SEQUENCE_TICK] = 60;
    e.osc = 3;
    e.velocity = 1.0;
    amy_add_event(&e);
}

void example_sequencer_drums_synth(uint32_t start) {
    // Play a drum pattern using the low-level sequencer structure driving default system drums synth (10)
    amy_event e;

    // Add patterns.
    // Hi hat every 8 ticks.
    e = amy_default_event();
    e.sequence[SEQUENCE_TAG] = 0;
    e.sequence[SEQUENCE_PERIOD] = 24;
    e.sequence[SEQUENCE_TICK] = 0;
    e.synth = 10;
    e.midi_note = 42;  // Closed Hat
    e.velocity = 1.0;
    amy_add_event(&e);

    // Bass drum every 32 ticks.
    e.sequence[SEQUENCE_TAG] = 1;
    e.sequence[SEQUENCE_PERIOD] = 96;
    e.sequence[SEQUENCE_TICK] = 0;
    e.synth = 10;
    e.midi_note = 35;  // Std kick
    e.velocity = 1.0;
    amy_add_event(&e);

    // Snare every 32 ticks, counterphase to BD.
    e.sequence[SEQUENCE_TAG] = 2;
    e.sequence[SEQUENCE_PERIOD] = 96;
    e.sequence[SEQUENCE_TICK] = 48;
    e.synth = 10;
    e.midi_note = 38;  // Snare
    e.velocity = 1.0;
    amy_add_event(&e);

    // Cow once every other cycle.
    e.sequence[SEQUENCE_TAG] = 3;
    e.sequence[SEQUENCE_PERIOD] = 192;
    e.sequence[SEQUENCE_TICK] = 180;
    e.synth = 10;
    e.midi_note = 56;  // Cowbell
    e.velocity = 1.0;
    amy_add_event(&e);
}

void example_fm(uint32_t start) {
    // Direct construction of an FM tone, as in the documentation.
    amy_event e;

    // Modulating oscillator (op 2)
    e = amy_default_event();
    e.time = start;
    e.osc = 9;
    e.wave = SINE;
    e.ratio = 1.0f;
    e.amp_coefs[COEF_CONST] = 1.0f;
    e.amp_coefs[COEF_VEL] = 0;
    e.amp_coefs[COEF_EG0] = 0;
    amy_add_event(&e);

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
    amy_add_event(&e);

    // ALGO control oscillator
    e = amy_default_event();
    e.time = start;
    e.osc = 7;
    e.wave = ALGO;
    e.algorithm = 1;  // algo 1 has op 2 driving op 1 driving output (plus a second chain for ops 6,5,4,3).
    e.algo_source[4] = 9;
    e.algo_source[5] = 8;
    amy_add_event(&e);

    // Add a note on event.
    e = amy_default_event();
    e.time = start + 100;
    e.osc = 7;
    e.midi_note = 60;
    e.velocity = 1.0f;
    amy_add_event(&e);
}


// Minimal custom oscillator

void beeper_init(void) {
    //printf("Beeper init\n");
}

void beeper_note_on(uint16_t osc, float freq) {
    saw_down_note_on(osc, freq);
}

void beeper_note_off(uint16_t osc) {
    synth[osc]->note_off_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
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
    amy_event e = amy_default_event();
    e.osc = 50;
    e.time = amy_sysclock();
    e.freq_coefs[0] = 880;
    e.wave = CUSTOM;
    e.velocity = 1;
    amy_add_event(&e);

    e.velocity = 0;
    e.time += 500;
    amy_add_event(&e);
}

void example_patch_from_events() {
    int time = amy_sysclock();
    int number = 1039;
    amy_event e = amy_default_event();
    e.time = time;
    e.patch_number = number;
    e.reset_osc = RESET_PATCH;
    amy_add_event(&e);

    e = amy_default_event();
    e.time = time;
    e.patch_number = number;
    e.osc = 0;
    e.wave = SAW_DOWN;
    e.chained_osc = 1;
    strcpy(e.bp0, "0,1,1000,0.1,200,0");
    amy_add_event(&e);

    e = amy_default_event();
    e.time = time;
    e.patch_number = number;
    e.osc = 1;
    e.wave = SINE;
    e.freq_coefs[COEF_CONST] = 131.0f;
    strcpy(e.bp0, "0,1,500,0,200,0");
    amy_add_event(&e);

    e = amy_default_event();
    e.time = time;
    e.synth = 0;
    e.num_voices = 4;
    e.patch_number = number;
    amy_add_event(&e);

    e = amy_default_event();
    e.time = time + 100;
    e.synth = 0;
    e.midi_note = 60.0f;
    e.velocity = 1.0f;
    amy_add_event(&e);

    e = amy_default_event();
    e.time = time + 300;
    e.synth = 0;
    e.midi_note = 64.0f;
    e.velocity = 1.0f;
    amy_add_event(&e);

    e = amy_default_event();
    e.time = time + 500;
    e.synth = 0;
    e.midi_note = 67.0f;
    e.velocity = 1.0f;
    amy_add_event(&e);

    e = amy_default_event();
    e.time = time + 800;
    e.synth = 0;
    e.velocity = 0.0f;
    amy_add_event(&e);
}
