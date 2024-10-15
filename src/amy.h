#ifndef __AMY_H
#define __AMY_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>



// This is for baked in samples that come with AMY. The header file written by `amy_headers.py` writes this.
typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
} pcm_map_t;


#ifndef AMY_CONFIG_H
#define AMY_CONFIG_H amy_config.h
#endif

#define QUOTED(x) #x
#define INCLUDE(x) QUOTED(x)

#include INCLUDE(AMY_CONFIG_H)

// Rest of amy setup
#define SAMPLE_MAX 32767
#define MAX_ALGO_OPS 6 
#define MAX_BREAKPOINTS 8
#define MAX_BREAKPOINT_SETS 2
#define THREAD_USLEEP 500
#define AMY_BYTES_PER_SAMPLE 2

// Constants for filters.c, needed for synth structure.
#define FILT_NUM_DELAYS  4    // Need 4 memories for DFI filters, if used (only 2 for DFII).

//#define LINEAR_INTERP        // use linear interp for oscs
// "The cubic stuff is just showing off.  One would only ever use linear in prod." -- dpwe, May 10 2021 
//#define CUBIC_INTERP         // use cubic interpolation for oscs
typedef int16_t output_sample_type;
// Sample values for modulation sources
#define UP    32767
#define DOWN -32768

// Magic value for "0 Hz" in log-scale.
#define ZERO_HZ_LOG_VAL -99.0
// Frequency of Midi note 0, used to make logfreq scales.
// Have 0 be midi 60, C4, 261.63 Hz
#define ZERO_LOGFREQ_IN_HZ 261.63
#define ZERO_MIDI_NOTE 60

#define NUM_COMBO_COEFS 7  // 7 control-mixing params: const, note, velocity, env1, env2, mod, pitchbend
enum coefs{
    COEF_CONST = 0,
    COEF_NOTE = 1,
    COEF_VEL = 2,
    COEF_EG0 = 3,
    COEF_EG1 = 4,
    COEF_MOD = 5,
    COEF_BEND = 6,
};

#define MAX_MESSAGE_LEN 255
#define MAX_PARAM_LEN 80
// synth[].filter_type values
#define FILTER_NONE 0
#define FILTER_LPF 1
#define FILTER_BPF 2
#define FILTER_HPF 3
#define FILTER_LPF24 4
// synth[].wave values
#define SINE 0
#define PULSE 1
#define SAW_DOWN 2
#define SAW_UP 3
#define TRIANGLE 4
#define NOISE 5
#define KS 6
#define PCM 7
#define ALGO 8
#define PARTIAL 9
#define PARTIALS 10
#define BYO_PARTIALS 11
#define AUDIO_IN0 12
#define AUDIO_IN1 13
#define CUSTOM 14
#define WAVE_OFF 15

// synth[].status values
#define SYNTH_OFF 0
#define SYNTH_AUDIBLE 1
#define SYNTH_AUDIBLE_SUSPENDED 2
#define SYNTH_IS_MOD_SOURCE 3
#define SYNTH_IS_ALGO_SOURCE 4

// event.status values
#define EVENT_EMPTY 0
#define EVENT_SCHEDULED 1
#define EVENT_TRANSFER_DATA 2


// Envelope generator types (for synth[osc].env_type[eg]).
#define ENVELOPE_NORMAL 0
#define ENVELOPE_LINEAR 1
#define ENVELOPE_DX7 2
#define ENVELOPE_TRUE_EXPONENTIAL 3

// Reset masks
#define RESET_ALL_OSCS 8192
#define RESET_TIMEBASE 16384
#define RESET_AMY 32768


#define true 1
#define false 0

#define AMY_OK 0
typedef int amy_err_t;


#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif



#include "amy_fixedpoint.h"

#if defined ARDUINO && !defined TLONG && !defined ESP_PLATFORM
#include "avr/pgmspace.h" // for PROGMEM, DMAMEM, FASTRUN
#else
#define PROGMEM 
#endif

extern uint8_t (*amy_external_render_hook)(uint16_t, SAMPLE*, uint16_t);


enum params{
    WAVE, PATCH, MIDI_NOTE,              // 0, 1, 2
    AMP,                                 // 3..9
    DUTY=AMP + NUM_COMBO_COEFS,          // 10..16
    FEEDBACK=DUTY + NUM_COMBO_COEFS,     // 17
    FREQ,                                // 18..24
    VELOCITY=FREQ + NUM_COMBO_COEFS,     // 25
    PHASE, DETUNE, VOLUME, PITCH_BEND,   // 26, 26, 28, 29
    PAN,                                 // 30..36
    FILTER_FREQ=PAN + NUM_COMBO_COEFS,   // 37..43
    RATIO=FILTER_FREQ + NUM_COMBO_COEFS, // 44
    RESONANCE, PORTAMENTO, CHAINED_OSC,  // 45, 46, 47
    MOD_SOURCE, FILTER_TYPE,             // 48, 49
    EQ_L, EQ_M, EQ_H,                    // 50, 51, 52
    ALGORITHM, LATENCY,                  // 53, 54
    ALGO_SOURCE_START=100,               // 100..105
    ALGO_SOURCE_END=100+MAX_ALGO_OPS,    // 106
    BP_START=ALGO_SOURCE_END + 1,        // 107..138
    BP_END=BP_START + (MAX_BREAKPOINT_SETS * MAX_BREAKPOINTS * 2), // 139
    EG0_TYPE, EG1_TYPE,                  // 140, 141
    CLONE_OSC,                           // 142
    RESET_OSC,                           // 143
    NO_PARAM                             // 144
};

#ifdef AMY_DEBUG
      


enum itags{
    RENDER_OSC_WAVE, COMPUTE_BREAKPOINT_SCALE, HOLD_AND_MODIFY, FILTER_PROCESS, FILTER_PROCESS_STAGE0,
    FILTER_PROCESS_STAGE1, ADD_DELTA_TO_QUEUE, AMY_ADD_EVENT, PLAY_EVENT,  MIX_WITH_PAN, AMY_RENDER, 
    AMY_PREPARE_BUFFER, AMY_FILL_BUFFER, AMY_PARSE_MESSAGE,RENDER_LUT_FM, RENDER_LUT_FB, RENDER_LUT, 
    RENDER_LUT_CUB, RENDER_LUT_FM_FB, RENDER_LPF_LUT, DSPS_BIQUAD_F32_ANSI_SPLIT_FB, DSPS_BIQUAD_F32_ANSI_SPLIT_FB_TWICE, DSPS_BIQUAD_F32_ANSI_COMMUTED, 
    PARAMETRIC_EQ_PROCESS, HPF_BUF, SCAN_MAX, DSPS_BIQUAD_F32_ANSI, BLOCK_NORM, CALIBRATE, AMY_ESP_FILL_BUFFER, NO_TAG
};
struct profile {
    uint32_t calls;
    uint64_t us_total;
    uint64_t start;
};

extern uint64_t profile_start_us;

#define AMY_PROFILE_INIT(tag) \
    profiles[tag].start = 0; \
    profiles[tag].calls = 0; \
    profiles[tag].us_total = 0; \
    profile_start_us = amy_get_us();

#define AMY_PROFILE_START(tag) \
    profiles[tag].start = amy_get_us();

#define AMY_PROFILE_STOP(tag) \
    profiles[tag].us_total += (amy_get_us()-profiles[tag].start); \
    profiles[tag].calls++;

#define AMY_PROFILE_PRINT(tag) \
    if(profiles[tag].calls) {\
        fprintf(stderr,"%40s: %10"PRIu32" calls %10"PRIu64"us total [%6.2f%% wall %6.2f%% render] %9"PRIu64"us per call\n", \
        profile_tag_name(tag), profiles[tag].calls, profiles[tag].us_total, \
        ((float)(profiles[tag].us_total) / (float)(amy_get_us() - profile_start_us))*100.0, \
        ((float)(profiles[tag].us_total) / (float)(profiles[AMY_RENDER].us_total))*100.0, \
        (profiles[tag].us_total)/profiles[tag].calls);\
    }
#else
#define AMY_PROFILE_START(tag)
#define AMY_PROFILE_STOP(tag)

#endif

extern void amy_profiles_init();
extern void amy_profiles_print();





#include <limits.h>
static inline int isnan_c11(float test)
{
    return isnan(test);
}


#define AMY_UNSET_FLOAT nanf("")

#define AMY_UNSET_VALUE(var) _Generic((var), \
    float: AMY_UNSET_FLOAT, \
    uint32_t: UINT32_MAX, \
    uint16_t: UINT16_MAX, \
    int16_t: SHRT_MAX, \
    uint8_t: UINT8_MAX, \
    int8_t: SCHAR_MAX, \
    int32_t: INT_MAX \
)

#define AMY_UNSET(var) var = AMY_UNSET_VALUE(var)

#define AMY_IS_UNSET(var) _Generic((var), \
    float: isnan_c11(var), \
    default: var==AMY_UNSET_VALUE(var) \
)


#define AMY_IS_SET(var) !AMY_IS_UNSET(var)


// Delta holds the individual changes from an event, it's sorted in order of playback time 
// this is more efficient in memory than storing entire events per message 
struct delta {
    uint32_t data; // casted to the right thing later
    enum params param; // which parameter is being changed
    uint32_t time; // what time to play / change this parameter
    uint16_t osc; // which oscillator it impacts
    struct delta * next; // the next event, in time 
};


// API accessible events, are converted to delta types for the synth to play back 
struct event {
    uint32_t time;
    uint16_t osc;
    uint16_t wave;
    int16_t patch;  // Negative patch is voice count for build-your-own PARTIALS
    float midi_note;
    uint16_t load_patch;
    float amp_coefs[NUM_COMBO_COEFS];
    float freq_coefs[NUM_COMBO_COEFS];
    float filter_freq_coefs[NUM_COMBO_COEFS];
    float duty_coefs[NUM_COMBO_COEFS];
    float pan_coefs[NUM_COMBO_COEFS];
    float feedback;
    float velocity;
    float phase;
    float detune;
    float volume;
    float pitch_bend;
    uint16_t latency_ms;
    float ratio;
    float resonance;
    uint16_t portamento_ms;
    uint16_t chained_osc;
    uint16_t mod_source;
    uint8_t algorithm;
    uint8_t filter_type;
    float eq_l;
    float eq_m;
    float eq_h;
    uint16_t bp_is_set[MAX_BREAKPOINT_SETS];
    char algo_source[MAX_PARAM_LEN];
    char bp0[MAX_PARAM_LEN];
    char bp1[MAX_PARAM_LEN];
    uint8_t eg_type[MAX_BREAKPOINT_SETS];
    char voices[MAX_PARAM_LEN];
    uint16_t reset_osc;
    uint8_t status;
};

// This is the state of each oscillator, set by the sequencer from deltas
struct synthinfo {
    uint16_t osc; // self-reference
    uint16_t wave;
    int16_t patch;  // Negative patch is voice count for build-your-own PARTIALS
    float midi_note;
    float amp_coefs[NUM_COMBO_COEFS];
    float logfreq_coefs[NUM_COMBO_COEFS];
    float filter_logfreq_coefs[NUM_COMBO_COEFS];
    float duty_coefs[NUM_COMBO_COEFS];
    float pan_coefs[NUM_COMBO_COEFS];
    float feedback;
    uint8_t status;
    float velocity;
    PHASOR trigger_phase;
    PHASOR phase;
    float detune;
    float step;
    float substep;
    SAMPLE sample;  // Used by KS, otherwise?
    SAMPLE mod_value;  // last value returned by this oscillator when acting as a MOD_SOURCE.
    float volume;
    float logratio;
    float resonance;
    float portamento_alpha;
    uint16_t chained_osc;
    uint16_t mod_source;
    uint8_t algorithm;
    uint8_t filter_type;
    // algo_source remains int16 because users can add -1 to indicate no osc 
    int16_t algo_source[MAX_ALGO_OPS];

    uint32_t render_clock;
    uint32_t note_on_clock;
    uint32_t note_off_clock;
    uint32_t zero_amp_clock;   // Time amplitude hits zero.
    uint32_t mod_value_clock;  // Only calculate mod_value once per frame (for mod_source).
    uint32_t breakpoint_times[MAX_BREAKPOINT_SETS][MAX_BREAKPOINTS];
    float breakpoint_values[MAX_BREAKPOINT_SETS][MAX_BREAKPOINTS];
    uint8_t eg_type[MAX_BREAKPOINT_SETS];  // one of the ENVELOPE_ values
    SAMPLE last_scale[MAX_BREAKPOINT_SETS];  // remembers current envelope level, to use as start point in release.
  
    // State variable for the dc-removal filter.
    SAMPLE hpf_state[2];
    // Constant offset to add to sawtooth before integrating.
    SAMPLE dc_offset;
    // Selected lookup table and size.
    const LUT *lut;
    float eq_l;
    float eq_m;
    float eq_h;
    // For ALGO feedback ops
    SAMPLE last_two[2];
    // For filters.  Need 2x because LPF24 uses two instances of filter.
    SAMPLE filter_delay[2 * FILT_NUM_DELAYS];
    // The block-floating-point shift of the filter delay values.
    int last_filt_norm_bits;
};

// synthinfo, but only the things that mods/env can change. one per osc
struct mod_synthinfo {
    float amp;
    float last_amp;    // amplitude smoother
    float pan;
    float last_pan;   // Pan history for interpolation.
    float duty;
    float last_duty;   // Duty history for interpolation.
    float logfreq;
    float last_logfreq;  // for portamento
    float filter_logfreq;
    float last_filter_logfreq;  // filter freq history for smoothing.
    float resonance;
    float feedback;
};


extern struct synthinfo* synth;
extern struct mod_synthinfo* msynth;
extern struct mod_state mglobal;
#ifdef AMY_DEBUG
extern struct profile profiles[NO_TAG];
extern int64_t amy_get_us();
#endif

extern uint8_t amy_transfer_flag ;


// Chorus gets is modulator from a special osc one beyond the normal range.
#define CHORUS_MOD_SOURCE AMY_OSCS



struct event amy_default_event();
void amy_add_event(struct event e);
void amy_add_event_internal(struct event e, uint16_t base_osc);
int16_t * amy_simple_fill_buffer() ;
int web_audio_buffer(float *samples, int length);
void amy_render(uint16_t start, uint16_t end, uint8_t core);
void show_debug(uint8_t type) ;
void oscs_deinit() ;
uint32_t amy_sysclock();
float freq_for_midi_note(float midi_note);
float logfreq_for_midi_note(float midi_note);
float midi_note_for_logfreq(float logfreq);
float logfreq_of_freq(float freq);
float freq_of_logfreq(float logfreq);
int8_t check_init(amy_err_t (*fn)(), char *name);
void amy_increase_volume();
void amy_decrease_volume();
void * malloc_caps(uint32_t size, uint32_t flags);
void config_reverb(float level, float liveness, float damping, float xover_hz);
void config_chorus(float level, int max_delay, float lfo_freq, float depth);
void osc_note_on(uint16_t osc, float initial_freq);
void chorus_note_on(float initial_freq);

SAMPLE log2_lut(SAMPLE x);
SAMPLE exp2_lut(SAMPLE x);

// global synth state
struct state {
    uint8_t cores;
    uint8_t has_reverb;
    uint8_t has_chorus;
    uint8_t has_echo;
    float volume;
    float pitch_bend;
    // State of fixed dc-blocking HPF
    SAMPLE hpf_state;
    SAMPLE eq[3];
    uint16_t event_qsize;
    int16_t next_event_write;
    struct delta * event_start; // start of the sorted list
    int16_t latency_ms;
};

// custom oscillator
struct custom_oscillator {
    void (*init)(void);
    void (*note_on)(uint16_t osc, float freq);
    void (*note_off)(uint16_t osc);
    void (*mod_trigger)(uint16_t osc);
    SAMPLE (*render)(SAMPLE* buf, uint16_t osc);
    SAMPLE (*compute_mod)(uint16_t osc);
};

// Shared structures
extern uint32_t total_samples;
extern struct synthinfo *synth;
extern struct mod_synthinfo *msynth; // the synth that is being modified by modulations & envelopes
extern struct state amy_global; 


float atoff(const char *s);
int8_t oscs_init();
void patches_init();
int parse_breakpoint(struct synthinfo * e, char* message, uint8_t bp_set) ;
void parse_algorithm_source(struct synthinfo * e, char* message) ;
void hold_and_modify(uint16_t osc) ;
void amy_prepare_buffer();
int16_t * amy_fill_buffer();

uint32_t ms_to_samples(uint32_t ms) ;


// external functions
void amy_play_message(char *message);
struct event amy_parse_message(char * message);
void amy_restart();
void amy_start(uint8_t cores, uint8_t reverb, uint8_t chorus, uint8_t echo);
void amy_stop();
void amy_live_start();
void amy_live_stop();
void amy_reset_oscs();
void amy_print_devices();
void amy_set_custom(struct custom_oscillator* custom);
void amy_reset_sysclock();

extern int parse_int_list_message32(char *message, int32_t *vals, int max_num_vals, int32_t skipped_val);
extern int parse_int_list_message16(char *message, int16_t *vals, int max_num_vals, int16_t skipped_val);
extern void reset_osc(uint16_t i );


extern float render_am_lut(float * buf, float step, float skip, float incoming_amp, float ending_amp, const float* lut, int16_t lut_size, float *mod, float bandwidth);
extern void ks_init();
extern void ks_deinit();
extern void algo_init();
extern void algo_deinit();
extern void pcm_init();
extern void custom_init();
extern SAMPLE render_ks(SAMPLE * buf, uint16_t osc); 
extern SAMPLE render_sine(SAMPLE * buf, uint16_t osc); 
extern SAMPLE render_fm_sine(SAMPLE *buf, uint16_t osc, SAMPLE *mod, SAMPLE feedback_level, uint16_t algo_osc, SAMPLE mod_amp);
extern SAMPLE render_pulse(SAMPLE * buf, uint16_t osc); 
extern SAMPLE render_saw_down(SAMPLE * buf, uint16_t osc);
extern SAMPLE render_saw_up(SAMPLE * buf, uint16_t osc);
extern SAMPLE render_triangle(SAMPLE * buf, uint16_t osc); 
extern SAMPLE render_noise(SAMPLE * buf, uint16_t osc); 
extern SAMPLE render_pcm(SAMPLE * buf, uint16_t osc);
extern SAMPLE render_algo(SAMPLE * buf, uint16_t osc, uint8_t core) ;
extern SAMPLE render_partial(SAMPLE *buf, uint16_t osc) ;
extern void partials_note_on(uint16_t osc);
extern void partials_note_off(uint16_t osc);
extern void patches_load_patch(struct event e); 
extern void patches_event_has_voices(struct event e);
extern void patches_reset();
extern void patches_debug();
extern void patches_store_patch(char * message);

extern SAMPLE render_partials(SAMPLE *buf, uint16_t osc);
extern SAMPLE render_custom(SAMPLE *buf, uint16_t osc) ;

extern SAMPLE compute_mod_pulse(uint16_t osc);
extern SAMPLE compute_mod_noise(uint16_t osc);
extern SAMPLE compute_mod_sine(uint16_t osc);
extern SAMPLE compute_mod_saw_up(uint16_t osc);
extern SAMPLE compute_mod_saw_down(uint16_t osc);
extern SAMPLE compute_mod_triangle(uint16_t osc);
extern SAMPLE compute_mod_pcm(uint16_t osc);
extern SAMPLE compute_mod_custom(uint16_t osc);

extern void sine_note_on(uint16_t osc, float initial_freq); 
extern void fm_sine_note_on(uint16_t osc, uint16_t algo_osc); 
extern void saw_down_note_on(uint16_t osc, float initial_freq); 
extern void saw_up_note_on(uint16_t osc, float initial_freq); 
extern void triangle_note_on(uint16_t osc, float initial_freq); 
extern void pulse_note_on(uint16_t osc, float initial_freq); 
extern void noise_note_on(uint16_t osc);
extern void pcm_note_on(uint16_t osc);
extern void pcm_note_off(uint16_t osc);
extern void custom_note_on(uint16_t osc, float freq);
extern void custom_note_off(uint16_t osc);
extern void partial_note_on(uint16_t osc);
extern void partial_note_off(uint16_t osc);
extern void algo_note_on(uint16_t osc);
extern void algo_note_off(uint16_t osc);
extern void ks_note_on(uint16_t osc); 
extern void ks_note_off(uint16_t osc);
extern void sine_mod_trigger(uint16_t osc);
extern void saw_down_mod_trigger(uint16_t osc);
extern void saw_up_mod_trigger(uint16_t osc);
extern void triangle_mod_trigger(uint16_t osc);
extern void pulse_mod_trigger(uint16_t osc);
extern void pcm_mod_trigger(uint16_t osc);
extern void custom_mod_trigger(uint16_t osc);
extern SAMPLE amy_get_random();
//extern void algo_custom_setup_patch(uint16_t osc, uint16_t * target_oscs);

extern int16_t * pcm_load(uint16_t patch, uint32_t length, uint32_t samplerate, uint8_t midinote, uint32_t loopstart, uint32_t loopend);
extern void pcm_unload_patch(uint16_t patch_number);
extern void pcm_unload_all_patches();

// filters
extern void filters_init();
extern void filters_deinit();
extern SAMPLE filter_process(SAMPLE * block, uint16_t osc, SAMPLE max_value);
extern void parametric_eq_process(SAMPLE *block);
extern void reset_filter(uint16_t osc);
extern void reset_parametric(void);
extern float dsps_sqrtf_f32_ansi(float f);
extern int8_t dsps_biquad_gen_lpf_f32(SAMPLE *coeffs, float f, float qFactor);
extern int8_t dsps_biquad_f32_ansi(const SAMPLE *input, SAMPLE *output, int len, SAMPLE *coef, SAMPLE *w);
extern SAMPLE scan_max(SAMPLE* block, int len);
// Use the esp32 optimized biquad filter if available
#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t dsps_biquad_f32_ae32(const float *input, float *output, int len, float *coef, float *w);
#else
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_INTERNAL 0
#endif



// envelopes
extern SAMPLE compute_breakpoint_scale(uint16_t osc, uint8_t bp_set, uint16_t sample_offset);
extern SAMPLE compute_mod_scale(uint16_t osc);
extern SAMPLE compute_mod_value(uint16_t mod_osc);
extern void retrigger_mod_source(uint16_t osc);


#endif



