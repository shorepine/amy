// api.c
// C callable entry points to amy

#include "amy.h"

//////////////////////
// Hooks

// Optional render hook that's called per oscillator during rendering, used (now) for CV output from oscillators. return 1 if this oscillator should be silent
uint8_t (*amy_external_render_hook)(uint16_t osc, SAMPLE*, uint16_t len ) = NULL;

// Optional external coef setter (meant for CV control of AMY via CtrlCoefs)
float (*amy_external_coef_hook)(uint16_t channel) = NULL;

// Optional hook that's called after all processing is done for a block, meant for python callback control of AMY
void (*amy_external_block_done_hook)(void) = NULL;

// Optional hook for a consumer of AMY to access MIDI data coming IN to AMY
void (*amy_external_midi_input_hook)(uint8_t * bytes, uint16_t len, uint8_t is_sysex) = NULL;

// Called every sequencer tick
void (*amy_external_sequencer_hook)(uint32_t) = NULL;


amy_config_t amy_default_config() {
    amy_config_t c;
    c.features.reverb = 1;
    c.features.echo = 1;
    c.features.chorus = 1;
    c.features.partials = 1;
    c.features.custom = 1;
    c.features.audio_in = 1;
    c.features.default_synths = 1;
    c.features.startup_bleep = 0;

    c.midi = AMY_MIDI_IS_NONE;
    #ifndef AMY_MCU
    c.audio = AMY_AUDIO_IS_MINIAUDIO;
    #else
    c.audio = AMY_AUDIO_IS_I2S;
    #endif
    c.ks_oscs = 1;

    c.max_oscs = 180;
    c.max_sequencer_tags = 256;
    c.max_voices = 64;
    c.max_synths = 64;
    c.max_memory_patches = 32;

    // caps
#if defined(TULIP) || defined(AMYBOARD) // || defined(ESP_PLATFORM)
    c.ram_caps_events = MALLOC_CAP_SPIRAM;
    c.ram_caps_synth = MALLOC_CAP_SPIRAM;
    c.ram_caps_block = MALLOC_CAP_DEFAULT;
    c.ram_caps_fbl = MALLOC_CAP_DEFAULT;
    c.ram_caps_delay = MALLOC_CAP_SPIRAM;
    c.ram_caps_sample = MALLOC_CAP_SPIRAM;
    c.ram_caps_sysex = MALLOC_CAP_SPIRAM;
    #else
    c.ram_caps_events = MALLOC_CAP_DEFAULT;
    c.ram_caps_synth = MALLOC_CAP_DEFAULT;
    c.ram_caps_block = MALLOC_CAP_DEFAULT;
    c.ram_caps_fbl = MALLOC_CAP_DEFAULT;
    c.ram_caps_delay = MALLOC_CAP_DEFAULT;
    c.ram_caps_sample = MALLOC_CAP_DEFAULT;
    c.ram_caps_sysex = MALLOC_CAP_DEFAULT;
    #endif    

    c.capture_device_id = -1;
    c.playback_device_id = -1;

    c.i2s_lrc = -1;
    c.i2s_dout = -1;
    c.i2s_din = -1;
    c.i2s_bclk = -1;
    c.i2s_mclk = -1;
    c.i2s_mclk_mult = 256;  // MCLK = mclk_mult * Fs.
    c.midi_out = -1;
    c.midi_in = -1;
    c.midi_uart = -1; 

    #ifdef ESP_PLATFORM
    c.midi_uart = 1;
    #endif

    #if (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2350)
    c.midi_uart = 1;
    #endif

    return c;
}


// create a new default API accessible event
amy_event amy_default_event() {
    amy_event e;
    amy_clear_event(&e);
    return e;
}

void amy_clear_event(amy_event *e) {
    e->status = EVENT_EMPTY;
    AMY_UNSET(e->time);
    AMY_UNSET(e->osc);
    AMY_UNSET(e->preset);
    AMY_UNSET(e->wave);
    AMY_UNSET(e->patch_number);
    AMY_UNSET(e->phase);
    AMY_UNSET(e->feedback);
    AMY_UNSET(e->velocity);
    AMY_UNSET(e->midi_note);
    AMY_UNSET(e->volume);
    AMY_UNSET(e->pitch_bend);
    AMY_UNSET(e->tempo);
    AMY_UNSET(e->latency_ms);
    AMY_UNSET(e->ratio);
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) {
        AMY_UNSET(e->amp_coefs[i]);
        AMY_UNSET(e->freq_coefs[i]);
        AMY_UNSET(e->filter_freq_coefs[i]);
        AMY_UNSET(e->duty_coefs[i]);
        AMY_UNSET(e->pan_coefs[i]);
    }
    AMY_UNSET(e->resonance);
    AMY_UNSET(e->portamento_ms);
    AMY_UNSET(e->filter_type);
    AMY_UNSET(e->chained_osc);
    AMY_UNSET(e->mod_source);
    AMY_UNSET(e->eq_l);
    AMY_UNSET(e->eq_m);
    AMY_UNSET(e->eq_h);
    AMY_UNSET(e->algorithm);
    AMY_UNSET(e->bp_is_set[0]);
    AMY_UNSET(e->bp_is_set[1]);
    AMY_UNSET(e->eg_type[0]);
    AMY_UNSET(e->eg_type[1]);
    AMY_UNSET(e->reset_osc);
    AMY_UNSET(e->note_source);
    for (int i = 0; i < MAX_ALGO_OPS; ++i) {
        AMY_UNSET(e->algo_source[i]);
    }
    for (int i = 0; i < MAX_VOICES_PER_INSTRUMENT; ++i) {
        AMY_UNSET(e->voices[i]);
    }
    e->bp0[0] = 0;
    e->bp1[0] = 0;
    AMY_UNSET(e->synth);
    AMY_UNSET(e->synth_flags);
    AMY_UNSET(e->to_synth);
    AMY_UNSET(e->grab_midi_notes);
    AMY_UNSET(e->pedal);
    AMY_UNSET(e->num_voices);
    AMY_UNSET(e->sequence[SEQUENCE_TICK]);
    AMY_UNSET(e->sequence[SEQUENCE_PERIOD]);
    AMY_UNSET(e->sequence[SEQUENCE_TAG]);
}


// get AUDIO_IN0 and AUDIO_IN1
void amy_get_input_buffer(output_sample_type * samples) {
    for(uint16_t i=0;i<AMY_BLOCK_SIZE*AMY_NCHANS;i++) samples[i] = amy_in_block[i];
}

// set AUDIO_EXT0 and AUDIO_EXT1
void amy_set_external_input_buffer(output_sample_type * samples) {
    for(uint16_t i=0;i<AMY_BLOCK_SIZE*AMY_NCHANS;i++) amy_external_in_block[i] = samples[i];
}

output_sample_type * amy_simple_fill_buffer() {
    amy_execute_deltas();
    amy_render(0, AMY_OSCS, 0);
    return amy_fill_buffer();
}


// on all platforms, sysclock is based on total samples played, using audio out (i2s or etc) as system clock
uint32_t amy_sysclock() {
    // Time is returned in integer milliseconds.  uint32_t rollover is 49.7 days.
    return (uint32_t)((amy_global.total_blocks * AMY_BLOCK_SIZE / (float)AMY_SAMPLE_RATE) * 1000);
}


// given a wire message string play / schedule the event directly (WIRE API)
void amy_add_message(char *message) {
    peek_stack("add_message");
    amy_event e; // = amy_default_event();
    amy_clear_event(&e);
    // Parse the wire string into an event
    amy_parse_message(message, strlen(message), &e);
    amy_add_event(&e);
}

// given an event play / schedule the event directly (C API)
void amy_add_event(amy_event *e) {
    peek_stack("add_event");
    amy_process_event(e);
    // Do not "play" events that are not sent directly to the AMY synthesizer, e.g. sequencer events or stored patches
    if(e->status == EVENT_SCHEDULED) {
        amy_event_to_deltas_queue(e, 0, &amy_global.delta_queue);
    }
}



void amy_stop() {
    oscs_deinit();
}


#ifdef __EMSCRIPTEN__
void amy_start_web() {
    // a shim for web AMY, as it's annoying to build structs in js
    amy_config_t amy_config = amy_default_config();
    amy_config.midi = AMY_MIDI_IS_WEBMIDI;
    amy_config.audio = AMY_AUDIO_IS_MINIAUDIO;
    amy_start(amy_config);
}

void amy_start_web_no_synths() {
    // a shim for web AMY, as it's annoying to build structs in js
    amy_config_t amy_config = amy_default_config();
    amy_config.midi = AMY_MIDI_IS_WEBMIDI;
    amy_config.audio = AMY_AUDIO_IS_MINIAUDIO;
    amy_config.features.default_synths = 0;
    amy_start(amy_config);
}
#endif

// defined in midi_mappings.c
extern void juno_filter_midi_handler(uint8_t * bytes, uint16_t len, uint8_t is_sysex);

void amy_default_synths() {
    // Configure several default synthesizers for "out of box" playability.

    // sine wave "bleeper" on ch 0 (not a MIDI channel)
    amy_event e = amy_default_event();
    e.patch_number= 1024;
    // osc=0 sinewave.
    // Sine is default, but we need to have one delta, else the number of oscs is zero = no patch.
    patches_store_patch(&e, "v0w0");
    e.num_voices = 1;
    e.synth = 0;
    amy_add_event(&e);

    // GM drum synth on channel 10
    e = amy_default_event();
    e.patch_number = 1025;
    patches_store_patch(&e, "w7f0");
    e.num_voices = 6;
    e.synth = 10;
    // Flag to perform note -> drum PCM patch translation.
    e.synth_flags = _SYNTH_FLAGS_MIDI_DRUMS | _SYNTH_FLAGS_IGNORE_NOTE_OFFS;
    amy_add_event(&e);

    // DX7 6 note poly on channel 2
    e = amy_default_event();
    e.num_voices = 6;
    e.patch_number = 128;
    e.synth = 2;
    amy_add_event(&e);

    // Juno 6 poly on channel 1
    // Define this last so if we release it, the oscs aren't fragmented.
    e = amy_default_event();
    e.num_voices = 6;
    e.patch_number = 0;
    e.synth = 1;
    amy_add_event(&e);
    // Add some MIDI CCs for the Juno (defined in midi_mappings.c).
    amy_external_midi_input_hook = juno_filter_midi_handler;
}

// Schedule a bleep now
void amy_bleep(uint32_t start) {
    amy_event e = amy_default_event();
    e.osc = AMY_OSCS - 1;  // Use a high-up osc to avoid collisions?
    e.time = start;
    e.wave = SINE;
    e.freq_coefs[COEF_CONST] = 220;
    e.pan_coefs[COEF_CONST] = 0.9;
    e.velocity = 1;
    amy_add_event(&e);
    e.time = start + 150;
    e.freq_coefs[COEF_CONST] = 440;
    e.pan_coefs[COEF_CONST] = 0.1;
    amy_add_event(&e);
    e.time = start + 300;
    e.velocity = 0;
    e.pan_coefs[COEF_CONST] = 0.5;  // Restore default pan to osc.
    amy_add_event(&e);
}

// Schedule a bleep now using default bleep synth (0)
void amy_bleep_synth(uint32_t start) {
    amy_event e = amy_default_event();
    e.synth = 0;
    e.time = start;
    // you have to use notes with synths, so the voice manager can grok.
    e.midi_note = 57;
    e.pan_coefs[COEF_CONST] = 0.9;
    e.velocity = 1;
    amy_add_event(&e);
    e.time = start + 150;
    e.midi_note = 69;
    e.pan_coefs[COEF_CONST] = 0.1;
    amy_add_event(&e);
    e.time = start + 300;
    e.velocity = 0;
    e.pan_coefs[COEF_CONST] = 0.5;  // Restore default pan to osc 0.
    amy_add_event(&e);
}

void amy_start(amy_config_t c) {
    global_init(c);
    run_midi();
    amy_profiles_init();
    sequencer_start();
    oscs_init();
    if(AMY_HAS_DEFAULT_SYNTHS) amy_default_synths();
    if(AMY_HAS_STARTUP_BLEEP) {
        if(AMY_HAS_DEFAULT_SYNTHS)
            amy_bleep_synth(0);  // bleep using the default sinewave synth voice.
        else
            amy_bleep(0);  // bleep using raw oscs.
    }
}
