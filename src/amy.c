// libAMY

// Brian Whitman
// brian@variogr.am

#include "amy.h"

// Flag set momentarily by debug message to report state on-demand.
int debug_flag = 0;

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
        case AMY_ADD_EVENT: return "AMY_ADD_EVENT";
        case PLAY_EVENT: return "PLAY_EVENT";
        case MIX_WITH_PAN: return "MIX_WITH_PAN";
        case AMY_RENDER: return "AMY_RENDER";            
        case AMY_PREPARE_BUFFER: return "AMY_PREPARE_BUFFER";            
        case AMY_FILL_BUFFER: return "AMY_FILL_BUFFER";            
        case AMY_PARSE_MESSAGE: return "AMY_PARSE_MESSAGE";            
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
        case ENCL_LOG2: return "ENCL_LOG2";            
        case BLOCK_NORM: return "BLOCK_NORM";       
        case FILTER_LOOP_MUL8F_SS: return "FILTER_LOOP_MUL8F_SS";
        case FILTER_LOOP_MUL4E_SS: return "FILTER_LOOP_MUL4E_SS";
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
#include "pcm_large.h"


#include "clipping_lookup_table.h"
#include "delay.h"
// Final output delay lines.
delay_line_t **delay_lines; 

#ifdef _POSIX_THREADS
#include <pthread.h>
pthread_mutex_t amy_queue_lock; 
#endif

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
extern SemaphoreHandle_t xQueueSemaphore;
#endif

// Global state 
struct state amy_global;
// set of deltas for the fifo to be played
struct delta * events;
// state per osc as multi-channel synthesizer that the scheduler renders into
struct synthinfo * synth;
// envelope-modified per-osc state
struct mod_synthinfo * msynth;



// Two mixing blocks, one per core of rendering
SAMPLE ** fbl;
SAMPLE ** per_osc_fb; 

#ifndef malloc_caps
void * malloc_caps(uint32_t size, uint32_t flags) {
#ifdef ESP_PLATFORM
    //fprintf(stderr, "allocing size %ld flags %ld\n", size, flags);
    return heap_caps_malloc(size, flags);
#else
    // ignore flags
    return malloc(size);
#endif
}
#endif



// block -- what gets sent to the dac -- -32768...32767 (int16 le)
output_sample_type * block;
uint32_t total_samples;
uint32_t event_counter ;
uint32_t message_counter ;

char *message_start_pointer;
int16_t message_length;

int32_t computed_delta; // can be negative no prob, but usually host is larger # than client
uint8_t computed_delta_set; // have we set a delta yet?



SAMPLE *delay_mod = NULL;

typedef struct chorus_config {
    SAMPLE level;     // How much of the delayed signal to mix in to the output, typ F2S(0.5).
    int max_delay;   // Max delay when modulating.  Must be <= DELAY_LINE_LEN
    float lfo_freq;
    float depth;
} chorus_config_t;


chorus_config_t chorus = {CHORUS_DEFAULT_LEVEL, CHORUS_DEFAULT_MAX_DELAY, CHORUS_DEFAULT_LFO_FREQ, CHORUS_DEFAULT_MOD_DEPTH};

void alloc_chorus_delay_lines(void) {
    for(uint16_t c=0;c<AMY_NCHANS;++c) {
        delay_lines[c] = new_delay_line(DELAY_LINE_LEN, DELAY_LINE_LEN / 2, CHORUS_RAM_CAPS);
    }
    delay_mod = (SAMPLE *)malloc_caps(sizeof(SAMPLE) * AMY_BLOCK_SIZE, CHORUS_RAM_CAPS);
}

void dealloc_chorus_delay_lines(void) {
    for(uint16_t c=0;c<AMY_NCHANS;++c) {
        if (delay_lines[c]) free(delay_lines[c]);
        delay_lines[c] = NULL;
    }
    free(delay_mod);
    delay_mod = NULL;
}


void config_chorus(float level, int max_delay, float lfo_freq, float depth) {
    //fprintf(stderr, "config_chorus: level %.3f max_del %d lfo_freq %.3f depth %.3f\n",
    //        level, max_delay, lfo_freq, depth);
    if (level > 0) {
        // only allocate delay lines if chorus is more than inaudible.
        if (delay_lines[0] == NULL) {
            alloc_chorus_delay_lines();
        }
        // if we're turning on for the first time, start the oscillator.
        if (synth[CHORUS_MOD_SOURCE].status == STATUS_OFF) {  //chorus.level == 0) {
            // Setup chorus oscillator.
            synth[CHORUS_MOD_SOURCE].logfreq_coefs[COEF_CONST] = logfreq_of_freq(lfo_freq);
            synth[CHORUS_MOD_SOURCE].logfreq_coefs[COEF_NOTE] = 0;  // Turn off default.
            synth[CHORUS_MOD_SOURCE].amp_coefs[COEF_CONST] = depth;
            synth[CHORUS_MOD_SOURCE].amp_coefs[COEF_VEL] = 0;  // Turn off default.
            synth[CHORUS_MOD_SOURCE].amp_coefs[COEF_EG0] = 0;  // Turn off default.
            synth[CHORUS_MOD_SOURCE].wave = TRIANGLE;
            osc_note_on(CHORUS_MOD_SOURCE, freq_of_logfreq(synth[CHORUS_MOD_SOURCE].logfreq_coefs[0]));
        }
        // apply max_delay.
        for (int core=0; core<AMY_CORES; ++core) {
            for (int chan=0; chan<AMY_NCHANS; ++chan) {
                //delay_lines[chan]->max_delay = max_delay;
                delay_lines[chan]->fixed_delay = (int)max_delay / 2;
            }
        }
    }
    chorus.max_delay = max_delay;
    chorus.level = F2S(level);
    chorus.lfo_freq = lfo_freq;
    chorus.depth = depth;
}


typedef struct reverb_state {
    SAMPLE level;
    float liveness;
    float damping;
    float xover_hz;
} reverb_state_t;

reverb_state_t reverb = {F2S(REVERB_DEFAULT_LEVEL), REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING, REVERB_DEFAULT_XOVER_HZ};

void config_reverb(float level, float liveness, float damping, float xover_hz) {
    if (level > 0) {
        //printf("config_reverb: level %f liveness %f xover %f damping %f\n",
        //      level, liveness, xover_hz, damping);
        if (reverb.level == 0) init_stereo_reverb();  // In case it's the first time
        config_stereo_reverb(liveness, xover_hz, damping);
    }
    reverb.level = F2S(level);
    reverb.liveness = liveness;
    reverb.damping = damping;
    reverb.xover_hz = xover_hz;
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


void default_amy_parse_callback(char mode, char * message) {
    // do nothing
}

int8_t global_init() {
    // function pointers
    //amy_parse_callback = &default_amy_parse_callback;
    amy_global.next_event_write = 0;
    amy_global.event_start = NULL;
    amy_global.event_qsize = 0;
    amy_global.volume = 1.0f;
    amy_global.latency_ms = 0;
    amy_global.eq[0] = F2S(1.0f);
    amy_global.eq[1] = F2S(1.0f);
    amy_global.eq[2] = F2S(1.0f);
    amy_global.hpf_state = 0;
    amy_global.cores = 1;
    amy_global.has_reverb = 1;
    amy_global.has_chorus = 1;
    return 0;
}


// Convert to and from the log-frequency scale.
// A log-frequency scale is good for summing control inputs.


float logfreq_of_freq(float freq) {
    // logfreq is defined as log_2(freq / 8.18 Hz)
    if (freq==0) return ZERO_HZ_LOG_VAL;
    return log2f(freq / ZERO_LOGFREQ_IN_HZ);
}

float freq_of_logfreq(float logfreq) {
    if (logfreq==ZERO_HZ_LOG_VAL) return 0;
    return ZERO_LOGFREQ_IN_HZ * exp2f(logfreq);
}

float freq_for_midi_note(uint8_t midi_note) {
    return 440.0f*powf(2, (midi_note - 69.0f) / 12.0f);
}

float logfreq_for_midi_note(uint8_t midi_note) {
    // TODO: Precompensate for EPS_FOR_LOG
    return (midi_note - ZERO_MIDI_NOTE) / 12.0f;
}


// create a new default API accessible event
struct event amy_default_event() {
    struct event e;
    e.status = EMPTY;
    AMY_UNSET(e.time);
    AMY_UNSET(e.osc);
    AMY_UNSET(e.patch);
    AMY_UNSET(e.wave);
    AMY_UNSET(e.load_patch);
    AMY_UNSET(e.phase);
    AMY_UNSET(e.feedback);
    AMY_UNSET(e.velocity);
    AMY_UNSET(e.midi_note);
    AMY_UNSET(e.volume);
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
    AMY_UNSET(e.filter_type);
    AMY_UNSET(e.chained_osc);
    AMY_UNSET(e.clone_osc);
    AMY_UNSET(e.mod_source);
    AMY_UNSET(e.mod_target);
    AMY_UNSET(e.eq_l);
    AMY_UNSET(e.eq_m);
    AMY_UNSET(e.eq_h);
    AMY_UNSET(e.algorithm);
    AMY_UNSET(e.bp_is_set[0]);
    AMY_UNSET(e.bp_is_set[1]);
    AMY_UNSET(e.bp0_target);
    AMY_UNSET(e.bp1_target);
    AMY_UNSET(e.reset_osc);
    e.algo_source[0] = 0;
    e.bp0[0] = 0;
    e.bp1[0] = 0;
    e.voices[0] = 0;
    return e;
}

void add_delta_to_queue(struct delta d) {
    AMY_PROFILE_START(ADD_DELTA_TO_QUEUE)
#if defined ESP_PLATFORM && !defined ARDUINO
    //  take the queue mutex before starting
    xSemaphoreTake(xQueueSemaphore, portMAX_DELAY);
#elif defined _POSIX_THREADS
    //fprintf(stderr,"add_delta: time %d osc %d param %d val 0x%x, qsize %d\n", total_samples, d.osc, d.param, d.data, amy_global.event_qsize);
    pthread_mutex_lock(&amy_queue_lock); 
#endif

    if(amy_global.event_qsize < AMY_EVENT_FIFO_LEN) {
        // scan through the memory to find a free slot, starting at write pointer
        uint16_t write_location = amy_global.next_event_write;
        int16_t found = -1;
        // guaranteed to find eventually if qsize stays accurate
        while(found<0) {
            if(events[write_location].time == UINT32_MAX) found = write_location;
            write_location = (write_location + 1) % AMY_EVENT_FIFO_LEN;
        }
        // found a mem location. copy the data in and update the write pointers.
        events[found].time = d.time;
        events[found].osc = d.osc;
        events[found].param = d.param;
        events[found].data = d.data;
        amy_global.next_event_write = write_location;
        amy_global.event_qsize++;

        // now insert it into the sorted list for fast playback
        struct delta **pptr = &amy_global.event_start;
        while(d.time >= (*pptr)->time)
            pptr = &(*pptr)->next;
        events[found].next = *pptr;
        *pptr = &events[found];

        event_counter++;

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
void amy_add_event(struct event e) {
    amy_add_event_internal(e, 0);
}

// Add a API facing event, convert into delta directly
void amy_add_event_internal(struct event e, uint16_t base_osc) {
    AMY_PROFILE_START(AMY_ADD_EVENT)
    struct delta d;
    

    // Synth defaults if not set, these are required for the delta struct
    if(AMY_IS_UNSET(e.osc)) { e.osc = 0; } 
    if(AMY_IS_UNSET(e.time)) { e.time = 0; } 

    // First, adapt the osc in this event with base_osc offsets for voices
    e.osc += base_osc;

    // Voices / patches gets set up here 
    // you must set both voices & load_patch together to load a patch 
    if(e.voices[0] != 0 && AMY_IS_SET(e.load_patch)) {
        patches_load_patch(e);
        patches_event_has_voices(e);
        goto end;
    } else {
        if(e.voices[0] != 0) {
            patches_event_has_voices(e);
            goto end;
        }
    }


    d.time = e.time;
    d.osc = e.osc;
    // Everything else only added to queue if set
    if(AMY_IS_SET(e.wave)) { d.param=WAVE; d.data = *(uint32_t *)&e.wave; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.patch)) { d.param=PATCH; d.data = *(uint32_t *)&e.patch; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.midi_note)) { d.param=MIDI_NOTE; d.data = *(uint32_t *)&e.midi_note; add_delta_to_queue(d); }
    for (int i = 0; i < NUM_COMBO_COEFS; ++i)
        if(AMY_IS_SET(e.amp_coefs[i])) {  d.param=AMP + i; d.data = *(uint32_t *)&e.amp_coefs[i]; add_delta_to_queue(d); }

    // First freq coef is in Hz, rest are linear.
    if(AMY_IS_SET(e.freq_coefs[0])) { float logfreq = logfreq_of_freq(e.freq_coefs[0]); d.param=FREQ; d.data = *(uint32_t *)&logfreq; add_delta_to_queue(d); }

    for (int i = 1; i < NUM_COMBO_COEFS; ++i)
        if(AMY_IS_SET(e.freq_coefs[i])) { d.param=FREQ + i; d.data = *(uint32_t *)&e.freq_coefs[i]; add_delta_to_queue(d); }

    // First freq coef is in Hz, rest are linear.
    if(AMY_IS_SET(e.filter_freq_coefs[0])) { float filter_logfreq = logfreq_of_freq(e.filter_freq_coefs[0]); d.param=FILTER_FREQ; d.data = *(uint32_t *)&filter_logfreq; add_delta_to_queue(d); }
    for (int i = 1; i < NUM_COMBO_COEFS; ++i)
        if(AMY_IS_SET(e.filter_freq_coefs[i])) { float filter_logfreq_coef = e.filter_freq_coefs[i]; d.param=FILTER_FREQ + i; d.data = *(uint32_t *)&filter_logfreq_coef; add_delta_to_queue(d); }
    for (int i = 0; i < NUM_COMBO_COEFS; ++i)
        if(AMY_IS_SET(e.duty_coefs[i])) {  d.param=DUTY + i; d.data = *(uint32_t *)&e.duty_coefs[i]; add_delta_to_queue(d); }
    for (int i = 0; i < NUM_COMBO_COEFS; ++i)
        if(AMY_IS_SET(e.pan_coefs[i])) { d.param=PAN + i; d.data = *(uint32_t *)&e.pan_coefs[i]; add_delta_to_queue(d); }


    if(AMY_IS_SET(e.feedback)) { d.param=FEEDBACK; d.data = *(uint32_t *)&e.feedback; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.phase)) { d.param=PHASE; d.data = *(uint32_t *)&e.phase; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.volume)) { d.param=VOLUME; d.data = *(uint32_t *)&e.volume; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.latency_ms)) { d.param=LATENCY; d.data = *(uint32_t *)&e.latency_ms; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.ratio)) { float logratio = log2f(e.ratio); d.param=RATIO; d.data = *(uint32_t *)&logratio; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.resonance)) { d.param=RESONANCE; d.data = *(uint32_t *)&e.resonance; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.chained_osc)) { e.chained_osc += base_osc; d.param=CHAINED_OSC; d.data = *(uint32_t *)&e.chained_osc; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.clone_osc)) { e.clone_osc += base_osc; d.param=CLONE_OSC; d.data = *(uint32_t *)&e.clone_osc; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.reset_osc)) { e.reset_osc += base_osc; d.param=RESET_OSC; d.data = *(uint32_t *)&e.reset_osc; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.mod_source)) { e.mod_source += base_osc; d.param=MOD_SOURCE; d.data = *(uint32_t *)&e.mod_source; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.mod_target)) { d.param=MOD_TARGET; d.data = *(uint32_t *)&e.mod_target; add_delta_to_queue(d); }

    if(AMY_IS_SET(e.bp0_target)) { d.param=BP0_TARGET; d.data = *(uint32_t *)&e.bp0_target; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.bp1_target)) { d.param=BP1_TARGET; d.data = *(uint32_t *)&e.bp1_target; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.filter_type)) { d.param=FILTER_TYPE; d.data = *(uint32_t *)&e.filter_type; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.algorithm)) { d.param=ALGORITHM; d.data = *(uint32_t *)&e.algorithm; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.eq_l)) { d.param=EQ_L; d.data = *(uint32_t *)&e.eq_l; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.eq_m)) { d.param=EQ_M; d.data = *(uint32_t *)&e.eq_m; add_delta_to_queue(d); }
    if(AMY_IS_SET(e.eq_h)) { d.param=EQ_H; d.data = *(uint32_t *)&e.eq_h; add_delta_to_queue(d); }

    if(e.algo_source[0] != 0) {
        struct synthinfo t;
        parse_algorithm_source(&t, e.algo_source);
        for(uint8_t i=0;i<MAX_ALGO_OPS;i++) { 
            if(AMY_IS_SET(t.algo_source[i])) { 
                t.algo_source[i] += base_osc;
                d.param=ALGO_SOURCE_START+i; 
                d.data = *(uint32_t *)&t.algo_source[i]; 
                add_delta_to_queue(d); 
            }
        }
    }


    char * bps[MAX_BREAKPOINT_SETS] = {e.bp0, e.bp1};
    for(uint8_t i=0;i<MAX_BREAKPOINT_SETS;i++) {
        //if(bps[i][0] != 0) {
        if(AMY_IS_SET(e.bp_is_set[i])) {
            struct synthinfo t;
            parse_breakpoint(&t, bps[i], i);
            for(uint8_t j=0;j<MAX_BREAKPOINTS;j++) {
                d.param=BP_START+(j*2)+(i*MAX_BREAKPOINTS*2); d.data = *(uint32_t *)&t.breakpoint_times[i][j]; add_delta_to_queue(d);
                // Stop adding deltas after first UNSET time sent, just to mark the end of the set when overwriting.
                if(!AMY_IS_SET(t.breakpoint_times[i][j])) break;
                d.param=BP_START+(j*2 + 1)+(i*MAX_BREAKPOINTS*2); d.data = *(uint32_t *)&t.breakpoint_values[i][j]; add_delta_to_queue(d);
            }
        }
    }

    // add this last -- this is a trigger, that if sent alongside osc setup parameters, you want to run after those
    if(AMY_IS_SET(e.velocity)) {  d.param=VELOCITY; d.data = *(uint32_t *)&e.velocity; add_delta_to_queue(d); }
end:
    message_counter++;
    AMY_PROFILE_STOP(AMY_ADD_EVENT)

}


void clone_osc(uint16_t i, uint16_t f) {
    // Set all the synth state to the values from another osc.
    //fprintf(stderr, "cloning osc %d from %d\n", i, f);
    synth[i].wave = synth[f].wave;
    synth[i].patch = synth[f].patch;
    //synth[i].midi_note = synth[f].midi_note;
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].amp_coefs[j] = synth[f].amp_coefs[j];
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].logfreq_coefs[j] = synth[f].logfreq_coefs[j];
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].filter_logfreq_coefs[j] = synth[f].filter_logfreq_coefs[j];
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].duty_coefs[j] = synth[f].duty_coefs[j];
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].pan_coefs[j] = synth[f].pan_coefs[j];
    synth[i].feedback = synth[f].feedback;
    //synth[i].phase = synth[f].phase;
    //synth[i].volume = synth[f].volume;
    synth[i].eq_l = synth[f].eq_l;
    synth[i].eq_m = synth[f].eq_m;
    synth[i].eq_h = synth[f].eq_h;
    synth[i].logratio = synth[f].logratio;
    synth[i].resonance = synth[f].resonance;
    //synth[i].velocity = synth[f].velocity;
    //synth[i].step = synth[f].step;
    //synth[i].sample = synth[f].sample;
    //synth[i].mod_value = synth[f].mod_value;
    //synth[i].substep = synth[f].substep;
    //synth[i].status = synth[f].status;
    //synth[i].chained_osc = synth[f].chained_osc;  // RISKY - could make osc loops without knowing.
    //synth[i].mod_source = synth[f].mod_source;    // It's OK to have multiple oscs with the same mod source.  But if we set it, then clone other params, we overwrite it.
    synth[i].mod_target = synth[f].mod_target;
    //synth[i].note_on_clock = synth[f].note_on_clock;
    //synth[i].note_off_clock = synth[f].note_off_clock;
    //synth[i].zero_amp_clock = synth[f].zero_amp_clock;
    //synth[i].mod_value_clock = synth[f].mod_value_clock;
    synth[i].filter_type = synth[f].filter_type;
    //synth[i].hpf_state[0] = synth[f].hpf_state[0];
    //synth[i].hpf_state[1] = synth[f].hpf_state[1];
    //synth[i].last_amp = synth[f].last_amp;
    //synth[i].dc_offset = synth[f].dc_offset;
    synth[i].algorithm = synth[f].algorithm;
    //for(uint8_t j=0;j<MAX_ALGO_OPS;j++) synth[i].algo_source[j] = synth[f].algo_source[j];  // RISKY - end up allocating secondary oscs to multiple mains.
    for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) {
        for(uint8_t k=0;k<MAX_BREAKPOINTS;k++) {
            synth[i].breakpoint_times[j][k] = synth[f].breakpoint_times[j][k];
            synth[i].breakpoint_values[j][k] = synth[f].breakpoint_values[j][k];
        }
        synth[i].breakpoint_target[j] = synth[f].breakpoint_target[j];
    }
    //for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) { synth[i].last_scale[j] = synth[f].last_scale[j]; }
    //synth[i].last_two[0] = synth[f].last_two[0];
    //synth[i].last_two[1] = synth[f].last_two[1];
    //synth[i].lut = synth[f].lut;
}

void reset_osc(uint16_t i ) {
    // set all the synth state to defaults
    synth[i].osc = i; // self-reference to make updating oscs easier
    synth[i].wave = SINE;
    msynth[i].last_duty = 0.5f;
    AMY_UNSET(synth[i].patch);
    AMY_UNSET(synth[i].midi_note);
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].amp_coefs[j] = 0;
    synth[i].amp_coefs[COEF_VEL] = 1.0f;
    synth[i].amp_coefs[COEF_EG0] = 1.0f;
    msynth[i].amp = 1.0f;
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].logfreq_coefs[j] = 0;
    synth[i].logfreq_coefs[COEF_NOTE] = 1.0;
    msynth[i].logfreq = 0;
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].filter_logfreq_coefs[j] = 0;
    msynth[i].filter_logfreq = 0;
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].duty_coefs[j] = 0;
    synth[i].duty_coefs[COEF_CONST] = 0.5f;
    msynth[i].duty = 0.5f;
    for (int j = 0; j < NUM_COMBO_COEFS; ++j)
        synth[i].pan_coefs[j] = 0;
    synth[i].pan_coefs[COEF_CONST] = 0.5f;
    msynth[i].pan = 0.5f;
    synth[i].feedback = F2S(0); //.996; todo ks feedback is v different from fm feedback
    msynth[i].feedback = F2S(0); //.996; todo ks feedback is v different from fm feedback
    synth[i].phase = F2P(0);
    synth[i].volume = 0;
    synth[i].eq_l = 0;
    synth[i].eq_m = 0;
    synth[i].eq_h = 0;
    AMY_UNSET(synth[i].logratio);
    synth[i].resonance = 0.7f;
    msynth[i].resonance = 0.7f;
    synth[i].velocity = 0;
    synth[i].step = 0;
    synth[i].sample = F2S(0);
    synth[i].mod_value = F2S(0);
    synth[i].substep = 0;
    synth[i].status = STATUS_OFF;
    AMY_UNSET(synth[i].chained_osc);
    AMY_UNSET(synth[i].mod_source);
    synth[i].mod_target = 0;
    AMY_UNSET(synth[i].note_on_clock);
    AMY_UNSET(synth[i].note_off_clock);
    AMY_UNSET(synth[i].zero_amp_clock);
    AMY_UNSET(synth[i].mod_value_clock);
    synth[i].filter_type = FILTER_NONE;
    synth[i].hpf_state[0] = 0;
    synth[i].hpf_state[1] = 0;
    for(int j = 0; j < 2 * FILT_NUM_DELAYS; ++j) synth[i].filter_delay[j] = 0;
    synth[i].last_filt_norm_bits = 0;
    synth[i].last_amp = 0;
    synth[i].dc_offset = 0;
    synth[i].algorithm = 0;
    for(uint8_t j=0;j<MAX_ALGO_OPS;j++) AMY_UNSET(synth[i].algo_source[j]);
    for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) {
        for(uint8_t k=0;k<MAX_BREAKPOINTS;k++) {
            AMY_UNSET(synth[i].breakpoint_times[j][k]);
            AMY_UNSET(synth[i].breakpoint_values[j][k]);
        }
        synth[i].breakpoint_target[j] = 0;
    }
    for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) { synth[i].last_scale[j] = 0; }
    synth[i].last_two[0] = 0;
    synth[i].last_two[1] = 0;
    synth[i].lut = NULL;
}

void amy_reset_oscs() {
    // include chorus osc
    for(uint16_t i=0;i<AMY_OSCS+1;i++) reset_osc(i);
    // also reset filters and volume
    amy_global.volume = 1.0f;
    amy_global.eq[0] = F2S(1.0f);
    amy_global.eq[1] = F2S(1.0f);
    amy_global.eq[2] = F2S(1.0f);
    // Reset chorus oscillator
    if (AMY_HAS_CHORUS) config_chorus(CHORUS_DEFAULT_LEVEL, CHORUS_DEFAULT_MAX_DELAY, CHORUS_DEFAULT_LFO_FREQ, CHORUS_DEFAULT_MOD_DEPTH);
    if( AMY_HAS_REVERB) config_reverb(REVERB_DEFAULT_LEVEL, REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING, REVERB_DEFAULT_XOVER_HZ);
    // Reset patches
    patches_reset();
}



// the synth object keeps held state, whereas events are only deltas/changes
int8_t oscs_init() {
    if(AMY_KS_OSCS>0)
        ks_init();
    filters_init();
    algo_init();

    if(pcm_samples) {
        pcm_init();
    }
    if(AMY_HAS_CUSTOM == 1) {
        custom_init();
    }
    events = (struct delta*)malloc_caps(sizeof(struct delta) * AMY_EVENT_FIFO_LEN, EVENTS_RAM_CAPS);
    synth = (struct synthinfo*) malloc_caps(sizeof(struct synthinfo) * (AMY_OSCS+1), SYNTH_RAM_CAPS);
    msynth = (struct mod_synthinfo*) malloc_caps(sizeof(struct mod_synthinfo) * (AMY_OSCS+1), SYNTH_RAM_CAPS);
    block = (output_sample_type *) malloc_caps(sizeof(output_sample_type) * AMY_BLOCK_SIZE * AMY_NCHANS, BLOCK_RAM_CAPS);
    // set all oscillators to their default values
    amy_reset_oscs();

    // make a fencepost last event with no next, time of end-1, and call it start for now, all other events get inserted before it
    events[0].next = NULL;
    events[0].time = UINT32_MAX - 1;
    events[0].osc = 0;
    events[0].data = 0;
    events[0].param = NO_PARAM;
    amy_global.next_event_write = 1;
    amy_global.event_start = &events[0];
    amy_global.event_qsize = 1; // queue will always have at least 1 thing in it 

    // set all the other events to empty
    for(uint16_t i=1;i<AMY_EVENT_FIFO_LEN;i++) {
        events[i].time = UINT32_MAX;
        events[i].next = NULL;
        events[i].osc = 0;
        events[i].data = 0;
        events[i].param = NO_PARAM;
    }
    fbl = (SAMPLE**) malloc_caps(sizeof(SAMPLE*) * AMY_CORES, FBL_RAM_CAPS); // one per core, just core 0 used off esp32
    per_osc_fb = (SAMPLE**) malloc_caps(sizeof(SAMPLE*) * AMY_CORES, FBL_RAM_CAPS); // one per core, just core 0 used off esp32
    // clear out both as local mode won't use fbl[1] 
    for(uint16_t core=0;core<AMY_CORES;++core) {
        fbl[core]= (SAMPLE*)malloc_caps(sizeof(SAMPLE) * AMY_BLOCK_SIZE * AMY_NCHANS, FBL_RAM_CAPS);
        per_osc_fb[core]= (SAMPLE*)malloc_caps(sizeof(SAMPLE) * AMY_BLOCK_SIZE, FBL_RAM_CAPS);
        for(uint16_t c=0;c<AMY_NCHANS;++c) {
            for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) {
                fbl[core][AMY_BLOCK_SIZE*c + i] = 0;
            }
        }
    }
    // we only alloc delay lines if the chorus is turned on.
    if (delay_lines == NULL)
        delay_lines = (delay_line_t **)malloc(sizeof(delay_line_t *) * AMY_NCHANS);
    if(AMY_HAS_CHORUS > 0 || AMY_HAS_REVERB > 0) {
        for (int c = 0; c < AMY_NCHANS; ++c)  delay_lines[c] = NULL;
    }
    //init_stereo_reverb();

    total_samples = 0;
    computed_delta = 0;
    computed_delta_set = 0;
    event_counter = 0;
    message_counter = 0;
    //printf("AMY online with %d oscillators, %d block size, %d cores, %d channels, %d pcm samples\n", 
    //    AMY_OSCS, AMY_BLOCK_SIZE, AMY_CORES, AMY_NCHANS, pcm_samples);
    return 0;
}


// types: 0 - show profile if set
//        1 - show profile, queue
//        2 - show profile, queue, osc data
void show_debug(uint8_t type) {
    debug_flag = type;
    amy_profiles_print();
    if(type>0) {
        struct delta * ptr = amy_global.event_start;
        uint16_t q = amy_global.event_qsize;
        if(q > 25) q = 25;
        for(uint16_t i=0;i<q;i++) {
            fprintf(stderr,"%d time %" PRIu32 " osc %d param %d - %f %d\n", i, ptr->time, ptr->osc, ptr->param, *(float *)&ptr->data, *(int *)&ptr->data);
            ptr = ptr->next;
        }
    }
    if(type>1) {
        // print out all the osc data
        //printf("global: filter %f resonance %f volume %f status %d\n", amy_global.filter_freq, amy_global.resonance, amy_global.volume, amy_global.status);
        fprintf(stderr,"global: volume %f eq: %f %f %f \n", amy_global.volume, S2F(amy_global.eq[0]), S2F(amy_global.eq[1]), S2F(amy_global.eq[2]));
        //printf("mod global: filter %f resonance %f\n", mglobal.filter_freq, mglobal.resonance);
        for(uint16_t i=0;i<10 /* AMY_OSCS */;i++) {
            fprintf(stderr,"osc %d: status %d wave %d mod_target %d mod_source %d velocity %flogratio %f feedback %f resonance %f step %f chained %d algo %d source %d,%d,%d,%d,%d,%d  \n",
                    i, synth[i].status, synth[i].wave, synth[i].mod_target, synth[i].mod_source,
                    synth[i].velocity, synth[i].logratio, synth[i].feedback, synth[i].resonance, P2F(synth[i].step), synth[i].chained_osc, 
                    synth[i].algorithm, 
                    synth[i].algo_source[0], synth[i].algo_source[1], synth[i].algo_source[2], synth[i].algo_source[3], synth[i].algo_source[4], synth[i].algo_source[5] );
            fprintf(stderr, "  amp_coefs: %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i].amp_coefs[0], synth[i].amp_coefs[1], synth[i].amp_coefs[2], synth[i].amp_coefs[3], synth[i].amp_coefs[4], synth[i].amp_coefs[5]);
            fprintf(stderr, "  lfr_coefs: %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i].logfreq_coefs[0], synth[i].logfreq_coefs[1], synth[i].logfreq_coefs[2], synth[i].logfreq_coefs[3], synth[i].logfreq_coefs[4], synth[i].logfreq_coefs[5]);
            fprintf(stderr, "  flf_coefs: %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i].filter_logfreq_coefs[0], synth[i].filter_logfreq_coefs[1], synth[i].filter_logfreq_coefs[2], synth[i].filter_logfreq_coefs[3], synth[i].filter_logfreq_coefs[4], synth[i].filter_logfreq_coefs[5]);
            fprintf(stderr, "  dut_coefs: %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i].duty_coefs[0], synth[i].duty_coefs[1], synth[i].duty_coefs[2], synth[i].duty_coefs[3], synth[i].duty_coefs[4], synth[i].duty_coefs[5]);
            fprintf(stderr, "  pan_coefs: %.3f %.3f %.3f %.3f %.3f %.3f\n", synth[i].pan_coefs[0], synth[i].pan_coefs[1], synth[i].pan_coefs[2], synth[i].pan_coefs[3], synth[i].pan_coefs[4], synth[i].pan_coefs[5]);
            if(type>3) {
                for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) {
                    fprintf(stderr,"  bp%d (target %d): ", j, synth[i].breakpoint_target[j]);
                    for(uint8_t k=0;k<MAX_BREAKPOINTS;k++) {
                        fprintf(stderr,"%" PRIi32 ": %f ", synth[i].breakpoint_times[j][k], synth[i].breakpoint_values[j][k]);
                    }
                    fprintf(stderr,"\n");
                }
                fprintf(stderr,"mod osc %d: amp: %f, logfreq %f duty %f filter_logfreq %f resonance %f fb/bw %f pan %f \n", i, msynth[i].amp, msynth[i].logfreq, msynth[i].duty, msynth[i].filter_logfreq, msynth[i].resonance, msynth[i].feedback, msynth[i].pan);
            }
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
    free(synth);
    free(msynth);
    free(events);
    if(AMY_KS_OSCS > 0)
        ks_deinit();
    filters_deinit();
    dealloc_chorus_delay_lines();
}


void osc_note_on(uint16_t osc, float initial_freq) {
    //printf("Note on: osc %d wav %d filter_freq_coefs=%f %f %f %f %f %f\n", osc, synth[osc].wave, 
    //       synth[osc].filter_logfreq_coefs[0], synth[osc].filter_logfreq_coefs[1], synth[osc].filter_logfreq_coefs[2],
    //       synth[osc].filter_logfreq_coefs[3], synth[osc].filter_logfreq_coefs[4], synth[osc].filter_logfreq_coefs[5]);
    if(synth[osc].wave==SINE) sine_note_on(osc, initial_freq);
    if(synth[osc].wave==SAW_DOWN) saw_down_note_on(osc, initial_freq);
    if(synth[osc].wave==SAW_UP) saw_up_note_on(osc, initial_freq);
    if(synth[osc].wave==TRIANGLE) triangle_note_on(osc, initial_freq);
    if(synth[osc].wave==PULSE) pulse_note_on(osc, initial_freq);
    if(synth[osc].wave==PCM) pcm_note_on(osc);
    if(synth[osc].wave==ALGO) algo_note_on(osc);
    if(AMY_HAS_PARTIALS == 1) {
        if(synth[osc].wave==PARTIAL)  partial_note_on(osc);
        if(synth[osc].wave==PARTIALS) partials_note_on(osc);
    }
    if(AMY_HAS_CUSTOM == 1) {
        if(synth[osc].wave==CUSTOM) custom_note_on(osc, initial_freq);
    }
}

void apply_target_to_coefs(uint16_t osc, int target_val, int which_coef) {
    // Convert a TARGET bitmask into values in all the coef vectors.
    synth[osc].amp_coefs[which_coef] = (target_val & TARGET_AMP)? 1.0f : 0;
    synth[osc].logfreq_coefs[which_coef] = (target_val & TARGET_FREQ)? 1.0f : 0;
    synth[osc].filter_logfreq_coefs[which_coef] = (target_val & TARGET_FILTER_FREQ)? 1.0f : 0;
    synth[osc].duty_coefs[which_coef] = (target_val & TARGET_DUTY)? 1.0f : 0;
    synth[osc].pan_coefs[which_coef] = (target_val & TARGET_PAN)? 1.0f : 0;
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
        next_osc = synth[next_osc].chained_osc;
    } while(AMY_IS_SET(next_osc));
    return false;
}

// play an event, now -- tell the audio loop to start making noise
void play_event(struct delta d) {
    AMY_PROFILE_START(PLAY_EVENT)
    //fprintf(stderr,"play_event: time %d osc %d param %d val 0x%x, qsize %d\n", total_samples, d.osc, d.param, d.data, global.event_qsize);
    uint8_t trig=0;
    // todo: event-only side effect, remove
    if(d.param == MIDI_NOTE) {
        synth[d.osc].midi_note = *(uint16_t *)&d.data;
        //synth[d.osc].freq = freq_for_midi_note(*(uint16_t *)&d.data);
        //synth[d.osc].logfreq_coefs[0] = logfreq_for_midi_note(*(uint16_t *)&d.data);
        //printf("time %lld osc %d midi_note %d logfreq %f\n", total_samples, d.osc, synth[d.osc].midi_note, synth[d.osc].logfreq);
        // Midi note and Velocity are propagated to chained_osc.
        if (AMY_IS_SET(synth[d.osc].chained_osc)) {
            d.osc = synth[d.osc].chained_osc;
            // Recurse with the new osc.  We have to recurse rather than directly setting so that a complete chain of recursion will work.
            play_event(d);
        }
    }

    if(d.param == WAVE) {
        synth[d.osc].wave = *(uint16_t *)&d.data;
        // todo: event-only side effect, remove
        // we do this because we need to set up LUTs for FM oscs. it's a TODO to make this cleaner
        if(synth[d.osc].wave == SINE) {
            sine_note_on(d.osc, freq_of_logfreq(synth[d.osc].logfreq_coefs[0]));
        }
    }
    if(d.param == PHASE) synth[d.osc].phase = *(PHASOR *)&d.data;  // PHASOR
    if(d.param == PATCH) synth[d.osc].patch = *(uint16_t *)&d.data;
    if(d.param == FEEDBACK) synth[d.osc].feedback = *(float *)&d.data;

    if(d.param >= AMP && d.param < AMP + NUM_COMBO_COEFS)
        synth[d.osc].amp_coefs[d.param - AMP] = *(float *)&d.data;
    if(d.param >= FREQ && d.param < FREQ + NUM_COMBO_COEFS)
        synth[d.osc].logfreq_coefs[d.param - FREQ] = *(float *)&d.data;
    if(d.param >= FILTER_FREQ && d.param < FILTER_FREQ + NUM_COMBO_COEFS)
        synth[d.osc].filter_logfreq_coefs[d.param - FILTER_FREQ] = *(float *)&d.data;
    if(d.param >= DUTY && d.param < DUTY + NUM_COMBO_COEFS)
        synth[d.osc].duty_coefs[d.param - DUTY] = *(float *)&d.data;
    if(d.param >= PAN && d.param < PAN + NUM_COMBO_COEFS)
        synth[d.osc].pan_coefs[d.param - PAN] = *(float *)&d.data;

    if(d.param == BP0_TARGET) {
        synth[d.osc].breakpoint_target[0] = *(uint16_t *)&d.data;
        //trig = 1;
        apply_target_to_coefs(d.osc, synth[d.osc].breakpoint_target[0], COEF_EG0);
    }
    if(d.param == BP1_TARGET) {
        synth[d.osc].breakpoint_target[1] = *(uint16_t *)&d.data;
        //trig=1;
        apply_target_to_coefs(d.osc, synth[d.osc].breakpoint_target[1], COEF_EG1);
    }
    // todo, i really should clean this up
    if(d.param >= BP_START && d.param < BP_END) {
        uint8_t pos = d.param - BP_START;
        uint8_t bp_set = 0;
        if(pos > (MAX_BREAKPOINTS * 2)) { bp_set = 1; pos = pos - (MAX_BREAKPOINTS * 2); }
        if(pos % 2 == 0) {
            synth[d.osc].breakpoint_times[bp_set][pos / 2] = *(uint32_t *)&d.data;
        } else {
            synth[d.osc].breakpoint_values[bp_set][(pos-1) / 2] = *(float *)&d.data;
        }
        //trig=1;
    }
    if(trig) synth[d.osc].note_on_clock = total_samples;

    if(d.param == CHAINED_OSC) {
        int chained_osc = *(int16_t *)&d.data;
        if (chained_osc >=0 && chained_osc < AMY_OSCS &&
            !chained_osc_would_cause_loop(d.osc, chained_osc))
            synth[d.osc].chained_osc = chained_osc;
        else
            AMY_UNSET(synth[d.osc].chained_osc);
    }
    if(d.param == CLONE_OSC) { clone_osc(d.osc, *(int16_t *)&d.data); }
    if(d.param == RESET_OSC) { if(*(int16_t *)&d.data>AMY_OSCS) { amy_reset_oscs(); } else { reset_osc(*(int16_t *)&d.data); } }
    // todo: event-only side effect, remove
    if(d.param == MOD_SOURCE) { synth[d.osc].mod_source = *(uint16_t *)&d.data; synth[*(uint16_t *)&d.data].status = IS_MOD_SOURCE; }
    if(d.param == MOD_TARGET) {
        synth[d.osc].mod_target = *(uint16_t *)&d.data;
        apply_target_to_coefs(d.osc, synth[d.osc].mod_target, COEF_MOD);
    }

    if(d.param == RATIO) synth[d.osc].logratio = *(float *)&d.data;

    if(d.param == FILTER_TYPE) synth[d.osc].filter_type = *(uint8_t *)&d.data;
    if(d.param == RESONANCE) synth[d.osc].resonance = *(float *)&d.data;

    if(d.param == ALGORITHM) synth[d.osc].algorithm = *(uint8_t *)&d.data;

    if(d.param >= ALGO_SOURCE_START && d.param < ALGO_SOURCE_END) {
        uint16_t which_source = d.param - ALGO_SOURCE_START;
        synth[d.osc].algo_source[which_source] = *(int16_t *)&d.data;
        if(synth[d.osc].algo_source[which_source] >= 0) synth[*(int16_t*)&d.data].status=IS_ALGO_SOURCE;
    }

    // for global changes, just make the change, no need to update the per-osc synth
    if(d.param == VOLUME) amy_global.volume = *(float *)&d.data;
    if(d.param == LATENCY) { amy_global.latency_ms = *(uint16_t *)&d.data; computed_delta_set = 0; computed_delta = 0; }
    if(d.param == EQ_L) amy_global.eq[0] = F2S(powf(10, *(float *)&d.data / 20.0));
    if(d.param == EQ_M) amy_global.eq[1] = F2S(powf(10, *(float *)&d.data / 20.0));
    if(d.param == EQ_H) amy_global.eq[2] = F2S(powf(10, *(float *)&d.data / 20.0));

    // triggers / envelopes
    // the only way a sound is made is if velocity (note on) is >0.
    // Ignore velocity events if we've already received one this frame.  This may be due to a loop in chained_oscs.
    if(d.param == VELOCITY) {
        if (*(float *)&d.data > 0) { // new note on (even if something is already playing on this osc)
            //synth[d.osc].amp_coefs[COEF_CONST] = *(float *)&d.data; // these could be decoupled, later
            synth[d.osc].velocity = *(float *)&d.data;
            synth[d.osc].status = AUDIBLE;
            // take care of fm & ks first -- no special treatment for bp/mod
            if(synth[d.osc].wave==KS) {
                #if AMY_KS_OSCS > 0
                ks_note_on(d.osc);
                #endif
            }  else {
                // an osc came in with a note on.
                // start the bp clock
                synth[d.osc].note_on_clock = total_samples; //esp_timer_get_time() / 1000;

                // if there was a filter active for this voice, reset it
                if(synth[d.osc].filter_type != FILTER_NONE) reset_filter(d.osc);
                // For repeatability, start at zero phase.
                synth[d.osc].phase = 0;
                // restart the waveforms
                // Guess at the initial frequency depending only on const & note.  Envelopes not "developed" yet.
                float initial_logfreq = synth[d.osc].logfreq_coefs[0];
                if (AMY_IS_SET(synth[d.osc].midi_note))
                    initial_logfreq += synth[d.osc].logfreq_coefs[1] * logfreq_for_midi_note(synth[d.osc].midi_note);
                float initial_freq = freq_of_logfreq(initial_logfreq);
                osc_note_on(d.osc, initial_freq);
                // trigger the mod source, if we have one
                if(AMY_IS_SET(synth[d.osc].mod_source)) {
                    synth[synth[d.osc].mod_source].note_on_clock = total_samples;  // Need a note_on_clock to have envelope work correctly.
                    if(synth[synth[d.osc].mod_source].wave==SINE) sine_mod_trigger(synth[d.osc].mod_source);
                    if(synth[synth[d.osc].mod_source].wave==SAW_DOWN) saw_up_mod_trigger(synth[d.osc].mod_source);
                    if(synth[synth[d.osc].mod_source].wave==SAW_UP) saw_down_mod_trigger(synth[d.osc].mod_source);
                    if(synth[synth[d.osc].mod_source].wave==TRIANGLE) triangle_mod_trigger(synth[d.osc].mod_source);
                    if(synth[synth[d.osc].mod_source].wave==PULSE) pulse_mod_trigger(synth[d.osc].mod_source);
                    if(synth[synth[d.osc].mod_source].wave==PCM) pcm_mod_trigger(synth[d.osc].mod_source);
                    if(synth[synth[d.osc].mod_source].wave==CUSTOM) custom_mod_trigger(synth[d.osc].mod_source);
                }

            }
        } else if(synth[d.osc].velocity > 0 && *(float *)&d.data == 0) { // new note off
            // DON'T clear velocity, we still need to reference it in decay.
            //synth[d.osc].velocity = 0;
            if(synth[d.osc].wave==KS) {
                #if AMY_KS_OSCS > 0
                ks_note_off(d.osc);
                #endif
            }
            else if(synth[d.osc].wave==ALGO) { algo_note_off(d.osc); }
            else if(synth[d.osc].wave==PARTIAL) {
                #if AMY_HAS_PARTIALS == 1
                partial_note_off(d.osc);
                #endif
            }
            else if(synth[d.osc].wave==PARTIALS) {
                #if AMY_HAS_PARTIALS == 1
                partials_note_off(d.osc);
                #endif
            }
            else if(synth[d.osc].wave==PCM) { pcm_note_off(d.osc); }
            else if(synth[d.osc].wave==CUSTOM) {
                #if AMY_HAS_CUSTOM == 1
                custom_note_off(d.osc);
                #endif
            }
            else {
                // osc note off, start release
                AMY_UNSET(synth[d.osc].note_on_clock);
                synth[d.osc].note_off_clock = total_samples;
            }
        }
        // Now maybe propagate the velocity event to the chained osc.
        if (AMY_IS_SET(synth[d.osc].chained_osc)) {
            d.osc = synth[d.osc].chained_osc;
            // Recurse with the new osc.
            play_event(d);
        }
    }
    AMY_PROFILE_STOP(PLAY_EVENT)
}

float combine_controls(float *controls, float *coefs) {
    float result = 0;
    for (int i = 0; i < NUM_COMBO_COEFS; ++i)
        result += controls[i] * coefs[i];
    return result;
}

float combine_controls_mult(float *controls, float *coefs) {
    float result = 1.0;
    for (int i = 0; i < NUM_COMBO_COEFS; ++i)
        if (coefs[i] != 0)
            result *= controls[i] * coefs[i];
    return result;
}

// apply an mod & bp, if any, to the osc
void hold_and_modify(uint16_t osc) {
    AMY_PROFILE_START(HOLD_AND_MODIFY)
    float ctrl_inputs[NUM_COMBO_COEFS];
    ctrl_inputs[COEF_CONST] = 1.0f;
    ctrl_inputs[COEF_NOTE] = (AMY_IS_SET(synth[osc].midi_note)) ? logfreq_for_midi_note(synth[osc].midi_note) : 0;
    ctrl_inputs[COEF_VEL] = synth[osc].velocity;
    ctrl_inputs[COEF_EG0] = S2F(compute_breakpoint_scale(osc, 0));
    ctrl_inputs[COEF_EG1] = S2F(compute_breakpoint_scale(osc, 1));
    ctrl_inputs[COEF_MOD] = S2F(compute_mod_scale(osc));

    msynth[osc].last_pan = msynth[osc].pan;

    // copy all the modifier variables
    msynth[osc].logfreq = combine_controls(ctrl_inputs, synth[osc].logfreq_coefs);
    msynth[osc].filter_logfreq = combine_controls(ctrl_inputs, synth[osc].filter_logfreq_coefs);
    msynth[osc].duty = combine_controls(ctrl_inputs, synth[osc].duty_coefs);
    msynth[osc].pan = combine_controls(ctrl_inputs, synth[osc].pan_coefs);
    // amp is a special case - coeffs apply in log domain.
    msynth[osc].amp = combine_controls_mult(ctrl_inputs, synth[osc].amp_coefs);
    if (msynth[osc].amp <= 0.001)  msynth[osc].amp = 0;

    msynth[osc].feedback = synth[osc].feedback;
    msynth[osc].resonance = synth[osc].resonance;

    if (osc == 999) {
        fprintf(stderr, "h&m: time %f osc %d note %f vel %f eg0 %f eg1 %f ampc %.3f %.3f %.3f %.3f %.3f %.3f lfqc %.3f %.3f %.3f %.3f %.3f %.3f amp %f logfreq %f\n",
               total_samples / (float)AMY_SAMPLE_RATE, osc,
               ctrl_inputs[COEF_NOTE], ctrl_inputs[COEF_VEL], ctrl_inputs[COEF_EG0], ctrl_inputs[COEF_EG1],
               synth[osc].amp_coefs[0], synth[osc].amp_coefs[1], synth[osc].amp_coefs[2], synth[osc].amp_coefs[3], synth[osc].amp_coefs[4], synth[osc].amp_coefs[5],
               synth[osc].logfreq_coefs[0], synth[osc].logfreq_coefs[1], synth[osc].logfreq_coefs[2], synth[osc].logfreq_coefs[3], synth[osc].logfreq_coefs[4], synth[osc].logfreq_coefs[5],
               msynth[osc].amp, msynth[osc].logfreq);

    // Stop oscillators if amp is zero for several frames in a row.
    // Note: We can't wait for the note off because we need to turn off PARTIAL oscs when envelopes end, even if no note off.
    }
#define MIN_ZERO_AMP_TIME_SAMPS (10 * AMY_BLOCK_SIZE)
    if(AMY_IS_SET(synth[osc].zero_amp_clock)) {
        if (msynth[osc].amp > 0) {
            AMY_UNSET(synth[osc].zero_amp_clock);
        } else {
            if ( (total_samples - synth[osc].zero_amp_clock) >= MIN_ZERO_AMP_TIME_SAMPS) {
                //printf("h&m: time %f osc %d OFF\n", total_samples/(float)AMY_SAMPLE_RATE, osc);
                synth[osc].status = STATUS_OFF;
                AMY_UNSET(synth[osc].note_off_clock);
                AMY_UNSET(synth[osc].zero_amp_clock);
            }
        }
    } else if (msynth[osc].amp == 0) {
        synth[osc].zero_amp_clock = total_samples;
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
            stereo_dest[i] += MUL8F_SS(gain_l, mono_src[i]);
            stereo_dest[AMY_BLOCK_SIZE + i] += MUL8F_SS(gain_r, mono_src[i]);
            gain_l += d_gain_l;
            gain_r += d_gain_r;
        }
    }
    AMY_PROFILE_STOP(MIX_WITH_PAN)
}

SAMPLE render_osc_wave(uint16_t osc, uint8_t core, SAMPLE* buf) {
    // Returns abs max of what it wrote.
    SAMPLE max_val = 0;
    AMY_PROFILE_START(RENDER_OSC_WAVE)
    // fill buf with next block_size of samples for specified osc.
    for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) { buf[i] = 0; }
    hold_and_modify(osc); // apply bp / mod
    if(!(msynth[osc].amp == 0 && synth[osc].last_amp == 0)) {
        if(synth[osc].wave == NOISE) max_val = render_noise(buf, osc);
        if(synth[osc].wave == SAW_DOWN) max_val = render_saw_down(buf, osc);
        if(synth[osc].wave == SAW_UP) max_val = render_saw_up(buf, osc);
        if(synth[osc].wave == PULSE) max_val = render_pulse(buf, osc);
        if(synth[osc].wave == TRIANGLE) max_val = render_triangle(buf, osc);
        if(synth[osc].wave == SINE) max_val = render_sine(buf, osc);
        if(synth[osc].wave == KS) {
            #if AMY_KS_OSCS > 0
            max_val = render_ks(buf, osc);
            #endif
        }
        if(pcm_samples)
            if(synth[osc].wave == PCM) max_val = render_pcm(buf, osc);
        if(synth[osc].wave == ALGO) max_val = render_algo(buf, osc, core);
        if(AMY_HAS_PARTIALS == 1) {
            if(synth[osc].wave == PARTIAL) max_val = render_partial(buf, osc);
            if(synth[osc].wave == PARTIALS) max_val = render_partials(buf, osc);
        }
    }
    if(AMY_HAS_CUSTOM == 1) {
        if(synth[osc].wave == CUSTOM) max_val = render_custom(buf, osc);
    }
    AMY_PROFILE_STOP(RENDER_OSC_WAVE)
    return max_val;
}

void amy_render(uint16_t start, uint16_t end, uint8_t core) {
    AMY_PROFILE_START(AMY_RENDER)
    for(uint16_t i=0;i<AMY_BLOCK_SIZE*AMY_NCHANS;i++) { fbl[core][i] = 0; }
    SAMPLE max_max = 0;
    for(uint16_t osc=start; osc<end; osc++) {
        if(synth[osc].status==AUDIBLE) { // skip oscs that are silent or mod sources from playback
            SAMPLE max_val = render_osc_wave(osc, core, per_osc_fb[core]);
            if (max_val > max_max) max_max = max_val;
            // check it's not off, just in case. todo, why do i care?
            if(synth[osc].wave != WAVE_OFF) {
                // apply filter to osc if set
                if(synth[osc].filter_type != FILTER_NONE) filter_process(per_osc_fb[core], osc, max_val);
                mix_with_pan(fbl[core], per_osc_fb[core], msynth[osc].last_pan, msynth[osc].pan);
                //printf("render5 %d %d %d %d\n", osc, start, end, core);

            }
        }

    }

    if (debug_flag) {
        debug_flag = 0;  // Only do this once each time debug_flag is set.
        SAMPLE smax = scan_max(fbl[core], AMY_BLOCK_SIZE);
        fprintf(stderr, "time %" PRIu32 " core %d max_max=%.3f post-eq max=%.3f\n", total_samples, core, S2F(max_max), S2F(smax));
    }

    AMY_PROFILE_STOP(AMY_RENDER)

}

// on all platforms, sysclock is based on total samples played, using audio out (i2s or etc) as system clock
uint32_t amy_sysclock() {
    return (uint32_t)((total_samples / (float)AMY_SAMPLE_RATE) * 1000);
}


void amy_increase_volume() {
    amy_global.volume += 0.5f;
    if(amy_global.volume > MAX_VOLUME) amy_global.volume = MAX_VOLUME;    
}

void amy_decrease_volume() {
    amy_global.volume -= 0.5f;
    if(amy_global.volume < 0) amy_global.volume = 0;    
}

// this takes scheduled events and plays them at the right time
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

    // find any events that need to be played from the (in-order) queue
    struct delta *event_start = amy_global.event_start;
    if (amy_global.event_qsize > 1 && event_start->time == UINT32_MAX) {
        fprintf(stderr, "WARN: time=UINT32_MAX found at qsize=%d\n", amy_global.event_qsize);
    }
    while(sysclock >= event_start->time) {
        play_event(*event_start);
        event_start->time = UINT32_MAX;
        amy_global.event_qsize--;
        event_start = event_start->next;
    }
    amy_global.event_start = event_start;

#if defined ESP_PLATFORM && !defined ARDUINO
    // give the mutex back
    xSemaphoreGive(xQueueSemaphore);
#elif defined _POSIX_THREADS
    pthread_mutex_unlock(&amy_queue_lock);
#endif
    
    if(AMY_HAS_CHORUS==1) {
        hold_and_modify(CHORUS_MOD_SOURCE);
        if(chorus.level!=0)  render_osc_wave(CHORUS_MOD_SOURCE, 0 /* core */, delay_mod);
    }
    AMY_PROFILE_STOP(AMY_PREPARE_BUFFER)

}

int16_t * amy_simple_fill_buffer() {
    amy_prepare_buffer();
    amy_render(0, AMY_OSCS, 0);
    return amy_fill_buffer();
}

int16_t * amy_fill_buffer() {
    AMY_PROFILE_START(AMY_FILL_BUFFER)
    // mix results from both cores.
    if(AMY_CORES==2) {
        for (int16_t i=0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)  fbl[0][i] += fbl[1][i];
    }
    // apply the eq filters if set
    if(amy_global.eq[0] != F2S(1.0f) || amy_global.eq[1] != F2S(1.0f) || amy_global.eq[2] != F2S(1.0f)) {
        parametric_eq_process(fbl[0]);
    }

    if(AMY_HAS_CHORUS==1) {
        // apply chorus.
        if(chorus.level > 0 && delay_lines[0] != NULL) {
            // apply time-varying delays to both chans.
            // delay_mod_val, the modulated delay amount, is set up before calling render_*.
            SAMPLE scale = F2S(1.0f);
            for (int16_t c=0; c < AMY_NCHANS; ++c) {
                apply_variable_delay(fbl[0] + c * AMY_BLOCK_SIZE, delay_lines[c],
                                     delay_mod, scale, chorus.level, 0);
                // flip delay direction for alternating channels.
                scale = -scale;
            }
        }
    }
    if(AMY_HAS_REVERB) {
        // apply reverb.
        if(reverb.level > 0) {
            if(AMY_NCHANS == 1) {
                stereo_reverb(fbl[0], NULL, fbl[0], NULL, AMY_BLOCK_SIZE, reverb.level);
            } else {
                stereo_reverb(fbl[0], fbl[0] + AMY_BLOCK_SIZE, fbl[0], fbl[0] + AMY_BLOCK_SIZE, AMY_BLOCK_SIZE, reverb.level);
            }
        }
    }
    // global volume is supposed to max out at 10, so scale by 0.1.
    SAMPLE volume_scale = F2S(0.1f * amy_global.volume);
    for(int16_t i=0; i < AMY_BLOCK_SIZE; ++i) {
        for (int16_t c=0; c < AMY_NCHANS; ++c) {

            // Convert the mixed sample into the int16 range, applying overall gain.
            SAMPLE fsample = MUL8F_SS(volume_scale, fbl[0][i + c * AMY_BLOCK_SIZE]);

            // One-pole high-pass filter to remove large low-frequency excursions from
            // some FM patches. b = [1 -1]; a = [1 -0.995]
            //SAMPLE new_state = fsample + MUL8F_SS(F2S(0.995f), amy_global.hpf_state);  // MUL8 is critical here.
#ifdef HPF_OUTPUT
            SAMPLE new_state = fsample + amy_global.hpf_state - SHIFTR(amy_global.hpf_state, 8);  // i.e. 0.9961*hpf_state
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
    total_samples += AMY_BLOCK_SIZE;
    AMY_PROFILE_STOP(AMY_FILL_BUFFER)
    return block;
}

uint32_t ms_to_samples(uint32_t ms) {
    return (uint32_t)(((float)ms / 1000.0) * (float)AMY_SAMPLE_RATE);
}

float atoff(const char *s) {
    // Returns float value corresponding to parseable prefix of s.
    // Unlike atof(), it does not recognize scientific format ('e' or 'E')
    // and will stop parsing there.  Needed for message strings that contain
    // 'e' as a command prefix.
    float frac = 0;
    // Skip leading spaces.
    while (*s == ' ') ++s;
    float whole = (float)atoi(s);
    int is_negative = (s[0] == '-');  // Can't use (whole < 0) because of "-0.xx".
    //const char *s_in = s;  // for debug message.
    s += strspn(s, "-0123456789");
    if (*s == '.') {
        // Float with a decimal part.
        // Step over dp
        ++s;
        // Extract fractional part.
        int fraclen = strspn(s, "0123456789");
        char fracpart[8];
        // atoi() will overflow for values larger than 2^31, so only decode a prefix.
        if (fraclen > 6) {
            for(int i = 0; i < 7; ++i) {
                fracpart[i] = s[i];
            }
            fracpart[7] = '\0';
            s = fracpart;
            fraclen = 7;
        }
        frac = (float)atoi(s);
        frac /= powf(10.f, (float)fraclen);
        if (is_negative) frac = -frac;
    }
    //fprintf(stderr, "input was %s output is %f + %f = %f\n", s_in, whole, frac, whole+frac);
    return whole + frac;
}

int parse_float_list_message(char *message, float *vals, int max_num_vals) {
    // Return the number of values extracted from message.
    uint16_t c = 0;
    uint16_t stop = strspn(message, " 0123456789-,.");  // Note space & period.
    int num_vals_received = 0;
    while(c < stop && num_vals_received < max_num_vals) {
        if(message[c] != ',') {
            *vals++ = atoff(message + c);
            ++num_vals_received;
        }
        while(message[c] != ',' && message[c] != 0 && c < MAX_MESSAGE_LEN) c++;
        c++;
    }
    return num_vals_received;
}

int parse_int_list_message(char *message, int16_t *vals, int max_num_vals) {
    // Return the number of values extracted from message.
    uint16_t c = 0;
    uint16_t stop = strspn(message, " 0123456789-,");  // Space, no period.
    int num_vals_received = 0;
    while(c < stop && num_vals_received < max_num_vals) {
        if(message[c] != ',') {
            *vals++ = atoi(message + c);
            ++num_vals_received;
        }
        while(message[c] != ',' && message[c] != 0 && c < MAX_MESSAGE_LEN) c++;
        c++;
    }
    return num_vals_received;
}

void copy_param_list_substring(char *dest, const char *src) {
    // Copy wire command string up to next parameter char.
    uint16_t c = 0;
    uint16_t stop = strspn(src, " 0123456789-,.");  // Note space & period.
    while (c < stop && src[c]) {
        dest[c] = src[c];
        c++;
    }
    dest[c] = '\0';
}

// helper to parse the list of source voices for an algorithm
void parse_algorithm_source(struct synthinfo * e, char *message) {
    parse_int_list_message(message, e->algo_source, MAX_ALGO_OPS);
}

// helper to parse the special bp string
void parse_breakpoint(struct synthinfo * e, char* message, uint8_t which_bpset) {
    float vals[2 * MAX_BREAKPOINTS];
    // Read all the values as floats.
    int num_vals = parse_float_list_message(message, vals, 2 * MAX_BREAKPOINTS);
    // Distribute out to times and vals, casting times to ints.
    for (int i = 0; i < num_vals; ++i) {
        if ((i % 2) == 0)
            e->breakpoint_times[which_bpset][i >> 1] = ms_to_samples((int)vals[i]);
        else
            e->breakpoint_values[which_bpset][i >> 1] = vals[i];
    }
    // Unset remaining vals.
    for (int i = num_vals; i < 2 * MAX_BREAKPOINTS; ++i) {
        if ((i % 2) == 0)
            AMY_UNSET(e->breakpoint_times[which_bpset][i >> 1]);
        else
            AMY_UNSET(e->breakpoint_values[which_bpset][i >> 1]);
    }
}

void parse_coef_message(char *message, float *coefs) {
    int num_coefs = parse_float_list_message(message, coefs, NUM_COMBO_COEFS);
    // Clear the unspecified coefs to zero.
    for (int i = num_coefs; i < NUM_COMBO_COEFS; ++i)
        coefs[i] = 0;
}

// given a string return an event
struct event amy_parse_message(char * message) {
    AMY_PROFILE_START(AMY_PARSE_MESSAGE)
    uint8_t mode = 0;
    uint16_t start = 0;
    uint16_t c = 0;
    int16_t length = strlen(message);
    struct event e = amy_default_event();
    uint32_t sysclock = amy_sysclock();

    //printf("parse_message: %s\n", message);
    
    // cut the osc cruft max etc add, they put a 0 and then more things after the 0
    int new_length = length;
    for(int d=0;d<length;d++) {
        if(message[d] == 0) { new_length = d; d = length + 1;  }
    }
    length = new_length;
    //fprintf(stderr, "%s\n", message);

    while(c < length+1) {
        uint8_t b = message[c];
        //if(b == '_' && c==0) sync_response = 1;
        if( ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z')) || b == 0) {  // new mode or end
            if(mode=='t') {
                e.time=atol(message + start);
                // if we have set latency AND haven't yet synced our times, do it now
                if(amy_global.latency_ms != 0) {
                    if(!computed_delta_set) {
                        computed_delta = e.time - sysclock;
                        //fprintf(stderr,"setting computed delta to %lld (e.time is %lld sysclock %lld) max_drift_ms %d latency %d\n", computed_delta, e.time, sysclock, AMY_MAX_DRIFT_MS, amy_global.latency_ms);
                        computed_delta_set = 1;
                    }
                }
            } else {
                if(mode >= 'A' && mode <= 'z') {
                    switch(mode) {
                        case 'a': parse_coef_message(message + start, e.amp_coefs);break;
                        case 'A': copy_param_list_substring(e.bp0, message+start); e.bp_is_set[0] = 1; break;
                        case 'B': copy_param_list_substring(e.bp1, message+start); e.bp_is_set[1] = 1; break;
                        case 'b': e.feedback=atoff(message+start); break;
                        case 'c': e.chained_osc = atoi(message + start); break;
                        case 'C': e.clone_osc = atoi(message + start); break;
                        case 'd': parse_coef_message(message + start, e.duty_coefs);break;
                        case 'D': show_debug(atoi(message + start)); break;
                        case 'f': parse_coef_message(message + start, e.freq_coefs);break;
                        case 'F': parse_coef_message(message + start, e.filter_freq_coefs); break;
                        case 'G': e.filter_type = atoi(message + start); break;
                        case 'g': e.mod_target = atoi(message + start);  break;
                        case 'H': if(AMY_HAS_REVERB) config_reverb(S2F(reverb.level), atoff(message + start), reverb.damping, reverb.xover_hz); break;
                        case 'h': if(AMY_HAS_REVERB) config_reverb(atoff(message + start), reverb.liveness, reverb.damping, reverb.xover_hz); break;
                        case 'I': e.ratio = atoff(message + start); break;
                        case 'j': if(AMY_HAS_REVERB)config_reverb(S2F(reverb.level), reverb.liveness, atoff(message + start), reverb.xover_hz); break;
                        case 'J': if(AMY_HAS_REVERB)config_reverb(S2F(reverb.level), reverb.liveness, reverb.damping, atoff(message + start)); break;
                        // chorus.level 
                        case 'k': if(AMY_HAS_CHORUS)config_chorus(atoff(message + start), chorus.max_delay, chorus.lfo_freq, chorus.depth); break;
                        case 'K': e.load_patch = atoi(message+start); break; 
                        case 'l': e.velocity=atoff(message + start); break;
                        case 'L': e.mod_source=atoi(message + start); break;
                        // chorus.lfo_freq
                        case 'M': if(AMY_HAS_CHORUS)config_chorus(S2F(chorus.level), chorus.max_delay, atoff(message + start), chorus.depth); break;
                        // chorus.max_delay
                        case 'm': if(AMY_HAS_CHORUS)config_chorus(S2F(chorus.level), atoi(message + start), chorus.lfo_freq, chorus.depth); break;
                        case 'N': e.latency_ms = atoi(message + start);  break;
                        case 'n': e.midi_note=atoi(message + start); break;
                        case 'o': e.algorithm=atoi(message+start); break;
                        case 'O': copy_param_list_substring(e.algo_source, message+start); break;
                        case 'p': e.patch=atoi(message + start); break;
                        case 'P': e.phase=F2P(atoff(message + start)); break;
                        case 'Q': parse_coef_message(message + start, e.pan_coefs); break;
                        // chorus.depth
                        case 'q': if(AMY_HAS_CHORUS)config_chorus(S2F(chorus.level), chorus.max_delay, chorus.lfo_freq, atoff(message+start)); break;
                        case 'R': e.resonance=atoff(message + start); break;
                        case 'r': copy_param_list_substring(e.voices, message+start); break; 
                        case 'S': e.reset_osc = atoi(message + start); break;
                        case 'T': e.bp0_target = atoi(message + start);  break;
                        case 'W': e.bp1_target = atoi(message + start);  break;
                        case 'v': e.osc=((atoi(message + start)) % AMY_OSCS);  break; // allow osc wraparound
                        case 'V': e.volume = atoff(message + start); break;
                        case 'w': e.wave=atoi(message + start); break;
                        case 'x': e.eq_l = atoff(message+start); break;
                        case 'y': e.eq_m = atoff(message+start); break;
                        case 'z': e.eq_h = atoff(message+start); break;
                        default:
                            break;
                            // If a parse callback function is declared, call it to see if there's something else to parse
                            //(*amy_parse_callback)(mode, message+start);
                            //break;
                    }
                }
            }
            mode=b;
            start=c+1;
        }
        c++;
    }
    AMY_PROFILE_STOP(AMY_PARSE_MESSAGE)

    // Only do this if we got some data
    if(length >0) {
        // TODO -- should time adjustment happen during parsing or playback?

        // Now adjust time in some useful way:
        // if we have a delta OR latency is 0 , AND got a time in this message, use it schedule it properly
        if(( (computed_delta_set || amy_global.latency_ms==0) && e.time > 0)) {
            // OK, so check for potentially negative numbers here (or really big numbers-sysclock)
            int32_t potential_time = (int32_t)((int32_t)e.time - (int32_t)computed_delta) + amy_global.latency_ms;
            if(potential_time < 0 || (potential_time > (int32_t)(sysclock + amy_global.latency_ms + AMY_MAX_DRIFT_MS))) {
                //fprintf(stderr,"recomputing time base: message came in with %lld, mine is %lld, computed delta was %lld\n", e.time, sysclock, computed_delta);
                computed_delta = e.time - sysclock;
                //fprintf(stderr,"computed delta now %lld\n", computed_delta);
            }
            e.time = (e.time - computed_delta) + amy_global.latency_ms;
        } else { // else play it asap
            e.time = sysclock + amy_global.latency_ms;
        }
        e.status = SCHEDULED;
        return e;
        
    }
    return amy_default_event();
}

// given a string play / schedule the event directly
void amy_play_message(char *message) {
    //fprintf(stderr, "amy_play_message: %s\n", message);
    struct event e = amy_parse_message(message);
    if(e.status == SCHEDULED) {
        amy_add_event(e);
    }
}


// amy_play_message -> amy_parse_message -> amy_add_event -> add_delta_to_queue -> i_events queue -> global event queue

// fill_audio_buffer_task -> read delta global event queue -> play_event -> apply delta to synth[d.osc]

void amy_stop() {
    oscs_deinit();
}

void amy_start(uint8_t cores, uint8_t reverb, uint8_t chorus) {
    #ifdef _POSIX_THREADS
        pthread_mutex_init(&amy_queue_lock, NULL);
    #endif
    amy_profiles_init();
    global_init();
    amy_global.cores = cores;
    amy_global.has_chorus = chorus;
    amy_global.has_reverb = reverb;
    oscs_init();
    amy_reset_oscs();
}
