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



amy_config_t amy_default_config() {
    amy_config_t c;
    c.has_reverb = 1;
    c.has_echo = 1;
    c.has_chorus = 1;
    c.has_partials = 1;
    c.has_custom = 1;
    c.ks_oscs = 1;
    c.delta_fifo_len = 2400;
    c.has_audio_in = 1;
    c.has_midi_uart = 0;
    c.has_midi_web = 0;
    c.has_midi_gadget = 0;
    c.set_default_synth = 1;
    c.cores = 1;
    c.max_oscs = 180;

    // caps
    #if (defined TULIP) || (defined AMYBOARD)
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
    c.midi_out = -1;
    c.midi_in = -1;
    return c;
}


// create a new default API accessible event
amy_event amy_default_event() {
    amy_event e;
    e.status = EVENT_EMPTY;
    AMY_UNSET(e.time);
    AMY_UNSET(e.osc);
    AMY_UNSET(e.preset);
    AMY_UNSET(e.wave);
    AMY_UNSET(e.patch_number);
    AMY_UNSET(e.phase);
    AMY_UNSET(e.feedback);
    AMY_UNSET(e.velocity);
    AMY_UNSET(e.midi_note);
    AMY_UNSET(e.volume);
    AMY_UNSET(e.pitch_bend);
    AMY_UNSET(e.tempo);
    AMY_UNSET(e.latency_ms);
    AMY_UNSET(e.ratio);
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) {
        AMY_UNSET(e.amp_coefs[i]);
        AMY_UNSET(e.freq_coefs[i]);
        AMY_UNSET(e.filter_freq_coefs[i]);
        AMY_UNSET(e.duty_coefs[i]);
        AMY_UNSET(e.pan_coefs[i]);
    }
    AMY_UNSET(e.resonance);
    AMY_UNSET(e.portamento_ms);
    AMY_UNSET(e.filter_type);
    AMY_UNSET(e.chained_osc);
    AMY_UNSET(e.mod_source);
    AMY_UNSET(e.eq_l);
    AMY_UNSET(e.eq_m);
    AMY_UNSET(e.eq_h);
    AMY_UNSET(e.algorithm);
    AMY_UNSET(e.bp_is_set[0]);
    AMY_UNSET(e.bp_is_set[1]);
    AMY_UNSET(e.eg_type[0]);
    AMY_UNSET(e.eg_type[1]);
    AMY_UNSET(e.reset_osc);
    AMY_UNSET(e.source);
    e.algo_source[0] = 0;
    e.bp0[0] = 0;
    e.bp1[0] = 0;
    e.voices[0] = 0;
    AMY_UNSET(e.synth);
    AMY_UNSET(e.synth_flags);
    AMY_UNSET(e.to_synth);
    AMY_UNSET(e.grab_midi_notes);
    AMY_UNSET(e.pedal);
    AMY_UNSET(e.num_voices);
    AMY_UNSET(e.sequence[SEQUENCE_TICK]);
    AMY_UNSET(e.sequence[SEQUENCE_PERIOD]);
    AMY_UNSET(e.sequence[SEQUENCE_TAG]);
    return e;
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
    amy_prepare_buffer();
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
    amy_event e = amy_default_event();
    // Parse the wire string into an event
    amy_parse_message(message, &e);
    amy_add_event(&e);
}

// given an event play / schedule the event directly (C API)
void amy_add_event(amy_event *e) {
    amy_process_event(e);
    // Do not "play" events that are not sent directly to the AMY synthesizer, e.g. sequencer events or stored patches
    if(e->status == EVENT_SCHEDULED) {
   	    amy_event_to_deltas_then(e, 0, add_delta_to_queue, NULL);
    }
}



void amy_stop() {
    oscs_deinit();
}


#ifdef __EMSCRIPTEN__
void amy_start_web() {
    // a shim for web AMY, as it's annoying to build structs in js
    amy_config_t amy_config = amy_default_config();
    amy_config.has_midi_web = 1;
    amy_start(amy_config);
}
#endif

void amy_start(amy_config_t c) {
    global_init(c);
    run_midi();
    amy_profiles_init();
    sequencer_init();
    oscs_init();
    if(amy_global.config.set_default_synth)amy_default_setup();
}