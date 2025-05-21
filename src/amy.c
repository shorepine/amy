// libAMY

// Brian Whitman
// brian@variogr.am

#include "amy.h"

#ifdef AMY_DEBUG

const char* profile_tag_name(enum itags tag) {
    switch (tag) {
        case RENDER_OSC_WAVE: return "RENDER_OSC_WAVE";
        case COMPUTE_BREAKPOINT_SCALE: return "COMPUTE_BREAKPOINT_SCALE";
        case HOLD_AND_MODIFY: return "HOLD_AND_MODIFY";
        case FILTER_PROCESS: return "FILTER_PROCESS";
        case FILTER_PROCESS_STAGE0: return "FILTER_PROCESS_STAGE0";
        case FILTER_PROCESS_STAGE1: return "FILTER_PROCESS_STAGE1";
        case ADD_DELTA_TO_QUEUE: return "ADD_DELTA_TO_QUEUE";
        case AMY_ADD_DELTA: return "AMY_ADD_DELTA";
        case PLAY_DELTA: return "PLAY_DELTA";
        case MIX_WITH_PAN: return "MIX_WITH_PAN";
        case AMY_RENDER: return "AMY_RENDER";            
        case AMY_PREPARE_BUFFER: return "AMY_PREPARE_BUFFER";            
        case AMY_FILL_BUFFER: return "AMY_FILL_BUFFER";            
        case RENDER_LUT_FM: return "RENDER_LUT_FM";            
        case RENDER_LUT_FB: return "RENDER_LUT_FB";            
        case RENDER_LUT: return "RENDER_LUT";            
        case RENDER_LUT_CUB: return "RENDER_LUT_CUB";            
        case RENDER_LUT_FM_FB: return "RENDER_LUT_FM_FB";            
        case RENDER_LPF_LUT: return "RENDER_LPF_LUT";            
        case DSPS_BIQUAD_F32_ANSI_SPLIT_FB: return "DSPS_BIQUAD_F32_ANSI_SPLIT_FB";            
        case DSPS_BIQUAD_F32_ANSI_SPLIT_FB_TWICE: return "DSPS_BIQUAD_F32_ANSI_SPLIT_FB_TWICE";
        case DSPS_BIQUAD_F32_ANSI_COMMUTED: return "DSPS_BIQUAD_F32_ANSI_COMMUTED";            
        case PARAMETRIC_EQ_PROCESS: return "PARAMETRIC_EQ_PROCESS";            
        case HPF_BUF: return "HPF_BUF";            
        case SCAN_MAX: return "SCAN_MAX";            
        case DSPS_BIQUAD_F32_ANSI: return "DSPS_BIQUAD_F32_ANSI";            
        case BLOCK_NORM: return "BLOCK_NORM";       
        case CALIBRATE: return "CALIBRATE";
        case AMY_ESP_FILL_BUFFER: return "AMY_ESP_FILL_BUFFER";
        case NO_TAG: return "NO_TAG";
   }
   return "ERROR";
}
struct profile profiles[NO_TAG];
uint64_t profile_start_us = 0;

#ifdef ESP_PLATFORM
#include "esp_timer.h"
int64_t amy_get_us() { return esp_timer_get_time(); }
#elif defined PICO_ON_DEVICE
int64_t amy_get_us() { return to_us_since_boot(get_absolute_time()); }
#else
#include <sys/time.h>
int64_t amy_get_us() { struct timeval tv; gettimeofday(&tv,NULL); return tv.tv_sec*(uint64_t)1000000+tv.tv_usec; }
#endif

void amy_profiles_init() { 
    for(uint8_t i=0;i<NO_TAG;i++) { AMY_PROFILE_INIT(i) } 
} 
void amy_profiles_print() { for(uint8_t i=0;i<NO_TAG;i++) { AMY_PROFILE_PRINT(i) } amy_profiles_init(); }
#else
#define amy_profiles_init() 
#define amy_profiles_print()
#endif



// This defaults PCM size to small. If you want to be different, include "pcm_large.h" or "pcm_tiny.h"
#ifdef ALLES
#include "pcm_large.h"
#elif defined ARDUINO
#include "pcm_tiny.h"
#elif defined AMY_DAISY
#include "pcm_tiny.h"
#else
#include "pcm_small.h"
#endif

#include "clipping_lookup_table.h"

// Final output delay lines.
delay_line_t **chorus_delay_lines; 
delay_line_t **echo_delay_lines; 
SAMPLE *echo_delays[AMY_MAX_CHANNELS];
SAMPLE *delay_mod = NULL;


#ifdef _POSIX_THREADS
#include <pthread.h>
pthread_mutex_t amy_queue_lock; 
#endif

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
SemaphoreHandle_t xQueueSemaphore;
TaskHandle_t midi_handle;
#endif

// Global state 
struct state amy_global;
// set of deltas for the fifo to be played
struct delta * deltas;
// state per osc as multi-channel synthesizer that the scheduler renders into
struct synthinfo ** synth;
// envelope-modified per-osc state
struct mod_synthinfo ** msynth;

// Two mixing blocks, one per core of rendering
SAMPLE ** fbl;
SAMPLE ** per_osc_fb; 
SAMPLE core_max[AMY_MAX_CORES];

// Audio input blocks. Filled by the audio implementation before rendering.
// For live audio input from a codec, AUDIO_IN0 / 1
output_sample_type * amy_in_block;
// For generated audio streams, AUDIO_EXT0 / 1
output_sample_type * amy_external_in_block;
// block -- what gets sent to the dac -- -32768...32767 (int16 le)
output_sample_type * block;

//////////////////////
// Hooks

// Optional render hook that's called per oscillator during rendering, used (now) for CV output from oscillators. return 1 if this oscillator should be silent
uint8_t (*amy_external_render_hook)(uint16_t, SAMPLE*, uint16_t len ) = NULL;

// Optional external coef setter (meant for CV control of AMY via CtrlCoefs)
float (*amy_external_coef_hook)(uint16_t) = NULL;

// Optional hook that's called after all processing is done for a block, meant for python callback control of AMY
void (*amy_external_block_done_hook)(void) = NULL;

// Optional hook for a consumer of AMY to access MIDI data coming IN to AMY
void (*amy_external_midi_input_hook)(uint8_t *, uint16_t, uint8_t) = NULL;





#ifndef MALLOC_CAPS_DEFINED
  #define MALLOC_CAPS_DEFINED
  void * malloc_caps(uint32_t size, uint32_t flags) {
    void *result;
  #ifdef ESP_PLATFORM
    result = heap_caps_malloc(size, flags);
  #else
    // ignore flags
    result = malloc(size);
  #endif
    //if (size > 400000) abort();
    //fprintf(stderr, "malloc(%ld) @0x%lx\n", size, result);
    return result;
  }
#endif


uint32_t enclosing_power_of_2(uint32_t n) {
    uint32_t result = 1;
    while (result < n)  result <<= 1;
    return result;
}

void config_echo(float level, float delay_ms, float max_delay_ms, float feedback, float filter_coef) {
    uint32_t delay_samples = (uint32_t)(delay_ms / 1000.f * AMY_SAMPLE_RATE);
    //fprintf(stderr, "config_echo: delay_ms=%.3f max_delay_ms=%.3f delay_samples=%d echo.max_delay_samples=%d\n", delay_ms, max_delay_ms, delay_samples, echo.max_delay_samples);
    if (level > 0) {
        if (echo_delay_lines[0] == NULL) {
            // Delay line len must be power of 2.
            uint32_t max_delay_samples = enclosing_power_of_2((uint32_t)(max_delay_ms / 1000.f * AMY_SAMPLE_RATE));
            for (int c = 0; c < AMY_NCHANS; ++c)
                echo_delay_lines[c] = new_delay_line(max_delay_samples, 0, amy_global.config.ram_caps_delay);
            amy_global.echo.max_delay_samples = max_delay_samples;
            //fprintf(stderr, "config_echo: max_delay_samples=%d\n", max_delay_samples);
        }
        // Apply delay.  We have to stay 1 sample less than delay line length for FIR EQ delay.
        if (delay_samples > amy_global.echo.max_delay_samples - 1) delay_samples = amy_global.echo.max_delay_samples - 1;
        for (int c = 0; c < AMY_NCHANS; ++c) {
            echo_delay_lines[c]->fixed_delay = delay_samples;
        }
    }
    amy_global.echo.level = F2S(level);
    amy_global.echo.delay_samples = delay_samples;
    // Filter is IIR [1, filter_coef] normalized for filter_coef > 0 (LPF), or FIR [1, filter_coef] normalized for filter_coef < 0 (HPF).
    if (filter_coef > 0.99)  filter_coef = 0.99;  // Avoid unstable filters.
    amy_global.echo.filter_coef = F2S(filter_coef);
    // FIR filter potentially has gain > 1 for high frequencies, so discount the loop feedback to stop things exploding.
    if (filter_coef < 0)  feedback /= 1.f - filter_coef;
    amy_global.echo.feedback = F2S(feedback);
    //fprintf(stderr, "config_echo: delay_samples=%d level=%.3f feedback=%.3f filter_coef=%.3f fc0=%.3f\n", delay_samples, level, feedback, filter_coef, S2F(echo.filter_coef));
}

void dealloc_echo_delay_lines(void) {
    for (uint16_t c = 0; c < AMY_NCHANS; ++c)
        if (echo_delay_lines[c]) free(echo_delay_lines[c]);
}


void alloc_chorus_delay_lines(void) {
    for(uint16_t c=0;c<AMY_NCHANS;++c) {
        chorus_delay_lines[c] = new_delay_line(DELAY_LINE_LEN, DELAY_LINE_LEN / 2, amy_global.config.ram_caps_delay);
    }
    delay_mod = (SAMPLE *)malloc_caps(sizeof(SAMPLE) * AMY_BLOCK_SIZE, amy_global.config.ram_caps_delay);
}

void dealloc_chorus_delay_lines(void) {
    for(uint16_t c=0;c<AMY_NCHANS;++c) {
        if (chorus_delay_lines[c]) free(chorus_delay_lines[c]);
        chorus_delay_lines[c] = NULL;
    }
    free(delay_mod);
    delay_mod = NULL;
}

void config_chorus(float level, int max_delay, float lfo_freq, float depth) {
    //fprintf(stderr, "config_chorus: osc %d level %.3f max_del %d lfo_freq %.3f depth %.3f\n",
    //        CHORUS_MOD_SOURCE, level, max_delay, lfo_freq, depth);
    if (level > 0) {
        ensure_osc_allocd(CHORUS_MOD_SOURCE, NULL);
        // only allocate delay lines if chorus is more than inaudible.
        if (chorus_delay_lines[0] == NULL) {
            alloc_chorus_delay_lines();
        }
        // if we're turning on for the first time, start the oscillator.
        if (synth[CHORUS_MOD_SOURCE]->status == SYNTH_OFF) {  //chorus.level == 0) {
            // Setup chorus oscillator.
            synth[CHORUS_MOD_SOURCE]->logfreq_coefs[COEF_CONST] = logfreq_of_freq(lfo_freq);
            synth[CHORUS_MOD_SOURCE]->logfreq_coefs[COEF_NOTE] = 0;  // Turn off default.
            synth[CHORUS_MOD_SOURCE]->logfreq_coefs[COEF_BEND] = 0;  // Turn off default.
            synth[CHORUS_MOD_SOURCE]->amp_coefs[COEF_CONST] = depth;
            synth[CHORUS_MOD_SOURCE]->amp_coefs[COEF_VEL] = 0;  // Turn off default.
            synth[CHORUS_MOD_SOURCE]->amp_coefs[COEF_EG0] = 0;  // Turn off default.
            synth[CHORUS_MOD_SOURCE]->wave = TRIANGLE;
            osc_note_on(CHORUS_MOD_SOURCE, freq_of_logfreq(synth[CHORUS_MOD_SOURCE]->logfreq_coefs[COEF_CONST]));
        }
        // apply max_delay.
        for (int chan=0; chan<AMY_NCHANS; ++chan) {
            //chorus_delay_lines[chan]->max_delay = max_delay;
            chorus_delay_lines[chan]->fixed_delay = (int)max_delay / 2;
        }
    }
    amy_global.chorus.max_delay = max_delay;
    amy_global.chorus.level = F2S(level);
    amy_global.chorus.lfo_freq = lfo_freq;
    amy_global.chorus.depth = depth;
}

void config_reverb(float level, float liveness, float damping, float xover_hz) {
    if (level > 0) {
        //printf("config_reverb: level %f liveness %f xover %f damping %f\n",
        //      level, liveness, xover_hz, damping);
        if (amy_global.reverb.level == 0) { 
            init_stereo_reverb();  // In case it's the first time
        }
        config_stereo_reverb(liveness, xover_hz, damping);
    }
    amy_global.reverb.level = F2S(level);
    amy_global.reverb.liveness = liveness;
    amy_global.reverb.damping = damping;
    amy_global.reverb.xover_hz = xover_hz;
}


int8_t check_init(amy_err_t (*fn)(), char *name) {
    //fprintf(stderr,"starting %s: ", name);
    const amy_err_t ret = (*fn)();
    if(ret != AMY_OK) {
        fprintf(stderr,"[error:%i]\n", ret);
        return -1;
    }
    //fprintf(stderr,"[ok]\n");
    return 0;
}

int8_t global_init(amy_config_t c) {
    amy_global.config = c;
    amy_global.next_delta_write = 0;
    amy_global.delta_start = NULL;
    amy_global.delta_qsize = 0;
    amy_global.volume = 1.0f;
    amy_global.pitch_bend = 0;
    amy_global.latency_ms = 0;
    amy_global.tempo = 108.0; 
    amy_global.eq[0] = F2S(1.0f);
    amy_global.eq[1] = F2S(1.0f);
    amy_global.eq[2] = F2S(1.0f);
    amy_global.hpf_state = 0; 
    amy_global.transfer_flag = 0;
    amy_global.transfer_storage = NULL;
    amy_global.transfer_length = 0;
    amy_global.transfer_stored = 0;
    amy_global.debug_flag = 0;
    amy_global.sequencer_tick_count = 0;
    amy_global.next_amy_tick_us = 0;
    amy_global.us_per_tick = 0;
    amy_global.sequence_entry_ll_start = NULL;
    
    amy_global.reverb.level = F2S(REVERB_DEFAULT_LEVEL);
    amy_global.reverb.liveness= REVERB_DEFAULT_LIVENESS;
    amy_global.reverb.damping = REVERB_DEFAULT_DAMPING;
    amy_global.reverb.xover_hz = REVERB_DEFAULT_XOVER_HZ;

    amy_global.chorus.level = CHORUS_DEFAULT_LEVEL;
    amy_global.chorus.max_delay =  CHORUS_DEFAULT_MAX_DELAY;
    amy_global.chorus.lfo_freq = CHORUS_DEFAULT_LFO_FREQ;
    amy_global.chorus.depth = CHORUS_DEFAULT_MOD_DEPTH;

    amy_global.echo.level = F2S(ECHO_DEFAULT_LEVEL);
    amy_global.echo.delay_samples = (uint32_t)(ECHO_DEFAULT_DELAY_MS * 1000.f / AMY_SAMPLE_RATE);
    amy_global.echo.max_delay_samples = 65536;
    amy_global.echo.feedback = F2S(ECHO_DEFAULT_FEEDBACK);
    amy_global.echo.filter_coef = ECHO_DEFAULT_FILTER_COEF;

    return 0;
}

// Convert to and from the log-frequency scale.
// A log-frequency scale is good for summing control inputs.
float logfreq_of_freq(float freq) {
    // logfreq is defined as log_2(freq / 8.18 Hz)
    //if (freq==0) return ZERO_HZ_LOG_VAL;
    // Actually, special-case zero to mean middle C, for convenience.
    if (freq==0) return 0;  // i.e. == logfreq_of_freq(ZERO_LOGFREQ_IN_HZ == 261.63.
    return log2f(freq / ZERO_LOGFREQ_IN_HZ);
}

float freq_of_logfreq(float logfreq) {
    if (logfreq==ZERO_HZ_LOG_VAL) return 0;
    return ZERO_LOGFREQ_IN_HZ * exp2f(logfreq);
}

float freq_for_midi_note(float midi_note) {
    return 440.0f*powf(2.f, (midi_note - 69.0f) / 12.0f);
}

float logfreq_for_midi_note(float midi_note) {
    // TODO: Precompensate for EPS_FOR_LOG
    return (midi_note - ZERO_MIDI_NOTE) / 12.0f;
}

float midi_note_for_logfreq(float logfreq) {
    return 12.0f * logfreq + ZERO_MIDI_NOTE;
}


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

    #ifndef ARDUINO
    c.max_oscs = 180;
    #else
    c.max_oscs = 120;
    #endif

    // caps
    #if (defined TULIP) || (defined AMYBOARD)
    c.ram_caps_events = MALLOC_CAP_SPIRAM;
    c.ram_caps_synth = MALLOC_CAP_SPIRAM;
    c.ram_caps_block = MALLOC_CAP_DEFAULT;
    c.ram_caps_fbl = MALLOC_CAP_DEFAULT;
    c.ram_caps_delay = MALLOC_CAP_SPIRAM;
    c.ram_caps_sample = MALLOC_CAP_SPIRAM;
    #else
    c.ram_caps_events = MALLOC_CAP_DEFAULT;
    c.ram_caps_synth = MALLOC_CAP_DEFAULT;
    c.ram_caps_block = MALLOC_CAP_DEFAULT;
    c.ram_caps_fbl = MALLOC_CAP_DEFAULT;
    c.ram_caps_delay = MALLOC_CAP_DEFAULT;
    c.ram_caps_sample = MALLOC_CAP_DEFAULT;
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
struct event amy_default_event() {
    struct event e;
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
    return e;
}

void add_delta_to_queue(struct delta *d, void*user_data) {
    AMY_PROFILE_START(ADD_DELTA_TO_QUEUE)
#if defined ESP_PLATFORM && !defined ARDUINO
    //  take the queue mutex before starting
    xSemaphoreTake(xQueueSemaphore, portMAX_DELAY);
#elif defined _POSIX_THREADS
    //fprintf(stderr,"add_delta: time %d osc %d param %d val 0x%x, qsize %d\n", amy_global.total_blocks, d->osc, d->param, d->data, amy_global.delta_qsize);
    pthread_mutex_lock(&amy_queue_lock); 
#endif

    if(amy_global.delta_qsize < AMY_DELTA_FIFO_LEN) {
        // scan through the memory to find a free slot, starting at write pointer
        uint16_t write_location = amy_global.next_delta_write;
        int16_t found = -1;
        // guaranteed to find eventually if qsize stays accurate
        while(found<0) {
            if(deltas[write_location].time == UINT32_MAX) found = write_location;
            write_location = (write_location + 1) % AMY_DELTA_FIFO_LEN;
        }
        // found a mem location. copy the data in and update the write pointers.
        deltas[found].time = d->time;
        deltas[found].osc = d->osc;
        deltas[found].param = d->param;
        deltas[found].data.i = d->data.i;  // Copying uint32_t of union will copy float value if that's where it is.
        amy_global.next_delta_write = write_location;
        amy_global.delta_qsize++;

        // now insert it into the sorted list for fast playback
        struct delta **pptr = &amy_global.delta_start;
        while(d->time >= (*pptr)->time)
            pptr = &(*pptr)->next;
        deltas[found].next = *pptr;
        *pptr = &deltas[found];
    } else {
        // if there's no room in the queue, just skip the message
        // todo -- report this somehow?
        fprintf(stderr, "AMY queue is full\n");
    }
#if defined ESP_PLATFORM  && !defined ARDUINO
    xSemaphoreGive( xQueueSemaphore );
#elif defined _POSIX_THREADS
    pthread_mutex_unlock(&amy_queue_lock);
#endif
    AMY_PROFILE_STOP(ADD_DELTA_TO_QUEUE)

}

// For people to call when they don't know base_osc or don't care
void amy_add_event(struct event *e) {
    amy_add_event_internal(e, 0);
}

void amy_add_event_internal(struct event *e, uint16_t base_osc) {
    amy_parse_event_to_deltas(e, base_osc, add_delta_to_queue, NULL);
}

#define EVENT_TO_DELTA_F(FIELD, FLAG) if(AMY_IS_SET(e->FIELD)) { d.param=FLAG; d.data.f = e->FIELD; callback(&d, user_data); }
#define EVENT_TO_DELTA_I(FIELD, FLAG) if(AMY_IS_SET(e->FIELD)) { d.param=FLAG; d.data.i = e->FIELD; callback(&d, user_data); }
#define EVENT_TO_DELTA_WITH_BASEOSC(FIELD, FLAG)    if(AMY_IS_SET(e->FIELD)) { d.param=FLAG; d.data.i = e->FIELD + base_osc; ensure_osc_allocd(d.data.i, NULL); callback(&d, user_data);}
#define EVENT_TO_DELTA_LOG(FIELD, FLAG)             if(AMY_IS_SET(e->FIELD)) { d.param=FLAG; d.data.f = log2f(e->FIELD); callback(&d, user_data);}
#define EVENT_TO_DELTA_COEFS(FIELD, FLAG)  \
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) \
        EVENT_TO_DELTA_F(FIELD[i], FLAG + i)
// Const freq coef is in Hz, rest are linear.
#define EVENT_TO_DELTA_FREQ_COEFS(FIELD, FLAG) \
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) {      \
        if (AMY_IS_SET(e->FIELD[i]))  {              \
            d.param = FLAG + i;  \
            if (i == COEF_CONST)  \
                d.data.f = logfreq_of_freq(e->FIELD[i]);  \
            else \
                d.data.f = e->FIELD[i]; \
            callback(&d, user_data); \
        }    \
    }

// Add a API facing event, convert into delta directly
void amy_parse_event_to_deltas(struct event *e, uint16_t base_osc, void (*callback)(struct delta *d, void*user_data), void*user_data ) {
    AMY_PROFILE_START(AMY_ADD_DELTA)
    struct delta d;

    // Synth defaults if not set, these are required for the delta struct
    d.time = e->time;
    d.osc = e->osc;
    if(AMY_IS_UNSET(e->osc)) { d.osc = 0; } 
    if(AMY_IS_UNSET(e->time)) { d.time = 0; } 

    // First, adapt the osc in this event with base_osc offsets for voices
    d.osc += base_osc;

    // Ensure this osc has its synthinfo allocated.
    ensure_osc_allocd(d.osc, NULL);

    // Voices / patches gets set up here 
    // you must set both voices & load_patch together to load a patch 
    if (e->voices[0] != 0 || AMY_IS_SET(e->synth)) {
        if (AMY_IS_SET(e->patch_number) || AMY_IS_SET(e->num_voices)) {
            patches_load_patch(e);
        }
        patches_event_has_voices(e, callback, user_data);
        goto end;
    }

    // Everything else only added to queue if set
    EVENT_TO_DELTA_I(wave, WAVE)
    EVENT_TO_DELTA_I(preset, PRESET)
    EVENT_TO_DELTA_F(midi_note, MIDI_NOTE)
    EVENT_TO_DELTA_COEFS(amp_coefs, AMP)
    EVENT_TO_DELTA_FREQ_COEFS(freq_coefs, FREQ)
    EVENT_TO_DELTA_FREQ_COEFS(filter_freq_coefs, FILTER_FREQ)
    EVENT_TO_DELTA_COEFS(duty_coefs, DUTY)
    EVENT_TO_DELTA_COEFS(pan_coefs, PAN)
    EVENT_TO_DELTA_F(feedback, FEEDBACK)
    EVENT_TO_DELTA_F(phase, PHASE)
    EVENT_TO_DELTA_F(volume, VOLUME)
    EVENT_TO_DELTA_F(pitch_bend, PITCH_BEND)
    EVENT_TO_DELTA_I(latency_ms, LATENCY)
    EVENT_TO_DELTA_F(tempo, TEMPO)
    EVENT_TO_DELTA_LOG(ratio, RATIO)
    EVENT_TO_DELTA_F(resonance, RESONANCE)
    EVENT_TO_DELTA_I(portamento_ms, PORTAMENTO)
    EVENT_TO_DELTA_WITH_BASEOSC(chained_osc, CHAINED_OSC)
    EVENT_TO_DELTA_WITH_BASEOSC(reset_osc, RESET_OSC)
    EVENT_TO_DELTA_WITH_BASEOSC(mod_source, MOD_SOURCE)
    EVENT_TO_DELTA_I(source, EVENT_SOURCE)
    EVENT_TO_DELTA_I(filter_type, FILTER_TYPE)
    EVENT_TO_DELTA_I(algorithm, ALGORITHM)
    EVENT_TO_DELTA_F(eq_l, EQ_L)
    EVENT_TO_DELTA_F(eq_m, EQ_M)
    EVENT_TO_DELTA_F(eq_h, EQ_H)
    EVENT_TO_DELTA_I(eg_type[0], EG0_TYPE)
    EVENT_TO_DELTA_I(eg_type[1], EG1_TYPE)

    if(e->algo_source[0] != 0) {
        struct synthinfo t;
        parse_algorithm_source(&t, e->algo_source);
        for(uint8_t i=0;i<MAX_ALGO_OPS;i++) { 
            d.param = ALGO_SOURCE_START + i;
            if (AMY_IS_SET(t.algo_source[i])) {
                d.data.i = t.algo_source[i] + base_osc;
            } else{
                d.data.i = t.algo_source[i];
            }
            ensure_osc_allocd(d.data.i, NULL);
            callback(&d, user_data); 
        }
    }


    char * bps[MAX_BREAKPOINT_SETS] = {e->bp0, e->bp1};
    for(uint8_t i=0;i<MAX_BREAKPOINT_SETS;i++) {
        // amy_parse_message sets bp_is_set for anything including an empty string,
        // but direct calls to amy_add_event can just put a nonempty string into bp0/1.
        if(AMY_IS_SET(e->bp_is_set[i]) || bps[i][0] != 0) {
            // TODO(dpwe): Modify parse_breakpoints *not* to need an entire synthinfo, but to work with
            // vectors of breakpoint times/values.
            struct synthinfo t;
            uint32_t breakpoint_times[MAX_BREAKPOINTS];
            float breakpoint_values[MAX_BREAKPOINTS];
            t.max_num_breakpoints[i] = MAX_BREAKPOINTS;
            t.breakpoint_times[i] = (uint32_t *)&breakpoint_times;
            t.breakpoint_values[i] = (float *)&breakpoint_values;
            int num_bps = parse_breakpoint(&t, bps[i], i);
            for(uint8_t j = 0; j < num_bps; j++) {
                if(AMY_IS_SET(t.breakpoint_times[i][j])) {
                    d.param = BP_START + (j * 2) + (i * MAX_BREAKPOINTS * 2);
                    d.data.i = t.breakpoint_times[i][j];
                    callback(&d, user_data);
                }
                if(AMY_IS_SET(t.breakpoint_values[i][j])) {
                    d.param = BP_START + (j * 2 + 1) + (i * MAX_BREAKPOINTS * 2);
                    d.data.f = t.breakpoint_values[i][j];
                    callback(&d, user_data);
                }
            }
            // Send an unset value as the last + 1 breakpoint time to indicate the end of the BP set.
            if (num_bps < MAX_BREAKPOINTS) {
                d.param = BP_START + (num_bps * 2) + (i * MAX_BREAKPOINTS * 2);
                d.data.i = AMY_UNSET_VALUE(t.breakpoint_times[0][0]);
                callback(&d, user_data);
            }
        }
    }

    // add this last -- this is a trigger, that if sent alongside osc setup parameters, you want to run after those

    EVENT_TO_DELTA_F(velocity, VELOCITY)
end:
    AMY_PROFILE_STOP(AMY_ADD_DELTA)

}


void reset_osc(uint16_t i ) {
    if (synth[i] == NULL) return;
    // set all the synth state to defaults
    synth[i]->osc = i; // self-reference to make updating oscs easier
    synth[i]->wave = SINE;
    msynth[i]->last_duty = 0.5f;
    AMY_UNSET(synth[i]->preset);
    AMY_UNSET(synth[i]->midi_note);
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i]->amp_coefs[j] = 0;
    synth[i]->amp_coefs[COEF_CONST] = 1.0f;  // Mostly a no-op, but partials_note_on used to want this?
    synth[i]->amp_coefs[COEF_VEL] = 1.0f;
    synth[i]->amp_coefs[COEF_EG0] = 1.0f;
    msynth[i]->amp = 0;  // This matters for wave=PARTIAL, where msynth amp is effectively 1-frame delayed.
    msynth[i]->last_amp = 0;
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i]->logfreq_coefs[j] = 0;
    synth[i]->logfreq_coefs[COEF_NOTE] = 1.0;
    synth[i]->logfreq_coefs[COEF_BEND] = 1.0;
    msynth[i]->logfreq = 0;
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i]->filter_logfreq_coefs[j] = 0;
    msynth[i]->filter_logfreq = 0;
    AMY_UNSET(msynth[i]->last_filter_logfreq);
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i]->duty_coefs[j] = 0;
    synth[i]->duty_coefs[COEF_CONST] = 0.5f;
    msynth[i]->duty = 0.5f;
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i]->pan_coefs[j] = 0;
    synth[i]->pan_coefs[COEF_CONST] = 0.5f;
    msynth[i]->pan = 0.5f;
    synth[i]->feedback = F2S(0); //.996; todo ks feedback is v different from fm feedback
    msynth[i]->feedback = F2S(0); //.996; todo ks feedback is v different from fm feedback
    synth[i]->phase = F2P(0);
    AMY_UNSET(synth[i]->trigger_phase);
    synth[i]->eq_l = 0;
    synth[i]->eq_m = 0;
    synth[i]->eq_h = 0;
    AMY_UNSET(synth[i]->logratio);
    synth[i]->resonance = 0.7f;
    msynth[i]->resonance = 0.7f;
    synth[i]->portamento_alpha = 0;
    synth[i]->velocity = 0;
    synth[i]->step = 0;
    synth[i]->source = EVENT_NONE;
    synth[i]->mod_value = F2S(0);
    synth[i]->substep = 0;
    synth[i]->status = SYNTH_OFF;
    AMY_UNSET(synth[i]->chained_osc);
    AMY_UNSET(synth[i]->mod_source);
    AMY_UNSET(synth[i]->render_clock);
    AMY_UNSET(synth[i]->note_on_clock);
    synth[i]->note_off_clock = 0;  // Used to check that last event seen by note was off.
    AMY_UNSET(synth[i]->zero_amp_clock);
    AMY_UNSET(synth[i]->mod_value_clock);
    synth[i]->filter_type = FILTER_NONE;
    synth[i]->hpf_state[0] = 0;
    synth[i]->hpf_state[1] = 0;
    for(int j = 0; j < 2 * FILT_NUM_DELAYS; ++j) synth[i]->filter_delay[j] = 0;
    synth[i]->last_filt_norm_bits = 0;
    synth[i]->algorithm = 0;
    for(uint8_t j=0;j<MAX_ALGO_OPS;j++) AMY_UNSET(synth[i]->algo_source[j]);
    for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) {
        // max_num_breakpoints describes the alloc for this synthinfo, and is *not* reset.
        for(uint8_t k=0;k<synth[i]->max_num_breakpoints[j];k++) {
            AMY_UNSET(synth[i]->breakpoint_times[j][k]);
            AMY_UNSET(synth[i]->breakpoint_values[j][k]);
        }
        synth[i]->eg_type[j] = ENVELOPE_NORMAL;
    }
    for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) { synth[i]->last_scale[j] = 0; }
    synth[i]->last_two[0] = 0;
    synth[i]->last_two[1] = 0;
    synth[i]->lut = NULL;
}

void amy_reset_oscs() {
    // We reset oscs by freeing them.
    // Include chorus osc (osc=AMY_OSCS)
    for(uint16_t i=0;i<AMY_OSCS+1;i++) free_osc(i);
    // also reset filters and volume
    amy_global.volume = 1.0f;
    amy_global.pitch_bend = 0;
    amy_global.eq[0] = F2S(1.0f);
    amy_global.eq[1] = F2S(1.0f);
    amy_global.eq[2] = F2S(1.0f);
    reset_parametric();
    // Reset chorus oscillator etc.
    if (AMY_HAS_CHORUS) config_chorus(CHORUS_DEFAULT_LEVEL, CHORUS_DEFAULT_MAX_DELAY, CHORUS_DEFAULT_LFO_FREQ, CHORUS_DEFAULT_MOD_DEPTH);
    if (AMY_HAS_REVERB) config_reverb(REVERB_DEFAULT_LEVEL, REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING, REVERB_DEFAULT_XOVER_HZ);
    if (AMY_HAS_ECHO) config_echo(S2F(ECHO_DEFAULT_LEVEL), ECHO_DEFAULT_DELAY_MS, ECHO_DEFAULT_MAX_DELAY_MS, S2F(ECHO_DEFAULT_FEEDBACK), S2F(ECHO_DEFAULT_FILTER_COEF));
    // Reset patches
    patches_reset();
    // Reset instruments (synths)
    instruments_reset();
    // Reset memorypcm
    pcm_unload_all_presets();
}


void amy_deltas_reset() {
    // make a fencepost last delta with no next, time of end-1, and call it start for now, all other deltas get inserted before it
    deltas[0].next = NULL;
    deltas[0].time = UINT32_MAX - 1;
    deltas[0].osc = 0;
    deltas[0].data.i = 0;
    deltas[0].param = NO_PARAM;
    amy_global.next_delta_write = 1;
    amy_global.delta_start = &deltas[0];
    amy_global.delta_qsize = 1;

    // set all the other deltas to empty
    for(uint16_t i=1;i<AMY_DELTA_FIFO_LEN;i++) {
        deltas[i].time = UINT32_MAX;
        deltas[i].next = NULL;
        deltas[i].osc = 0;
        deltas[i].data.i = 0;
        deltas[i].param = NO_PARAM;
    }
}

void alloc_osc(int osc, uint8_t *max_num_breakpoints) {
    uint8_t default_num_breakpoints[MAX_BREAKPOINT_SETS] = {DEFAULT_NUM_BREAKPOINTS, DEFAULT_NUM_BREAKPOINTS};
    if (max_num_breakpoints == NULL) {
        max_num_breakpoints = default_num_breakpoints;
    }
    int total_num_breakpoints = 0;
    for (int i=0; i < MAX_BREAKPOINT_SETS; ++i)  total_num_breakpoints += max_num_breakpoints[i];
    uint8_t *ptr = malloc_caps(sizeof(struct synthinfo) + sizeof(struct mod_synthinfo)
                               + total_num_breakpoints * (sizeof(float) + sizeof(uint32_t)),
                               amy_global.config.ram_caps_events);
    synth[osc] = (struct synthinfo *)ptr;
    msynth[osc] = (struct mod_synthinfo *)(ptr + sizeof(struct synthinfo));
    // Point to the breakpoint sets.
    uint8_t *breakpoint_area = ptr + sizeof(struct synthinfo) + sizeof(struct mod_synthinfo);
    for (int i=0; i < MAX_BREAKPOINT_SETS; ++i) {
        synth[osc]->max_num_breakpoints[i] = max_num_breakpoints[i];
        synth[osc]->breakpoint_times[i] = (uint32_t *)breakpoint_area;
        breakpoint_area +=  max_num_breakpoints[i] * sizeof(uint32_t);  // must be a multiple of 4 bytes
        synth[osc]->breakpoint_values[i] = (float *)breakpoint_area;
        breakpoint_area += sizeof(float) * max_num_breakpoints[i];
    }
    reset_osc(osc);
    //fprintf(stderr, "alloc_osc %d num_breakpoints %d,%d\n", osc, synth[osc]->max_num_breakpoints[0], synth[osc]->max_num_breakpoints[1]);
}

void free_osc(int osc) {
    if (synth[osc] != NULL) {
        free(synth[osc]);
        //fprintf(stderr, "free_osc %d\n", osc);
    }
    synth[osc] = NULL;
    msynth[osc] = NULL;
}

void ensure_osc_allocd(int osc, uint8_t *max_num_breakpoints) {
    if (synth[osc] == NULL) alloc_osc(osc, max_num_breakpoints);
    else if (max_num_breakpoints) {
        bool realloc_needed = false;
        for (int i = 0; i < MAX_BREAKPOINT_SETS; ++i) {
            if (synth[osc]->max_num_breakpoints[i] < max_num_breakpoints[i])
                realloc_needed = true;
        }
        if (realloc_needed) {
            //fprintf(stderr, "realloc for osc %d (breakpoints %d, %d -> %d, %d\n", osc, synth[osc]->max_num_breakpoints[0], synth[osc]->max_num_breakpoints[1], max_num_breakpoints[0], max_num_breakpoints[1]);
            free_osc(osc);
            alloc_osc(osc, max_num_breakpoints);
        }
    }
}


// the synth object keeps held state, whereas deltas are only deltas/changes
int8_t oscs_init() {
    if(amy_global.config.ks_oscs>0)
        ks_init();
    filters_init();
    algo_init();
    patches_init();
    instruments_init();

    if(pcm_samples) {
        pcm_init();
    }
    if(amy_global.config.has_custom) {
        custom_init();
    }
    deltas = (struct delta*)malloc_caps(sizeof(struct delta) * AMY_DELTA_FIFO_LEN, amy_global.config.ram_caps_events);
    // synth and msynth are now pointers to arrays of pointers to dynamically-allocated synth structures.
    synth = (struct synthinfo **) malloc_caps(sizeof(struct synthinfo *) * (AMY_OSCS+1), amy_global.config.ram_caps_synth);
    bzero(synth, sizeof(struct synthinfo *) * (AMY_OSCS+1));
    msynth = (struct mod_synthinfo **) malloc_caps(sizeof(struct mod_synthinfo *) * (AMY_OSCS+1), amy_global.config.ram_caps_synth);
    block = (output_sample_type *) malloc_caps(sizeof(output_sample_type) * AMY_BLOCK_SIZE * AMY_NCHANS, amy_global.config.ram_caps_block);
    amy_in_block = (output_sample_type*)malloc_caps(sizeof(output_sample_type)*AMY_BLOCK_SIZE*AMY_NCHANS, amy_global.config.ram_caps_block);
    amy_external_in_block = (output_sample_type*)malloc_caps(sizeof(output_sample_type)*AMY_BLOCK_SIZE*AMY_NCHANS, amy_global.config.ram_caps_block);
    // set all oscillators to their default values
    amy_reset_oscs();
    // reset the deltas queue
    amy_deltas_reset();

    fbl = (SAMPLE**) malloc_caps(sizeof(SAMPLE*) * AMY_CORES, amy_global.config.ram_caps_fbl); // one per core, just core 0 used off esp32
    per_osc_fb = (SAMPLE**) malloc_caps(sizeof(SAMPLE*) * AMY_CORES, amy_global.config.ram_caps_fbl); // one per core, just core 0 used off esp32

    // clear out both as local mode won't use fbl[1] 
    for(uint16_t core=0;core<AMY_CORES;++core) {
        fbl[core]= (SAMPLE*)malloc_caps(sizeof(SAMPLE) * AMY_BLOCK_SIZE * AMY_NCHANS, amy_global.config.ram_caps_fbl);
        per_osc_fb[core]= (SAMPLE*)malloc_caps(sizeof(SAMPLE) * AMY_BLOCK_SIZE, amy_global.config.ram_caps_fbl);
        for(uint16_t c=0;c<AMY_NCHANS;++c) {
            for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) {
                fbl[core][AMY_BLOCK_SIZE*c + i] = 0;
            }
        }
    }
    // we only alloc delay lines if the chorus is turned on.
    if (chorus_delay_lines == NULL)
        chorus_delay_lines = (delay_line_t **)malloc(sizeof(delay_line_t *) * AMY_NCHANS);
    if(AMY_HAS_CHORUS > 0) {
        for (int c = 0; c < AMY_NCHANS; ++c)  chorus_delay_lines[c] = NULL;
    }
    if (echo_delay_lines == NULL)
        echo_delay_lines = (delay_line_t **)malloc(sizeof(delay_line_t *) * AMY_NCHANS);
    if(AMY_HAS_ECHO > 0) {
        for (int c = 0; c < AMY_NCHANS; ++c)  echo_delay_lines[c] = NULL;
    }

    // Set the optional inputs to 0
    for(uint16_t i=0;i<AMY_BLOCK_SIZE*AMY_NCHANS;i++) {
        amy_in_block[i] = 0;
        amy_external_in_block[i] = 0;
    }

    amy_global.total_blocks = 0;
    //printf("AMY online with %d oscillators, %d block size, %d cores, %d channels, %d pcm samples\n", 
    //    AMY_OSCS, AMY_BLOCK_SIZE, AMY_CORES, AMY_NCHANS, pcm_samples);
    return 0;
}

void print_osc_debug(int i /* osc */, bool show_eg) {
    fprintf(stderr,"osc %d: status %d wave %d mod_source %d velocity %f logratio %f feedback %f filtype %d resonance %f portamento_alpha %f step %f chained %d algo %d source %d,%d,%d,%d,%d,%d  \n",
            i, synth[i]->status, synth[i]->wave, synth[i]->mod_source,
            synth[i]->velocity, synth[i]->logratio, synth[i]->feedback, synth[i]->filter_type, synth[i]->resonance, synth[i]->portamento_alpha, P2F(synth[i]->step), synth[i]->chained_osc,
            synth[i]->algorithm,
            synth[i]->algo_source[0], synth[i]->algo_source[1], synth[i]->algo_source[2], synth[i]->algo_source[3], synth[i]->algo_source[4], synth[i]->algo_source[5] );
    fprintf(stderr, "  amp_coefs: %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i]->amp_coefs[0], synth[i]->amp_coefs[1], synth[i]->amp_coefs[2], synth[i]->amp_coefs[3], synth[i]->amp_coefs[4], synth[i]->amp_coefs[5], synth[i]->amp_coefs[6]);
    fprintf(stderr, "  lfr_coefs: %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i]->logfreq_coefs[0], synth[i]->logfreq_coefs[1], synth[i]->logfreq_coefs[2], synth[i]->logfreq_coefs[3], synth[i]->logfreq_coefs[4], synth[i]->logfreq_coefs[5], synth[i]->logfreq_coefs[6]);
    fprintf(stderr, "  flf_coefs: %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i]->filter_logfreq_coefs[0], synth[i]->filter_logfreq_coefs[1], synth[i]->filter_logfreq_coefs[2], synth[i]->filter_logfreq_coefs[3], synth[i]->filter_logfreq_coefs[4], synth[i]->filter_logfreq_coefs[5], synth[i]->filter_logfreq_coefs[6]);
    fprintf(stderr, "  dut_coefs: %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i]->duty_coefs[0], synth[i]->duty_coefs[1], synth[i]->duty_coefs[2], synth[i]->duty_coefs[3], synth[i]->duty_coefs[4], synth[i]->duty_coefs[5], synth[i]->duty_coefs[6]);
    fprintf(stderr, "  pan_coefs: %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i]->pan_coefs[0], synth[i]->pan_coefs[1], synth[i]->pan_coefs[2], synth[i]->pan_coefs[3], synth[i]->pan_coefs[4], synth[i]->pan_coefs[5], synth[i]->pan_coefs[6]);
    if(show_eg) {
        for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) {
            fprintf(stderr,"  eg%d (type %d): ", j, synth[i]->eg_type[j]);
            for(uint8_t k=0;k<synth[i]->max_num_breakpoints[j];k++) {
                fprintf(stderr,"%" PRIi32 ": %f ", synth[i]->breakpoint_times[j][k], synth[i]->breakpoint_values[j][k]);
            }
            fprintf(stderr,"\n");
        }
        fprintf(stderr,"mod osc %d: amp: %f, logfreq %f duty %f filter_logfreq %f resonance %f fb/bw %f pan %f \n", i, msynth[i]->amp, msynth[i]->logfreq, msynth[i]->duty, msynth[i]->filter_logfreq, msynth[i]->resonance, msynth[i]->feedback, msynth[i]->pan);
    }
}

// types: 0 - show profile if set
//        1 - show profile, queue
//        2 - show profile, queue, osc data
void show_debug(uint8_t type) {
    amy_global.debug_flag = type;
    amy_profiles_print();
    #ifdef ALLES
    esp_show_debug();
    #endif
    if(type>0) {
        struct delta * ptr = amy_global.delta_start;
        uint16_t q = amy_global.delta_qsize;
        if(q > 25) q = 25;
        for(uint16_t i=0;i<q;i++) {
            fprintf(stderr,"%d time %" PRIu32 " osc %d param %d - %f %" PRIu32 "\n", i, ptr->time, ptr->osc, ptr->param, ptr->data.f, ptr->data.i);
            ptr = ptr->next;
        }
    }
    if(type>1) {
        // print out all the osc data
        //printf("global: filter %f resonance %f volume %f bend %f status %d\n", amy_global.filter_freq, amy_global.resonance, amy_global.volume, amy_global.pitch_bend, amy_global.status);
        fprintf(stderr,"global: volume %f bend %f eq: %f %f %f \n", amy_global.volume, amy_global.pitch_bend, S2F(amy_global.eq[0]), S2F(amy_global.eq[1]), S2F(amy_global.eq[2]));
        for(uint16_t i=0;i<10 /* AMY_OSCS */;i++) {
            print_osc_debug(i, (type > 3) /* show_eg */);
        }
        if (type > 4) {
            patches_debug();
        }
        fprintf(stderr, "\n");
    }
}

void oscs_deinit() {
    free(block);
    free(fbl[0]);
    free(per_osc_fb[0]);
    if(AMY_CORES>1){ 
        free(fbl[1]);
        free(per_osc_fb[1]);
    }
    free(fbl);
    for (int i = 0; i < AMY_OSCS; ++i) free_osc(i);
    free(synth);
    free(msynth);
    free(deltas);
    if(amy_global.config.ks_oscs > 0)
        ks_deinit();
    filters_deinit();
    dealloc_chorus_delay_lines();
    dealloc_echo_delay_lines();
}

void audio_in_note_on(uint16_t osc, uint8_t channel) {
    // do i need to do anything here? probably not
}

void external_audio_in_note_on(uint16_t osc, uint8_t channel) {
    // do i need to do anything here? probably not
}

SAMPLE render_audio_in(SAMPLE * buf, uint16_t osc, uint8_t channel) {
    uint16_t c = 0;
    for(uint16_t i=channel;i<AMY_BLOCK_SIZE*AMY_NCHANS;i=i+(AMY_NCHANS)) {
        buf[c++] = SMULR7(L2S(amy_in_block[i]), F2S(msynth[osc]->amp));
    }
    // We have to return something for max_value or else the zero-amp reaper will come along. 
    return F2S(1.0); //max_value;
}

SAMPLE render_external_audio_in(SAMPLE *buf, uint16_t osc, uint8_t channel) {
    uint16_t c = 0;
    for(uint16_t i=channel;i<AMY_BLOCK_SIZE*AMY_NCHANS;i=i+(AMY_NCHANS)) {
        buf[c++] = SMULR7(L2S(amy_external_in_block[i]), F2S(msynth[osc]->amp));
    }
    // We have to return something for max_value or else the zero-amp reaper will come along. 
    return F2S(1.0); //max_value;
}


void osc_note_on(uint16_t osc, float initial_freq) {
    //printf("Note on: osc %d wav %d filter_freq_coefs=%f %f %f %f %f %f\n", osc, synth[osc]->wave, 
    //       synth[osc]->filter_logfreq_coefs[0], synth[osc]->filter_logfreq_coefs[1], synth[osc]->filter_logfreq_coefs[2],
    //       synth[osc]->filter_logfreq_coefs[3], synth[osc]->filter_logfreq_coefs[4], synth[osc]->filter_logfreq_coefs[5]);
            // take care of fm & ks first -- no special treatment for bp/mod
    if(amy_global.config.ks_oscs) if(synth[osc]->wave==KS) ks_note_on(osc);
    if(synth[osc]->wave==SINE) sine_note_on(osc, initial_freq);
    if(synth[osc]->wave==SAW_DOWN) saw_down_note_on(osc, initial_freq);
    if(synth[osc]->wave==SAW_UP) saw_up_note_on(osc, initial_freq);
    if(synth[osc]->wave==TRIANGLE) triangle_note_on(osc, initial_freq);
    if(synth[osc]->wave==PULSE) pulse_note_on(osc, initial_freq);
    if(synth[osc]->wave==PCM) pcm_note_on(osc);
    if(synth[osc]->wave==ALGO) algo_note_on(osc, initial_freq);
    if(synth[osc]->wave==NOISE) noise_note_on(osc);
    if(synth[osc]->wave==AUDIO_IN0) audio_in_note_on(osc, 0);
    if(synth[osc]->wave==AUDIO_IN1) audio_in_note_on(osc, 1);
    if(synth[osc]->wave==AUDIO_EXT0) external_audio_in_note_on(osc, 0);
    if(synth[osc]->wave==AUDIO_EXT1) external_audio_in_note_on(osc, 1);
    if(synth[osc]->wave==MIDI) {
        amy_send_midi_note_on(osc);
    }
    if(amy_global.config.has_partials) {
        if(synth[osc]->wave==PARTIALS || synth[osc]->wave==BYO_PARTIALS) partials_note_on(osc);
        if(synth[osc]->wave==INTERP_PARTIALS) interp_partials_note_on(osc);
    }
    if(amy_global.config.has_custom) {
        if(synth[osc]->wave==CUSTOM) custom_note_on(osc, initial_freq);
    }
}

int chained_osc_would_cause_loop(uint16_t osc, uint16_t chained_osc) {
    // Check to see if chaining this osc would cause a loop.
    uint16_t next_osc = chained_osc;
    do {
        if (next_osc == osc) {
            fprintf(stderr, "chaining osc %d to osc %d would cause loop.\n",
                    chained_osc, osc);
            return true;
        }
        next_osc = synth[next_osc]->chained_osc;
    } while(AMY_IS_SET(next_osc));
    return false;
}

float portamento_ms_to_alpha(uint16_t portamento_ms) {
    return 1.0f  - 1.0f / (1 + portamento_ms * AMY_SAMPLE_RATE / 1000 / AMY_BLOCK_SIZE);
}

#define DELTA_TO_SYNTH_I(FLAG, FIELD)  if (d->param == FLAG) synth[d->osc]->FIELD = d->data.i;
#define DELTA_TO_SYNTH_F(FLAG, FIELD)  if (d->param == FLAG) synth[d->osc]->FIELD = d->data.f;
#define DELTA_TO_COEFS(FLAG, FIELD) \
    if (PARAM_IS_COMBO_COEF(d->param, FLAG)) \
        synth[d->osc]->FIELD[d->param - FLAG] = d->data.f;

// play an delta, now -- tell the audio loop to start making noise
void play_delta(struct delta *d) {
    AMY_PROFILE_START(PLAY_DELTA)
    //fprintf(stderr,"play_delta: time %d osc %d param %d val 0x%x, qsize %d\n", amy_global.total_blocks, d->osc, d->param, d->data.i, global.delta_qsize);
    //uint8_t trig=0;
    // todo: delta-only side effect, remove
    if(d->param == MIDI_NOTE) {
        synth[d->osc]->midi_note = d->data.f;
        // Midi note and Velocity are propagated to chained_osc.
        if (AMY_IS_SET(synth[d->osc]->chained_osc)) {
            d->osc = synth[d->osc]->chained_osc;
            // Recurse with the new osc.  We have to recurse rather than directly setting so that a complete chain of recursion will work.
            play_delta(d);
        }
    }
    if(d->param == WAVE) {
        synth[d->osc]->wave = d->data.i;
        // todo: delta-only side effect, remove
        // we do this because we need to set up LUTs for FM oscs. it's a TODO to make this cleaner
        if(synth[d->osc]->wave == SINE) {
            sine_note_on(d->osc, freq_of_logfreq(synth[d->osc]->logfreq_coefs[COEF_CONST]));
        }
    }
    DELTA_TO_SYNTH_F(FEEDBACK, feedback)
    DELTA_TO_SYNTH_F(RATIO, logratio)
    DELTA_TO_SYNTH_F(RESONANCE, resonance)
    DELTA_TO_SYNTH_I(FILTER_TYPE, filter_type)
    DELTA_TO_SYNTH_I(EVENT_SOURCE, source)
    DELTA_TO_SYNTH_I(EG0_TYPE, eg_type[0])
    DELTA_TO_SYNTH_I(EG1_TYPE, eg_type[1])
    // For now, if the wave type is BYO_PARTIALS, negate the patch number (which is also num_partials) and treat like regular PARTIALS - partials_note_on knows what to do.
    if (d->param == PRESET) synth[d->osc]->preset = ((synth[d->osc]->wave == BYO_PARTIALS) ? -1 : 1) * (uint16_t)d->data.i;
    if (d->param == PORTAMENTO) synth[d->osc]->portamento_alpha = portamento_ms_to_alpha(d->data.i);
    if (d->param == PHASE) synth[d->osc]->trigger_phase = F2P(d->data.f);

    DELTA_TO_COEFS(AMP, amp_coefs)
    DELTA_TO_COEFS(FREQ, logfreq_coefs)
    DELTA_TO_COEFS(FILTER_FREQ, filter_logfreq_coefs)
    DELTA_TO_COEFS(DUTY, duty_coefs)
    DELTA_TO_COEFS(PAN, pan_coefs)

    // todo, i really should clean this up
    if (PARAM_IS_BP_COEF(d->param)) {
        uint8_t pos = d->param - BP_START;
        uint8_t bp_set = 0;
        while(pos >= (MAX_BREAKPOINTS * 2)) { ++bp_set; pos -= (MAX_BREAKPOINTS * 2); }
        if(pos % 2 == 0) {
            synth[d->osc]->breakpoint_times[bp_set][pos / 2] = d->data.i;
        } else {
            synth[d->osc]->breakpoint_values[bp_set][(pos-1) / 2] = d->data.f;
        }
        //trig=1;
    }
    if (PARAM_IS_COMBO_COEF(d->param, AMP) ||
        PARAM_IS_BP_COEF(d->param)) {
        // Changes to Amp/filter/EGs can potentially make a silence-suspended note come back.
        // Revive the note if it hasn't seen a note_off since the last note_on.
        if (synth[d->osc]->status == SYNTH_AUDIBLE_SUSPENDED && AMY_IS_UNSET(synth[d->osc]->note_off_clock))
            synth[d->osc]->status = SYNTH_AUDIBLE;
    }
    if(d->param == CHAINED_OSC) {
        int chained_osc = d->data.i;
        if (chained_osc >=0 && chained_osc < AMY_OSCS &&
            !chained_osc_would_cause_loop(d->osc, chained_osc))
            synth[d->osc]->chained_osc = chained_osc;
        else
            AMY_UNSET(synth[d->osc]->chained_osc);
    }
    if(d->param == RESET_OSC) { 
        // Remember that RESET_AMY, RESET_TIMEBASE and RESET_EVENTS happens immediately in the parse, so we don't deal with it here.
        if(d->data.i & RESET_ALL_OSCS) { 
            amy_reset_oscs(); 
        }
        if(d->data.i & RESET_SEQUENCER) { 
            sequencer_reset(); 
        }
        if(d->data.i & RESET_ALL_NOTES) { 
            all_notes_off(); 
        }
        if(d->data.i < AMY_OSCS+1) { 
            reset_osc(*(uint32_t *)&d->data); 
        } 
    }
    if(d->param == MOD_SOURCE) {
        uint16_t mod_osc = d->data.i;
        synth[d->osc]->mod_source = mod_osc;
        // NOTE: These are delta-only side effects.  A purist would strive to remove them.
        // When an oscillator is named as a modulator, we change its state.
        synth[mod_osc]->status = SYNTH_IS_MOD_SOURCE;
        // No longer record this osc in note_off state.
        AMY_UNSET(synth[mod_osc]->note_off_clock);
        // Remove default amplitude dependence on velocity when an oscillator is made a modulator.
        synth[mod_osc]->amp_coefs[COEF_VEL] = 0;
    }
    if(d->param == ALGORITHM) {
        synth[d->osc]->algorithm = d->data.i;
        // This is a DX7-style control osc; ensure eg_types are set
        // but only when ALGO is specified, so user can override later if desired->
        synth[d->osc]->eg_type[0] = ENVELOPE_DX7;
        synth[d->osc]->eg_type[1] = ENVELOPE_TRUE_EXPONENTIAL;
    }
    if(d->param >= ALGO_SOURCE_START && d->param < ALGO_SOURCE_END) {
        uint16_t which_source = d->param - ALGO_SOURCE_START;
        synth[d->osc]->algo_source[which_source] = d->data.i;
        if(AMY_IS_SET(synth[d->osc]->algo_source[which_source])) {
            int osc = synth[d->osc]->algo_source[which_source];
            synth[osc]->status = SYNTH_IS_ALGO_SOURCE;
            // Configure the amp envelope appropriately, just once when named as an algo_source.
            synth[osc]->eg_type[0] = ENVELOPE_DX7;
        }
    }
    // for global changes, just make the change, no need to update the per-osc synth
    if(d->param == VOLUME) amy_global.volume = d->data.f;
    if(d->param == PITCH_BEND) amy_global.pitch_bend = d->data.f;
    if(d->param == LATENCY) amy_global.latency_ms = d->data.i;
    if(d->param == TEMPO) { amy_global.tempo = d->data.f; sequencer_recompute(); }
    if(d->param == EQ_L) amy_global.eq[0] = F2S(powf(10, d->data.f / 20.0));
    if(d->param == EQ_M) amy_global.eq[1] = F2S(powf(10, d->data.f / 20.0));
    if(d->param == EQ_H) amy_global.eq[2] = F2S(powf(10, d->data.f / 20.0));

    // triggers / envelopes
    // the only way a sound is made is if velocity (note on) is >0.
    // Ignore velocity deltas if we've already received one this frame.  This may be due to a loop in chained_oscs.
    if(d->param == VELOCITY) {
        if (d->data.f > 0) { // new note on (even if something is already playing on this osc)
            synth[d->osc]->velocity = d->data.f;
            synth[d->osc]->status = SYNTH_AUDIBLE;
            // an osc came in with a note on.
            // start the bp clock
            synth[d->osc]->note_on_clock = amy_global.total_blocks*AMY_BLOCK_SIZE; //esp_timer_get_time() / 1000;

            // if there was a filter active for this voice, reset it
            if(synth[d->osc]->filter_type != FILTER_NONE) reset_filter(d->osc);
            // We no longer reset the phase here; instead, we reset phase when an oscillator falls silent.
            // But if a trigger_phase is set, use that.
            if (AMY_IS_SET(synth[d->osc]->trigger_phase))
                synth[d->osc]->phase = synth[d->osc]->trigger_phase;

            // restart the waveforms
            // Guess at the initial frequency depending only on const & note.  Envelopes not "developed" yet.
            float initial_logfreq = synth[d->osc]->logfreq_coefs[COEF_CONST];
            if (AMY_IS_SET(synth[d->osc]->midi_note)) {
                // synth[d->osc]->logfreq_coefs[COEF_CONST] = 0;
                initial_logfreq += synth[d->osc]->logfreq_coefs[COEF_NOTE] * logfreq_for_midi_note(synth[d->osc]->midi_note);
            }
            // If we're coming out of note-off, set the freq history for portamento.
            //if (AMY_IS_SET(synth[d->osc]->note_off_clock))
            //    msynth[d->osc]->last_logfreq = initial_logfreq;
            // Now we've tested that, we can reset note-off clocks.
            AMY_UNSET(synth[d->osc]->note_off_clock);  // Most recent note event is not note-off.
            AMY_UNSET(synth[d->osc]->zero_amp_clock);

            float initial_freq = freq_of_logfreq(initial_logfreq);
            osc_note_on(d->osc, initial_freq);
            // trigger the mod source, if we have one
            if(AMY_IS_SET(synth[d->osc]->mod_source)) {
                if (AMY_IS_SET(synth[synth[d->osc]->mod_source]->trigger_phase))
                    synth[synth[d->osc]->mod_source]->phase = synth[synth[d->osc]->mod_source]->trigger_phase;

                synth[synth[d->osc]->mod_source]->note_on_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;  // Need a note_on_clock to have envelope work correctly.
                if(synth[synth[d->osc]->mod_source]->wave==SINE) sine_mod_trigger(synth[d->osc]->mod_source);
                if(synth[synth[d->osc]->mod_source]->wave==SAW_DOWN) saw_up_mod_trigger(synth[d->osc]->mod_source);
                if(synth[synth[d->osc]->mod_source]->wave==SAW_UP) saw_down_mod_trigger(synth[d->osc]->mod_source);
                if(synth[synth[d->osc]->mod_source]->wave==TRIANGLE) triangle_mod_trigger(synth[d->osc]->mod_source);
                if(synth[synth[d->osc]->mod_source]->wave==PULSE) pulse_mod_trigger(synth[d->osc]->mod_source);
                if(synth[synth[d->osc]->mod_source]->wave==PCM) pcm_mod_trigger(synth[d->osc]->mod_source);
                if(synth[synth[d->osc]->mod_source]->wave==CUSTOM) custom_mod_trigger(synth[d->osc]->mod_source);
            }
        } else if(synth[d->osc]->velocity > 0 && d->data.f == 0) { // new note off
            // DON'T clear velocity, we still need to reference it in decay.
            //synth[d->osc]->velocity = 0;
            if(synth[d->osc]->wave==KS) ks_note_off(d->osc);
            else if(synth[d->osc]->wave==ALGO)  algo_note_off(d->osc);
            else if(synth[d->osc]->wave==PCM) pcm_note_off(d->osc);
            else if(synth[d->osc]->wave==MIDI) amy_send_midi_note_off(d->osc);
            else if(synth[d->osc]->wave==CUSTOM) custom_note_off(d->osc);
            else if(synth[d->osc]->wave==PARTIALS || synth[d->osc]->wave==BYO_PARTIALS || synth[d->osc]->wave==INTERP_PARTIALS) {
                AMY_UNSET(synth[d->osc]->note_on_clock);
                synth[d->osc]->note_off_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
                if(synth[d->osc]->wave==INTERP_PARTIALS) interp_partials_note_off(d->osc);
                else partials_note_off(d->osc);
            } else {
                // osc note off, start release
                // For now, note_off_clock signals note off BUT ONLY IF IT'S NOT KS, ALGO, PARTIAL, PARTIALS, PCM, or CUSTOM.
                // I'm not crazy about this, but if we apply it in those cases, the default bp0 amp envelope immediately zeros-out
                // those waves on note-off.
                AMY_UNSET(synth[d->osc]->note_on_clock);
                if (AMY_IS_UNSET(synth[d->osc]->note_off_clock)) {
                    // Only set the note_off_clock (start of release) if we don't already have one.
                    synth[d->osc]->note_off_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
                }
            }
        }
        // Now maybe propagate the velocity delta to the chained osc.
        if (AMY_IS_SET(synth[d->osc]->chained_osc)) {
            d->osc = synth[d->osc]->chained_osc;
            // Recurse with the new osc.
            play_delta(d);
        }
    }
    AMY_PROFILE_STOP(PLAY_DELTA)
}

float combine_controls(float *controls, float *coefs) {
    float result = 0;
    for (int i = 0; i < NUM_COMBO_COEFS; ++i)
        result += coefs[i] * controls[i];
    return result;
}

float combine_controls_mult(float *controls, float *coefs) {
    float result = 1.0;
    for (int i = 0; i < NUM_COMBO_COEFS; ++i)
        if (coefs[i] != 0)
            // COEF_MOD and COEF_BEND are applied as amp *= (1 + COEF * CONTROL).
            result *= ((i > COEF_EG1)? 1.0f : 0) + coefs[i] * controls[i];
    return result;
}

// apply an mod & bp, if any, to the osc
#ifdef __EMSCRIPTEN__
#include "emscripten/webaudio.h"
extern float amy_web_cv_1;
extern float amy_web_cv_2;
#endif
void hold_and_modify(uint16_t osc) {
    AMY_PROFILE_START(HOLD_AND_MODIFY)
    float ctrl_inputs[NUM_COMBO_COEFS];
    ctrl_inputs[COEF_CONST] = 1.0f;
    ctrl_inputs[COEF_NOTE] = (AMY_IS_SET(synth[osc]->midi_note)) ? logfreq_for_midi_note(synth[osc]->midi_note) : 0;
    ctrl_inputs[COEF_VEL] = synth[osc]->velocity;
    ctrl_inputs[COEF_EG0] = S2F(compute_breakpoint_scale(osc, 0, 0));
    ctrl_inputs[COEF_EG1] = S2F(compute_breakpoint_scale(osc, 1, 0));
    ctrl_inputs[COEF_MOD] = S2F(compute_mod_scale(osc));
    ctrl_inputs[COEF_BEND] = amy_global.pitch_bend;
    if(amy_external_coef_hook != NULL) {
        ctrl_inputs[COEF_EXT0] = amy_external_coef_hook(0);
        ctrl_inputs[COEF_EXT1] = amy_external_coef_hook(1);
    } else {
        #ifdef __EMSCRIPTEN__
        ctrl_inputs[COEF_EXT0] = amy_web_cv_1;
        ctrl_inputs[COEF_EXT1] = amy_web_cv_2;
        #else
        ctrl_inputs[COEF_EXT0] = 0;
        ctrl_inputs[COEF_EXT1] = 0;        
        #endif
    }

    msynth[osc]->last_pan = msynth[osc]->pan;

    // copy all the modifier variables
    float logfreq = combine_controls(ctrl_inputs, synth[osc]->logfreq_coefs);
    if (synth[osc]->portamento_alpha == 0) {
        msynth[osc]->logfreq = logfreq;
    } else {
        msynth[osc]->logfreq = logfreq + synth[osc]->portamento_alpha * (msynth[osc]->last_logfreq - logfreq);
    }
    msynth[osc]->last_logfreq = msynth[osc]->logfreq;
    float filter_logfreq = combine_controls(ctrl_inputs, synth[osc]->filter_logfreq_coefs);
    #define MIN_FILTER_LOGFREQ -2.0  // LPF cutoff cannot go below w = 0.01 rad/samp in filters.c = 72 Hz, so clip it here at ~65 Hz.
    if (filter_logfreq < MIN_FILTER_LOGFREQ)  filter_logfreq = MIN_FILTER_LOGFREQ;
    if (AMY_IS_SET(msynth[osc]->last_filter_logfreq)) {
        #define MAX_DELTA_FILTER_LOGFREQ_DOWN 2.0
        float last_logfreq = msynth[osc]->last_filter_logfreq;
        if (filter_logfreq < last_logfreq - MAX_DELTA_FILTER_LOGFREQ_DOWN) {
            // Filter cutoff downward slew-rate limit.
            filter_logfreq = last_logfreq - MAX_DELTA_FILTER_LOGFREQ_DOWN;
        }
    }
    msynth[osc]->filter_logfreq = filter_logfreq;
    msynth[osc]->last_filter_logfreq = filter_logfreq;
    msynth[osc]->duty = combine_controls(ctrl_inputs, synth[osc]->duty_coefs);
    msynth[osc]->pan = combine_controls(ctrl_inputs, synth[osc]->pan_coefs);
    // amp is a special case - coeffs apply in log domain.
    // Also, we advance one frame by writing both last_amp and amp (=next amp)
    // *Except* for partials, where we allow one frame of ramp-on.
    float new_amp = combine_controls_mult(ctrl_inputs, synth[osc]->amp_coefs);
    if (synth[osc]->wave == PARTIAL) {
        msynth[osc]->last_amp = msynth[osc]->amp;
        msynth[osc]->amp = new_amp;
    } else {
        // Prevent hard-off on transition to release by updating last_amp only for nonzero new_last_amp.
        if (new_amp > 0) {
            msynth[osc]->last_amp = new_amp;
        }
        // Advance the envelopes to the beginning of the next frame.
        ctrl_inputs[COEF_EG0] = S2F(compute_breakpoint_scale(osc, 0, AMY_BLOCK_SIZE));
        ctrl_inputs[COEF_EG1] = S2F(compute_breakpoint_scale(osc, 1, AMY_BLOCK_SIZE));
        msynth[osc]->amp = combine_controls_mult(ctrl_inputs, synth[osc]->amp_coefs);
        if (msynth[osc]->amp <= 0.001)  msynth[osc]->amp = 0;
    }
    // synth[osc]->feedback is copied to msynth in pcm_note_on, then used to track note-off for looping PCM.
    // For PCM, don't re-copy it every loop, or we'd lose track of that flag.  (This means you can't change feedback mid-playback for PCM).
    // we also check for custom, for tulips' memorypcm 
    if (synth[osc]->wave != PCM && synth[osc]->wave != CUSTOM)  msynth[osc]->feedback = synth[osc]->feedback;
    msynth[osc]->resonance = synth[osc]->resonance;

    if (osc == 999) {
        fprintf(stderr, "h&m: time %f osc %d note %f vel %f eg0 %f eg1 %f ampc %.3f %.3f %.3f %.3f %.3f %.3f lfqc %.3f %.3f %.3f %.3f %.3f %.3f amp %f logfreq %f\n",
               amy_global.total_blocks*AMY_BLOCK_SIZE / (float)AMY_SAMPLE_RATE, osc,
               ctrl_inputs[COEF_NOTE], ctrl_inputs[COEF_VEL], ctrl_inputs[COEF_EG0], ctrl_inputs[COEF_EG1],
               synth[osc]->amp_coefs[0], synth[osc]->amp_coefs[1], synth[osc]->amp_coefs[2], synth[osc]->amp_coefs[3], synth[osc]->amp_coefs[4], synth[osc]->amp_coefs[5],
               synth[osc]->logfreq_coefs[0], synth[osc]->logfreq_coefs[1], synth[osc]->logfreq_coefs[2], synth[osc]->logfreq_coefs[3], synth[osc]->logfreq_coefs[4], synth[osc]->logfreq_coefs[5],
               msynth[osc]->amp, msynth[osc]->logfreq);

    }
    AMY_PROFILE_STOP(HOLD_AND_MODIFY)

}

static inline float lgain_of_pan(float pan) {
    if(pan > 1.f)  pan = 1.f;
    if(pan < 0)  pan = 0;
    return dsps_sqrtf_f32_ansi(1.f - pan);
}

static inline float rgain_of_pan(float pan) {
    if(pan > 1.f)  pan = 1.f;
    if(pan < 0)  pan = 0;
    return dsps_sqrtf_f32_ansi(pan);
}


void mix_with_pan(SAMPLE *stereo_dest, SAMPLE *mono_src, float pan_start, float pan_end) {
    AMY_PROFILE_START(MIX_WITH_PAN)
    /* copy a block_size of mono samples into an interleaved stereo buffer, applying pan */
    if(AMY_NCHANS==1) {
        // actually dest is mono, pan is ignored.
        for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) { stereo_dest[i] += mono_src[i]; }
    } else { 
        // stereo
        SAMPLE gain_l = F2S(lgain_of_pan(pan_start));
        SAMPLE gain_r = F2S(rgain_of_pan(pan_start));
        SAMPLE d_gain_l = F2S((lgain_of_pan(pan_end) - lgain_of_pan(pan_start)) / AMY_BLOCK_SIZE);
        SAMPLE d_gain_r = F2S((rgain_of_pan(pan_end) - rgain_of_pan(pan_start)) / AMY_BLOCK_SIZE);
        for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) {
            stereo_dest[i] += MUL8_SS(gain_l, mono_src[i]);
            stereo_dest[AMY_BLOCK_SIZE + i] += MUL8_SS(gain_r, mono_src[i]);
            gain_l += d_gain_l;
            gain_r += d_gain_r;
        }
    }
    AMY_PROFILE_STOP(MIX_WITH_PAN)
}

SAMPLE render_osc_wave(uint16_t osc, uint8_t core, SAMPLE* buf) {
    AMY_PROFILE_START(RENDER_OSC_WAVE)
    // Returns abs max of what it wrote.
    //fprintf(stderr, "+render_osc_wave: t=%ld core=%d buf=0x%lx (%f, %f, %f, %f...) osc=%d osc_t=%ld\n",
    //        amy_global.total_blocks, core, buf, S2F(buf[0]), S2F(buf[1]), S2F(buf[2]), S2F(buf[3]),
    //        osc, synth[osc]->render_clock);
    SAMPLE max_val = 0;
    // Only render if osc has not already been rendered this time step e.g. by chained_osc.
    if (synth[osc]->render_clock != amy_global.total_blocks*AMY_BLOCK_SIZE) {
        synth[osc]->render_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
        // fill buf with next block_size of samples for specified osc.
        hold_and_modify(osc); // apply bp / mod
        if(!(msynth[osc]->amp == 0 && msynth[osc]->last_amp == 0)) {
            if(synth[osc]->wave == NOISE) max_val = render_noise(buf, osc);
            if(synth[osc]->wave == SAW_DOWN) max_val = render_saw_down(buf, osc);
            if(synth[osc]->wave == SAW_UP) max_val = render_saw_up(buf, osc);
            if(synth[osc]->wave == PULSE) max_val = render_pulse(buf, osc);
            if(synth[osc]->wave == TRIANGLE) max_val = render_triangle(buf, osc);
            if(synth[osc]->wave == SINE) max_val = render_sine(buf, osc);
            if(synth[osc]->wave == AUDIO_IN0) max_val = render_audio_in(buf, osc, 0);
            if(synth[osc]->wave == AUDIO_IN1) max_val = render_audio_in(buf, osc, 1);
            if(synth[osc]->wave == AUDIO_EXT0) max_val = render_external_audio_in(buf, osc, 0);
            if(synth[osc]->wave == AUDIO_EXT1) max_val = render_external_audio_in(buf, osc, 1);
            if(synth[osc]->wave == MIDI) max_val = 1;
            if(synth[osc]->wave == KS) {
                if(amy_global.config.ks_oscs) {
                    max_val = render_ks(buf, osc);
                }
            }
            if(pcm_samples)
                if(synth[osc]->wave == PCM) max_val = render_pcm(buf, osc);
            if(synth[osc]->wave == ALGO) max_val = render_algo(buf, osc, core);
            if(amy_global.config.has_partials) {
                if(synth[osc]->wave == PARTIALS || synth[osc]->wave == BYO_PARTIALS || synth[osc]->wave == INTERP_PARTIALS)
                    max_val = render_partials(buf, osc);
            }
        }
        if(amy_global.config.has_custom) {
            if(synth[osc]->wave == CUSTOM) max_val = render_custom(buf, osc);
        }
        if(AMY_IS_SET(synth[osc]->chained_osc)) {
            // Stack oscillators - render next osc into same buffer.
            uint16_t chained_osc = synth[osc]->chained_osc;
            if (synth[chained_osc]->status == SYNTH_AUDIBLE) {  // We have to recheck this since we're bypassing the skip in amy_render.
                SAMPLE new_max_val = render_osc_wave(chained_osc, core, buf);
                if (new_max_val > max_val)  max_val = new_max_val;
            }
        }
        // note: Code transplanted here from hold_and_modify() to distinguish actual zero output
        // from zero-amplitude (but maybe inheriting values from chained_oscs).
        // Stop oscillators if amp is zero for several frames in a row.
        // Note: We can't wait for the note off because we need to turn off PARTIAL oscs when envelopes end, even if no note off.
#define MIN_ZERO_AMP_TIME_SAMPS (10 * AMY_BLOCK_SIZE)
        if(AMY_IS_SET(synth[osc]->zero_amp_clock)) {
            if (max_val > 0) {
                AMY_UNSET(synth[osc]->zero_amp_clock);
            } else {
                if ( (amy_global.total_blocks*AMY_BLOCK_SIZE - synth[osc]->zero_amp_clock) >= MIN_ZERO_AMP_TIME_SAMPS) {
                    //printf("h&m: time %f osc %d OFF\n", amy_global.total_blocks*AMY_BLOCK_SIZE/(float)AMY_SAMPLE_RATE, osc);
                    // Oscillator has fallen silent, stop executing it.
                    synth[osc]->status = SYNTH_AUDIBLE_SUSPENDED;  // It *could* come back...
                    // .. but force it to start at zero phase next time.
                    synth[osc]->phase = 0;
                }
            }
        } else if (max_val == 0) {
            synth[osc]->zero_amp_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
        }
    }
    AMY_PROFILE_STOP(RENDER_OSC_WAVE)
    //fprintf(stderr, "-render_osc_wave: t=%ld core=%d buf=0x%lx (%f, %f, %f, %f...) osc=%d osc_t=%ld\n",
    //    amy_global.total_blocks*AMY_BLOCK_SIZE, core, buf, S2F(buf[0]), S2F(buf[1]), S2F(buf[2]), S2F(buf[3]),
    //    osc, synth[osc]->render_clock);
    return max_val;
}

void amy_render(uint16_t start, uint16_t end, uint8_t core) {
    AMY_PROFILE_START(AMY_RENDER)
    for(uint16_t i=0;i<AMY_BLOCK_SIZE*AMY_NCHANS;i++) { fbl[core][i] = 0; }
    SAMPLE max_max = 0;
    for(uint16_t osc=start; osc<end; osc++) {
        if(synth[osc] != NULL && synth[osc]->status == SYNTH_AUDIBLE) { // skip oscs that are silent or mod sources from playback
            bzero(per_osc_fb[core], AMY_BLOCK_SIZE * sizeof(SAMPLE));
            SAMPLE max_val = render_osc_wave(osc, core, per_osc_fb[core]);
            // check it's not off, just in case. todo, why do i care?
            // apply filter to osc if set
            if(synth[osc]->filter_type != FILTER_NONE)
                max_val = filter_process(per_osc_fb[core], osc, max_val);
            uint8_t handled = 0;
            if(amy_external_render_hook!=NULL) {
                handled = amy_external_render_hook(osc, per_osc_fb[core], AMY_BLOCK_SIZE);
            } else {
                #ifdef __EMSCRIPTEN__
                // TODO -- pass the buffer to a JS shim using the new bytes support, we could use this to visualize CV output
                #endif
            }
            // only mix the audio in if the external hook did not handle it
            if(!handled) mix_with_pan(fbl[core], per_osc_fb[core], msynth[osc]->last_pan, msynth[osc]->pan);
            if (max_val > max_max) max_max = max_val;
        }
    }
    core_max[core] = max_max;

    if (amy_global.debug_flag) {
        amy_global.debug_flag = 0;  // Only do this once each time debug_flag is set.
        SAMPLE smax = scan_max(fbl[core], AMY_BLOCK_SIZE);
        fprintf(stderr, "time %" PRIu32 " core %d max_max=%.3f post-eq max=%.3f\n", amy_global.total_blocks*AMY_BLOCK_SIZE, core, S2F(max_max), S2F(smax));
    }

    AMY_PROFILE_STOP(AMY_RENDER)

}

// on all platforms, sysclock is based on total samples played, using audio out (i2s or etc) as system clock
uint32_t amy_sysclock() {
    // Time is returned in integer microseconds.  uint32_t rollover is 49.7 days.
    return (uint32_t)((amy_global.total_blocks * AMY_BLOCK_SIZE / (float)AMY_SAMPLE_RATE) * 1000);
}


void amy_increase_volume() {
    amy_global.volume += 0.5f;
    if(amy_global.volume > MAX_VOLUME) amy_global.volume = MAX_VOLUME;    
}

void amy_decrease_volume() {
    amy_global.volume -= 0.5f;
    if(amy_global.volume < 0) amy_global.volume = 0;    
}

void amy_set_pitch_bend(float value) {
    amy_global.pitch_bend = value;
}

// this takes scheduled deltas and plays them at the right time
void amy_prepare_buffer() {
    AMY_PROFILE_START(AMY_PREPARE_BUFFER)
    // check to see which sounds to play
    uint32_t sysclock = amy_sysclock();

#if defined ESP_PLATFORM && !defined ARDUINO
    // put a mutex around this so that the event parser doesn't touch these while i'm running
    xSemaphoreTake(xQueueSemaphore, portMAX_DELAY);
#elif defined _POSIX_THREADS
    pthread_mutex_lock(&amy_queue_lock);
#endif

    // find any deltas that need to be played from the (in-order) queue
    struct delta *delta_start = amy_global.delta_start;
    if (amy_global.delta_qsize > 1 && delta_start->time == UINT32_MAX) {
        fprintf(stderr, "WARN: time=UINT32_MAX found at qsize=%d\n", amy_global.delta_qsize);
    }
    while(sysclock >= delta_start->time) {
        play_delta(delta_start);
        delta_start->time = UINT32_MAX;
        amy_global.delta_qsize--;
        delta_start = delta_start->next;
    }
    amy_global.delta_start = delta_start;

#if defined ESP_PLATFORM && !defined ARDUINO
    // give the mutex back
    xSemaphoreGive(xQueueSemaphore);
#elif defined _POSIX_THREADS
    pthread_mutex_unlock(&amy_queue_lock);
#endif

    if(AMY_HAS_CHORUS==1) {
        ensure_osc_allocd(CHORUS_MOD_SOURCE, NULL);
        hold_and_modify(CHORUS_MOD_SOURCE);
        if(amy_global.chorus.level!=0)  {
            bzero(delay_mod, AMY_BLOCK_SIZE * sizeof(SAMPLE));
            render_osc_wave(CHORUS_MOD_SOURCE, 0 /* core */, delay_mod);
        }
    }
    AMY_PROFILE_STOP(AMY_PREPARE_BUFFER)

}



output_sample_type * amy_simple_fill_buffer() {
    amy_prepare_buffer();
    amy_render(0, AMY_OSCS, 0);
    return amy_fill_buffer();
}


// get AUDIO_IN0 and AUDIO_IN1
void amy_get_input_buffer(output_sample_type * samples) {
    for(uint16_t i=0;i<AMY_BLOCK_SIZE*AMY_NCHANS;i++) samples[i] = amy_in_block[i];
}

// set AUDIO_EXT0 and AUDIO_EXT1
void amy_set_external_input_buffer(output_sample_type * samples) {
    for(uint16_t i=0;i<AMY_BLOCK_SIZE*AMY_NCHANS;i++) amy_external_in_block[i] = samples[i];
}


// called by the audio render loop to alert JS (and then python) that a block has been rendered
void amy_block_processed(void) {

#ifdef __EMSCRIPTEN__
    EM_ASM({
        if(typeof amy_block_processed_js_hook === 'function') {
            amy_block_processed_js_hook();
        }
    });
#else
    if(amy_external_block_done_hook != NULL) {
        amy_external_block_done_hook();
    }
#endif
}


int16_t * amy_fill_buffer() {
    AMY_PROFILE_START(AMY_FILL_BUFFER)
    #ifdef __EMSCRIPTEN__
    // post a message to the main thread of the audioworklet (amy main, in this case) that a block has been finished
    //emscripten_audio_worklet_post_function_v(0, amy_block_processed);
    #else
    amy_block_processed();
    #endif
    // mix results from both cores.
    SAMPLE max_val = core_max[0];
    if(AMY_CORES==2) {
        for (int16_t i=0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)  fbl[0][i] += fbl[1][i];
        if (core_max[1] > max_val)  max_val = core_max[1];
    }
    // Apply global processing only if there is some signal.
    if (max_val > 0) {
        // apply the eq filters if there is some signal and EQ is non-default.
        if (amy_global.eq[0] != F2S(1.0f) || amy_global.eq[1] != F2S(1.0f) || amy_global.eq[2] != F2S(1.0f)) {
            parametric_eq_process(fbl[0]);
        }

        if(AMY_HAS_CHORUS==1) {
            // apply chorus.
            if(amy_global.chorus.level > 0 && chorus_delay_lines[0] != NULL) {
                // apply time-varying delays to both chans.
                // delay_mod_val, the modulated delay amount, is set up before calling render_*.
                SAMPLE scale = F2S(1.0f);
                for (int16_t c=0; c < AMY_NCHANS; ++c) {
                    apply_variable_delay(fbl[0] + c * AMY_BLOCK_SIZE, chorus_delay_lines[c],
                                         delay_mod, scale, amy_global.chorus.level, 0);
                    // flip delay direction for alternating channels.
                    scale = -scale;
                }
            }
        }
    }
    if (AMY_HAS_ECHO == 1) {
        // Apply echo.
        if (amy_global.echo.level > 0 && echo_delay_lines[0] != NULL ) {
            for (int16_t c=0; c < AMY_NCHANS; ++c) {
                apply_fixed_delay(fbl[0] + c * AMY_BLOCK_SIZE, echo_delay_lines[c], amy_global.echo.delay_samples, amy_global.echo.level, amy_global.echo.feedback, amy_global.echo.filter_coef);
            }
        }
    }
    if(AMY_HAS_REVERB) {
        // apply reverb.
        if(amy_global.reverb.level > 0) {
            if(AMY_NCHANS == 1) {
                stereo_reverb(fbl[0], NULL, fbl[0], NULL, AMY_BLOCK_SIZE, amy_global.reverb.level);
            } else {
                stereo_reverb(fbl[0], fbl[0] + AMY_BLOCK_SIZE, fbl[0], fbl[0] + AMY_BLOCK_SIZE, AMY_BLOCK_SIZE, amy_global.reverb.level);
            }
        }
    }
    // global volume is supposed to max out at 10, so scale by 0.1.
    SAMPLE volume_scale = MUL4_SS(F2S(0.1f), F2S(amy_global.volume));
    for(int16_t i=0; i < AMY_BLOCK_SIZE; ++i) {
        for (int16_t c=0; c < AMY_NCHANS; ++c) {

            // Convert the mixed sample into the int16 range, applying overall gain.
            SAMPLE fsample = MUL8_SS(volume_scale, fbl[0][i + c * AMY_BLOCK_SIZE]);

            // One-pole high-pass filter to remove large low-frequency excursions from
            // some FM patches. b = [1 -1]; a = [1 -0.995]
            //SAMPLE new_state = fsample + SMULR6(F2S(0.995f), amy_global.hpf_state);  // High-output-range, rounded MUL is critical here.
#ifdef AMY_HPF_OUTPUT
            SAMPLE new_state = fsample + amy_global.hpf_state
                - SHIFTR(amy_global.hpf_state + SHIFTR(F2S(1.0), 16), 8);  // i.e. 0.9961*hpf_state
            fsample = new_state - amy_global.hpf_state;
            amy_global.hpf_state = new_state;
#endif

            // soft clipping.
            int positive = 1;
            if (fsample < 0) positive = 0;

            int32_t uintval;
            if (positive) {  // avoid fabs()
                uintval = S2L(fsample);
            } else {
                uintval = S2L(-fsample);
            }
            if (uintval >= FIRST_NONLIN) {
                if (uintval >= FIRST_HARDCLIP) {
                    uintval = SAMPLE_MAX;
                } else {
                    uintval = clipping_lookup_table[uintval - FIRST_NONLIN];
                }
            }
            int16_t sample;

            // TODO -- the esp stuff here could sit outside of AMY
            // For some reason, have to drop a bit to stop hard wrapping on esp?
            #ifdef ESP_PLATFORM
                uintval >>= 1;
            #endif
            if (positive) {
              sample = uintval;
            } else {
              sample = -uintval;
            }
            if(AMY_NCHANS == 1) {
                #ifdef ESP_PLATFORM
                    // esp32's i2s driver has this bug
                    block[i ^ 0x01] = sample;
                #else
                    block[i] = sample;
                #endif
            } else {
                block[(AMY_NCHANS * i) + c] = sample;
            }
        }
    }
    amy_global.total_blocks++;
    AMY_PROFILE_STOP(AMY_FILL_BUFFER)

    return block;
}

void amy_reset_sysclock() {
    amy_global.total_blocks = 0;
    amy_global.sequencer_tick_count = 0;
    sequencer_recompute();
}

// given a string play / schedule the event directly
void amy_play_message(char *message) {
    //fprintf(stderr, "amy_play_message: %s\n", message);
    struct event e = amy_default_event();
    amy_parse_message(message, &e);
    if(e.status == EVENT_SCHEDULED) {
        amy_add_event(&e);
    }
}


void amy_default_setup() {
    // sine wave "bleeper" on ch 16
    // store memory patch 1024 sine wave
    struct event e = amy_default_event();
    e.patch_number = 1024;
    patches_store_patch(&e, "v0");  // Just osc=0 to have something; sinewave is the default state.
    e.num_voices = 1;
    e.synth = 16;
    amy_add_event(&e);

    // GM drum synth on channel 10
    // Somehow, we need to select simple round-robin voice allocation, because the note numbers don't indicate the voice, so using the same
    // voice for successive events with the same note number can end up truncating samples.
    // We could make the voice management use the outer product of preset number and note when calculating "same note"
    
    // {'wave': amy.PCM, 'freq': 0}
    e = amy_default_event();
    e.patch_number = 1025;
    patches_store_patch(&e, "w7f0");
    e.num_voices = 6;
    e.synth = 10;
    e.synth_flags = _SYNTH_FLAGS_MIDI_DRUMS | _SYNTH_FLAGS_IGNORE_NOTE_OFFS;  // Flag to perform note -> drum PCM patch translation.
    amy_add_event(&e);

    // Juno 6 poly on channel 1
    // Define this last so if we release it, the oscs aren't fragmented.
    e = amy_default_event();
    e.num_voices = 6;
    e.patch_number = 0;
    e.synth = 1;
    amy_add_event(&e);
}

// amy_play_message -> amy_parse_message -> amy_add_event -> add_delta_to_queue -> i_deltas queue -> global delta queue
// fill_audio_buffer_task -> read delta global delta queue -> play_delta -> apply delta to synth[d.osc]

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
    #ifdef _POSIX_THREADS
        pthread_mutex_init(&amy_queue_lock, NULL);
        #ifndef __EMSCRIPTEN__
        if(amy_global.config.has_midi_gadget || amy_global.config.has_midi_mac) {
            pthread_t midi_thread_id;
            pthread_create(&midi_thread_id, NULL, run_midi, NULL);
        }
        #endif
    #elif defined ESP_PLATFORM
    xQueueSemaphore = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(run_midi, MIDI_TASK_NAME, (MIDI_TASK_STACK_SIZE) / sizeof(StackType_t), NULL, MIDI_TASK_PRIORITY, &midi_handle, MIDI_TASK_COREID);
    #else
    run_midi();
    #endif

    amy_profiles_init();
    sequencer_init();
    oscs_init();
    if(amy_global.config.set_default_synth)amy_default_setup();
}
