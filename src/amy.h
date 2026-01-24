#ifndef __AMY_H
#define __AMY_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#ifndef __EMSCRIPTEN__
#ifdef _POSIX_THREADS
#include <pthread.h>
extern pthread_mutex_t amy_queue_lock; 
#endif
#endif


// This is for baked in samples that come with AMY. The header file written by `amy/headers.py` writes this.
typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
} pcm_map_t;


// These are overriden for you if you include pcm_X.h {tiny, small, large}
extern const int16_t pcm[];
extern const pcm_map_t pcm_map[];
extern const uint16_t pcm_samples;

#if (defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(ARDUINO) || defined(__IMXRT1062__) || defined(ARDUINO_ARCH_RP2040) ||defined(ARDUINO_ARCH_RP2350))
#define AMY_MCU
#endif

#define MAX_FILENAME_LEN 127



// Set block size and SR. We try for 256/44100, but some platforms don't let us:
#ifdef AMY_DAISY
#define AMY_BLOCK_SIZE 128
#define BLOCK_SIZE_BITS 7 // log2 of BLOCK_SIZE
#else
#define AMY_BLOCK_SIZE 256
#define BLOCK_SIZE_BITS 8 // log2 of BLOCK_SIZE
#endif

#ifdef AMY_DAISY
#define AMY_SAMPLE_RATE 48000
#elif defined __EMSCRIPTEN__
#define AMY_SAMPLE_RATE 48000
#else
#define AMY_SAMPLE_RATE 44100 
#endif

#define PCM_AMY_SAMPLE_RATE 22050

// Transfer types.
#define AMY_TRANSFER_TYPE_NONE 0
#define AMY_TRANSFER_TYPE_AUDIO 1
#define AMY_TRANSFER_TYPE_FILE 2
#define AMY_TRANSFER_TYPE_SAMPLE 3

#define AMY_PCM_TYPE_ROM 0
#define AMY_PCM_TYPE_FILE 1
#define AMY_PCM_TYPE_MEMORY 2

// File-streaming buffer size multiplier (in blocks).
#define PCM_FILE_BUFFER_MULT 8


#define AMY_BUS_OUTPUT 1
#define AMY_BUS_AUDIO_IN 2

// Always use fixed point. You can remove this if you want float
#define AMY_USE_FIXEDPOINT


// upper bounds for static arrays.
#define AMY_MAX_CORES 2          
#define AMY_MAX_CHANNELS 2

// Always use 2 channels. Clients that want mono can deinterleave
#define AMY_NCHANS 2


// Use dual cores on supported platforms
#if (defined (ESP_PLATFORM) || defined (ARDUINO_ARCH_RP2040) ||defined(ARDUINO_ARCH_RP2350))
#define AMY_DUALCORE
#define AMY_CORES 2
#else
#define AMY_CORES 1
#endif

#define AMY_HAS_STARTUP_BLEEP (amy_global.config.features.startup_bleep)
#define AMY_HAS_REVERB (amy_global.config.features.reverb)
#define AMY_HAS_AUDIO_IN (amy_global.config.features.audio_in)
#define AMY_HAS_I2S (amy_global.config.audio == AMY_AUDIO_IS_I2S)
#define AMY_HAS_DEFAULT_SYNTHS (amy_global.config.features.default_synths)
#define AMY_HAS_CHORUS (amy_global.config.features.chorus)
#define AMY_HAS_ECHO (amy_global.config.features.echo)
#define AMY_KS_OSCS amy_global.config.ks_oscs
#define AMY_HAS_PARTIALS  (amy_global.config.features.partials)
#define AMY_HAS_CUSTOM  (amy_global.config.features.custom)
#define AMY_OSCS amy_global.config.max_oscs

// On which MIDI channel to install the default MIDI drums handler.
#define AMY_MIDI_CHANNEL_DRUMS 10


#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#ifndef MALLOC_CAP_DEFAULT
#define MALLOC_CAP_DEFAULT 0
#endif


// 0.5 Hz modulation at 50% depth of 320 samples (i.e., 80..240 samples = 2..6 ms), mix at 0 (inaudible).
#define CHORUS_DEFAULT_LFO_FREQ 0.5
#define CHORUS_DEFAULT_MOD_DEPTH 0.5
#define CHORUS_DEFAULT_LEVEL 0
#define CHORUS_DEFAULT_MAX_DELAY 320
// Chorus gets is modulator from a special osc one beyond the normal range.
#define CHORUS_MOD_SOURCE AMY_OSCS

// center frequencies for the EQ
#define EQ_CENTER_LOW 800.0
#define EQ_CENTER_MED 2500.0
#define EQ_CENTER_HIGH 7000.0

// reverb setup
#define REVERB_DEFAULT_LEVEL 0
#define REVERB_DEFAULT_LIVENESS 0.85f
#define REVERB_DEFAULT_DAMPING 0.5f
#define REVERB_DEFAULT_XOVER_HZ 3000.0f

// echo setup,  Levels etc are SAMPLE (fxpoint), delays are in samples.
#define ECHO_DEFAULT_LEVEL 0
#define ECHO_DEFAULT_DELAY_MS  500.f
// Delay line allocates in 2^n samples at 44k; 743ms is just under 32768 samples.
#define ECHO_DEFAULT_MAX_DELAY_MS 743.f
#define ECHO_DEFAULT_FEEDBACK 0
#define ECHO_DEFAULT_FILTER_COEF 0

#define AMY_SEQUENCER_PPQ 48

#define DELAY_LINE_LEN 512  // 11 ms @ 44 kHz

// D is how close the sample gets to the clip limit before the nonlinearity engages.  
// So D=0.1 means output is linear for -0.9..0.9, then starts clipping.
#define CLIP_D 0.1

#define MAX_VOLUME 11.0

#define AMP_THRESH 0.001f
// A little bit more than 0.001 to avoid FP issues with exactly 0.001.
#define AMP_THRESH_PLUS 0.0011f

// Rest of amy setup
#define SAMPLE_MAX 32767
#define MAX_ALGO_OPS 6 
#define DEFAULT_NUM_BREAKPOINTS 8
// We need a max on the number of breakpoints to lay out the params enum statically.  Otherwise, it's dynamic.
#define MAX_BREAKPOINTS 24
#define MAX_BREAKPOINT_SETS 2
#define THREAD_USLEEP 500
#define AMY_BYTES_PER_SAMPLE 2
// We need some fixed vectors of per-instrument vectors.
#define MAX_VOICES_PER_INSTRUMENT 32

// Constants for filters.c, needed for synth structure.
#define FILT_NUM_DELAYS  4    // Need 4 memories for DFI filters, if used (only 2 for DFII).

typedef int16_t output_sample_type;

// Magic value for "0 Hz" in log-scale.
#define ZERO_HZ_LOG_VAL -99.0
// Frequency of Midi note 0, used to make logfreq scales.
// Have 0 be midi 60, C4, 261.63 Hz
#define ZERO_LOGFREQ_IN_HZ 261.63
#define ZERO_MIDI_NOTE 60

#define NUM_COMBO_COEFS 9  // 9 control-mixing params: const, note, velocity, env1, env2, mod, pitchbend, ext0, ext1
enum coefs{
    COEF_CONST = 0,
    COEF_NOTE = 1,
    COEF_VEL = 2,
    COEF_EG0 = 3,
    COEF_EG1 = 4,
    COEF_MOD = 5,
    COEF_BEND = 6,
    COEF_EXT0 = 7,
    COEF_EXT1 = 8,
};

#define MAX_MESSAGE_LEN 1024
#define MAX_PARAM_LEN 256
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
#define BYO_PARTIALS 10
#define INTERP_PARTIALS 11
#define AUDIO_IN0 12
#define AUDIO_IN1 13
#define AUDIO_EXT0 14
#define AUDIO_EXT1 15
#define AMY_MIDI 16
#define PCM_LEFT 17
#define PCM_RIGHT 18
#define PCM_MIX 7 // same as PCM
#define CUSTOM 19
#define WAVE_OFF 20
#define AMY_WAVE_IS_PCM(w) ((w) == PCM || (w) == PCM_LEFT || (w) == PCM_RIGHT)

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
#define EVENT_SEQUENCE 3

// note_source values
#define NOTE_SOURCE_MIDI 2

// Envelope generator types (for synth[osc].env_type[eg]).
#define ENVELOPE_NORMAL 0
#define ENVELOPE_LINEAR 1
#define ENVELOPE_DX7 2
#define ENVELOPE_TRUE_EXPONENTIAL 3

// Sequence enum
#define SEQUENCE_TICK 0
#define SEQUENCE_PERIOD 1
#define SEQUENCE_TAG 2

// Reset masks
#define RESET_SEQUENCER 4096
#define RESET_ALL_OSCS 8192
#define RESET_TIMEBASE 16384
#define RESET_AMY 32768
#define RESET_EVENTS 65536
#define RESET_ALL_NOTES 131072
#define RESET_SYNTHS 262144  // Non-scheduled release of all synths, voices, oscs prior to load_patch
#define RESET_PATCH 524288  // Clear one patch if patch_number provided, otherwise clear all patches.

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


enum params{
    WAVE, PRESET, MIDI_NOTE,              // 0, 1, 2
    AMP,                                 // 3..11
    DUTY=AMP + NUM_COMBO_COEFS,          // 12..20
    FEEDBACK=DUTY + NUM_COMBO_COEFS,     // 21
    FREQ,                                // 22..30
    VELOCITY=FREQ + NUM_COMBO_COEFS,     // 31
    PHASE, DETUNE, VOLUME, PITCH_BEND,   // 32, 33, 34, 35
    PAN,                                 // 36..44
    FILTER_FREQ=PAN + NUM_COMBO_COEFS,   // 45..53
    RATIO=FILTER_FREQ + NUM_COMBO_COEFS, // 54
    RESONANCE, PORTAMENTO, CHAINED_OSC,  // 55, 56, 57
    MOD_SOURCE, FILTER_TYPE,             // 58, 59
    EQ_L, EQ_M, EQ_H,                    // 60, 61, 62
    ALGORITHM, LATENCY, TEMPO,           // 63, 64, 65
    ALGO_SOURCE_START=100,               // 100..105
    ALGO_SOURCE_END=100+MAX_ALGO_OPS,    // 106
    BP_START=ALGO_SOURCE_END + 1,        // 107..202
    BP_END=BP_START + (MAX_BREAKPOINT_SETS * MAX_BREAKPOINTS * 2), // 203
    EG0_TYPE, EG1_TYPE,                  // 204, 205
    CLONE_OSC,                           // 206
    RESET_OSC,                           // 207
    NOTE_SOURCE,                         // 208
    ECHO_LEVEL,
    ECHO_DELAY_MS,
    ECHO_MAX_DELAY_MS,
    ECHO_FEEDBACK,
    ECHO_FILTER_COEF,
    CHORUS_LEVEL,
    CHORUS_MAX_DELAY,
    CHORUS_LFO_FREQ,
    CHORUS_DEPTH,
    REVERB_LEVEL,
    REVERB_LIVENESS,
    REVERB_DAMPING,
    REVERB_XOVER_HZ,
    NO_PARAM                    // 209
};

///////////////////////////////////////
// Profiler setup

#ifdef AMY_DEBUG
      
enum itags{
    RENDER_OSC_WAVE, COMPUTE_BREAKPOINT_SCALE, HOLD_AND_MODIFY, FILTER_PROCESS, FILTER_PROCESS_STAGE0,
    FILTER_PROCESS_STAGE1, ADD_DELTA_TO_QUEUE, AMY_ADD_DELTA, PLAY_DELTA,  MIX_WITH_PAN, AMY_RENDER, 
    AMY_EXECUTE_DELTAS, AMY_FILL_BUFFER, RENDER_LUT_FM, RENDER_LUT_FB, RENDER_LUT, 
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

extern struct profile profiles[NO_TAG];
extern int64_t amy_get_us();

#else
#define AMY_PROFILE_START(tag)
#define AMY_PROFILE_STOP(tag)

#endif // AMY_DEBUG

extern void amy_profiles_init();
extern void amy_profiles_print();

///////////////////////////////////////
// Default values setup

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
//    char*: *(uint8_t *)var == 0,

#define AMY_IS_SET(var) !AMY_IS_UNSET(var)

// Helpers to identify if param is in a range.
#define PARAM_IS_COMBO_COEF(param, base)   ((param) >= (base) && (param) < (base) + NUM_COMBO_COEFS)
#define PARAM_IS_BP_COEF(param)    ((param) >= BP_START && (param) < BP_END)


///////////////////////////////////////
// Events & deltas


// Delta holds the individual changes from an event, it's sorted in order of playback time 
// this is more efficient in memory than storing entire events per message 
struct delta {
    union d {
        uint32_t i;
        float f;
    } data;
    enum params param; // which parameter is being changed
    uint32_t time; // what time to play / change this parameter
    uint16_t osc; // which oscillator it impacts
    struct delta * next; // the next delta, in time 
};


// API accessible events, are converted to delta types for the synth to play back 
typedef struct amy_event {
    uint32_t time;
    uint16_t osc;
    uint16_t wave;
    int16_t preset;  // Negative preset is voice count for build-your-own PARTIALS
    float midi_note;
    uint16_t patch_number;
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
    float tempo;
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
    // Convert these two at least to vectors of ints, save several hundred bytes
    int16_t algo_source[MAX_ALGO_OPS];
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    char bp0[MAX_PARAM_LEN];
    char bp1[MAX_PARAM_LEN];
    uint8_t eg_type[MAX_BREAKPOINT_SETS];
    // Instrument-layer values.
    uint8_t synth;
    uint32_t synth_flags;  // Special flags to set when defining instruments.
    uint16_t synth_delay_ms;  // Extra delay added to synth note-ons to allow decay on voice-stealing.
    uint8_t to_synth;  // For moving setup between synth numbers.
    uint8_t grab_midi_notes;  // To enable/disable automatic MIDI note-on/off generating note-on/off.
    uint8_t pedal;  // MIDI pedal value.
    uint16_t num_voices;
    uint32_t sequence[3]; // tick, period, tag
    //
    uint8_t status;
    uint8_t note_source;  // .. to mark note on/offs that come from MIDI so we don't send them back out again.
    uint32_t reset_osc;
    // Global effects
    float echo_level;
    float echo_delay_ms;
    float echo_max_delay_ms;
    float echo_feedback;
    float echo_filter_coef;
    float chorus_level;
    float chorus_max_delay;
    float chorus_lfo_freq;
    float chorus_depth;
    float reverb_level;
    float reverb_liveness;
    float reverb_damping;
    float reverb_xover_hz;
} amy_event;

// This is the state of each oscillator, set by the sequencer from deltas
struct synthinfo {
    uint16_t osc; // self-reference
    uint16_t wave;
    int16_t preset;  // Negative preset is voice count for build-your-own PARTIALS
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
    SAMPLE mod_value;  // last value returned by this oscillator when acting as a MOD_SOURCE.
    float logratio;
    float resonance;
    float portamento_alpha;
    uint16_t chained_osc;
    uint16_t mod_source;
    uint8_t algorithm;
    uint8_t filter_type;
    // algo_source remains int16 because users can add -1 to indicate no osc 
    int16_t algo_source[MAX_ALGO_OPS];
    uint8_t terminate_on_silence;  // Do we enable the auto-termination of silent oscs?  Usually yes, not for PCM.

    uint32_t render_clock;
    uint32_t note_on_clock;
    uint32_t note_off_clock;
    uint32_t zero_amp_clock;   // Time amplitude hits zero.
    uint32_t mod_value_clock;  // Only calculate mod_value once per frame (for mod_source).
    uint32_t *breakpoint_times[MAX_BREAKPOINT_SETS];  // (in samples) was [MAX_BREAKPOINTS] now dynamically sized.
    float *breakpoint_values[MAX_BREAKPOINT_SETS];  // was [MAX_BREAKPOINTS] now dynamically sized.
    uint8_t eg_type[MAX_BREAKPOINT_SETS];  // one of the ENVELOPE_ values
    SAMPLE last_scale[MAX_BREAKPOINT_SETS];  // remembers current envelope level, to use as start point in release.
    uint8_t max_num_breakpoints[MAX_BREAKPOINT_SETS];  // actual length of breakpoint_times/breakpoint values
  
    // State variable for the dc-removal filter.
    SAMPLE hpf_state[2];
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
    // Was the most recent note on/off received e.g. from MIDI?
    uint8_t note_source;
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


typedef struct sequence_entry_ll_t {
    struct delta d;
    uint32_t tag;
    uint32_t tick; // 0 means not used 
    uint32_t period; // 0 means not used 
    struct sequence_entry_ll_t *next;
} sequence_entry_ll_t;


typedef struct delay_line {
    SAMPLE *samples;
    int len;
    int log_2_len;
    int fixed_delay;
    int next_in;
} delay_line_t;


#include "delay.h"
#include "sequencer.h"
#include "amy_midi.h"
#include "transfer.h"

///////////////////////////////////////
// config

#define AMY_AUDIO_IS_NONE 0x00
#define AMY_AUDIO_IS_I2S 0x01
#define AMY_AUDIO_IS_USB_GADGET 0x02
#define AMY_AUDIO_IS_MINIAUDIO 0x04

#define AMY_MIDI_IS_NONE 0x0
#define AMY_MIDI_IS_UART 0x01
#define AMY_MIDI_IS_USB_GADGET 0x02
#define AMY_MIDI_IS_MACOS 0x04
#define AMY_MIDI_IS_WEBMIDI 0x08


typedef struct  {
    struct {
        uint8_t chorus : 1;
        uint8_t reverb : 1;
        uint8_t echo : 1;
        uint8_t audio_in : 1;
        uint8_t default_synths : 1;
        uint8_t partials : 1;
        uint8_t custom : 1;
        uint8_t startup_bleep : 1;
    } features;
    struct {
        uint8_t multicore : 1;  // Use secondary cores if available.
        uint8_t multithread : 1;  // Use multitasking (e.g. RTOS threads) if available.
    } platform;
    // midi and audio are potentially bitmasks indicating data routing.
    uint8_t midi;
    uint8_t audio;

    // variables
    uint16_t max_oscs;
    uint8_t ks_oscs;
    uint32_t max_sequencer_tags;
    uint32_t max_voices;
    uint32_t max_synths;
    uint32_t max_memory_patches;

    // alternative audio output function
    size_t (*write_samples_fn)(const uint8_t *buffer, size_t buffer_size);

    // pins for MCU platforms
    int8_t i2s_lrc;
    int8_t i2s_dout;
    int8_t i2s_din;
    int8_t i2s_bclk;
    int8_t i2s_mclk;
    uint16_t i2s_mclk_mult;
    int8_t midi_out;
    int8_t midi_in;
    int8_t midi_uart;

    // memory caps for MCUs
    uint32_t ram_caps_events;
    uint32_t ram_caps_sysex;
    uint32_t ram_caps_synth;
    uint32_t ram_caps_block;
    uint32_t ram_caps_fbl;
    uint32_t ram_caps_delay;
    uint32_t ram_caps_sample;

    // device ids for miniaudio platforms
    int8_t capture_device_id;
    int8_t playback_device_id;

} amy_config_t;

typedef struct reverb_state {
    SAMPLE level;
    float liveness;
    float damping;
    float xover_hz;
} reverb_state_t;

typedef struct chorus_config {
    SAMPLE level;     // How much of the delayed signal to mix in to the output, typ F2S(0.5).
    int max_delay;    // Max delay when modulating.  Must be <= DELAY_LINE_LEN
    float lfo_freq;
    float depth;
} chorus_config_t;

typedef struct echo_config {
    SAMPLE level;  // Mix of echo into output.  0 = Echo off.
    uint32_t delay_samples;  // Current delay, quantized to samples.
    uint32_t max_delay_samples;  // Maximum delay, i.e. size of allocated delay line.
    SAMPLE feedback;  // Gain applied when feeding back output to input.
    SAMPLE filter_coef;  // Echo is filtered by a two-point normalize IIR.  This is the real pole location.
} echo_config_t;


// global synth state
struct state {
    amy_config_t config;
    uint8_t running;
    uint8_t i2s_is_in_background;  // Flag not to handle I2S in amy_update.
    float volume;
    float pitch_bend;
    // State of fixed dc-blocking HPF
    SAMPLE hpf_state;
    SAMPLE eq[3];
    uint16_t delta_qsize;
    struct delta * delta_queue; // start of the sorted queue of deltas to execute.
    int16_t latency_ms;
    float tempo;
    uint32_t total_blocks;
    uint8_t debug_flag;
    
    reverb_state_t reverb;
    chorus_config_t chorus;
    echo_config_t echo;

    // Transfer
    uint8_t transfer_flag;
    uint8_t * transfer_storage;
    uint32_t transfer_length_bytes;
    uint32_t transfer_stored_bytes; // using this for midi note before file load
    uint32_t transfer_file_handle; // using this for preset number before file load 
    char transfer_filename[MAX_FILENAME_LEN];

    // Sequencer
    uint32_t sequencer_tick_count;
    uint64_t next_amy_tick_us;
    uint32_t us_per_tick;
    sequence_entry_ll_t * sequence_entry_ll_start;

};


// custom oscillator
struct custom_oscillator {
    void (*init)(void);
    void (*deinit)(void);
    void (*note_on)(uint16_t osc, float freq);
    void (*note_off)(uint16_t osc);
    void (*mod_trigger)(uint16_t osc);
    SAMPLE (*render)(SAMPLE* buf, uint16_t osc);
    SAMPLE (*compute_mod)(uint16_t osc);
};



// Shared structures
extern struct synthinfo** synth;
extern struct mod_synthinfo** msynth;
extern struct state amy_global; 

extern output_sample_type * amy_out_block;
extern output_sample_type * amy_in_block;
extern output_sample_type * amy_external_in_block;

int8_t global_init(amy_config_t c);
void amy_deltas_reset();
void add_delta_to_queue(struct delta *d, struct delta **queue);
void amy_add_event_internal(amy_event *e, uint16_t base_osc);
void amy_event_to_deltas_queue(amy_event *e, uint16_t base_osc, struct delta **queue);
int web_audio_buffer(float *samples, int length);
void amy_render(uint16_t start, uint16_t end, uint8_t core);
void print_osc_debug(int i /* osc */, bool show_eg);
void show_debug(uint8_t type) ;
void oscs_deinit() ;
float freq_for_midi_note(float midi_note);
float logfreq_for_midi_note(float midi_note);
float midi_note_for_logfreq(float logfreq);
float logfreq_of_freq(float freq);
float freq_of_logfreq(float logfreq);
int8_t check_init(amy_err_t (*fn)(), char *name);
void * malloc_caps(uint32_t size, uint32_t flags);
void config_reverb(float level, float liveness, float damping, float xover_hz);
void config_chorus(float level, uint16_t max_delay, float lfo_freq, float depth);
void config_echo(float level, float delay_ms, float max_delay_ms, float feedback, float filter_coef);
void osc_note_on(uint16_t osc, float initial_freq);
void chorus_note_on(float initial_freq);

SAMPLE log2_lut(SAMPLE x);
SAMPLE exp2_lut(SAMPLE x);


float atoff(const char *s);
int8_t oscs_init();
void alloc_osc(int osc, uint8_t *max_num_breakpoints_per_bpset_or_null);
void free_osc(int osc);
void ensure_osc_allocd(int osc, uint8_t *max_num_breakpoints_per_bpset_or_null);
void patches_init(int max_memory_patches);
void patches_deinit();
int parse_breakpoint(struct synthinfo * e, char* message, uint8_t bp_set) ;
void parse_algo_source(char* message, int16_t *vals);
void hold_and_modify(uint16_t osc) ;
void amy_execute_delta();
void amy_execute_deltas();
int16_t * amy_fill_buffer();
int16_t * amy_simple_fill_buffer();  // excute_deltas + render + fill_buffer
uint32_t ms_to_samples(uint32_t ms) ;



// API
void amy_add_message(char *message);
void amy_add_event(amy_event *e);
void amy_parse_message(char * message, int length, amy_event *e);
void amy_start(amy_config_t);
void amy_stop();

int16_t *amy_update();        // in api.c
void amy_platform_init();     // in i2s.c
void amy_platform_deinit();   // in i2s.c
void amy_update_tasks();      // in i2s.c
int16_t *amy_render_audio();  // in i2s.c
size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes);  // in i2s.c

amy_config_t amy_default_config();
void amy_clear_event(amy_event *e);
amy_event amy_default_event();
uint32_t amy_sysclock();
int amy_get_output_buffer(output_sample_type * samples);
int amy_get_input_buffer(output_sample_type * samples);
void amy_set_external_input_buffer(output_sample_type * samples);
extern uint8_t (*amy_external_render_hook)(uint16_t, SAMPLE*, uint16_t);
extern float (*amy_external_coef_hook)(uint16_t);
extern void (*amy_external_block_done_hook)(void);
extern void (*amy_external_midi_input_hook)(uint8_t *, uint16_t, uint8_t);
extern void (*amy_external_sequencer_hook)(uint32_t);
// Hooks for file reading / writing / opening if your AMY host supports that
extern uint32_t (*amy_external_fopen_hook)(char * filename, char * mode) ;
extern uint32_t (*amy_external_fwrite_hook)(uint32_t fptr, uint8_t * bytes, uint32_t len);
extern uint32_t (*amy_external_fread_hook)(uint32_t fptr, uint8_t *bytes, uint32_t len);
extern void (*amy_external_fseek_hook)(uint32_t fptr, uint32_t pos);
extern void (*amy_external_fclose_hook)(uint32_t fptr);
extern void (*amy_external_file_transfer_done_hook)(const char *filename);


#ifdef __EMSCRIPTEN__
void amy_start_web();
void amy_start_web_no_synths();
#endif


// external functions
void amy_process_event(amy_event *e);
void amy_restart();
void amy_reset_oscs();
void amy_print_devices();
void amy_set_custom(struct custom_oscillator* custom);
void amy_reset_sysclock();

extern int parse_int_list_message32(char *message, int32_t *vals, int max_num_vals, int32_t skipped_val);
extern int parse_int_list_message16(char *message, int16_t *vals, int max_num_vals, int16_t skipped_val);
extern void reset_osc(uint16_t i );

extern int midi_store_control_code(int channel, int code, int is_log, float min_val, float max_val, float offset_val, char *message);
extern void cc_mapping_debug();

extern float render_am_lut(float * buf, float step, float skip, float incoming_amp, float ending_amp, const float* lut, int16_t lut_size, float *mod, float bandwidth);
extern void ks_init();
extern void ks_deinit();
extern void algo_init();
extern void algo_deinit();
extern void pcm_init();
extern void pcm_deinit();
extern void custom_init();
extern void custom_deinit();

void my_srand48(uint32_t seedval);

void audio_in_note_on(uint16_t osc, uint8_t channel);
void external_audio_in_note_on(uint16_t osc, uint8_t channel);
SAMPLE render_audio_in(SAMPLE * buf, uint16_t osc, uint8_t channel);
SAMPLE render_external_audio_in(SAMPLE *buf, uint16_t osc, uint8_t channel);

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
extern void interp_partials_note_on(uint16_t osc);
extern void interp_partials_note_off(uint16_t osc);
extern int interp_partials_max_partials_for_patch(int interp_partials_patch_number);
extern void patches_load_patch(amy_event *e); 
extern void patches_event_has_voices(amy_event *e, struct delta **queue);
extern void patches_reset_patch(int patch_number);
extern void patches_reset();
extern void parse_patch_number_to_events(uint16_t patch_number, struct amy_event **events, uint16_t *event_count);

extern struct delta **queue_for_patch_number(int patch_number);
extern void update_num_oscs_for_patch_number(int patch_number);
extern void all_notes_off();
extern void patches_debug();
extern void patches_store_patch(amy_event *e, char * message);
#define _SYNTH_FLAGS_MIDI_DRUMS (0x01)
#define _SYNTH_FLAGS_IGNORE_NOTE_OFFS (0x02)
#define _SYNTH_FLAGS_NEGATE_PEDAL (0x04)
extern void instruments_init(int num_instruments);
extern void instruments_deinit();
extern void instruments_reset();
extern void instrument_add_new(int instrument_number, int num_voices, uint16_t *amy_voices, uint16_t patch_number, uint32_t flags);
extern void instrument_release(int instrument_number);
extern void instrument_change_number(int old_instrument_number, int new_instrument_number);
#define _INSTRUMENT_NO_VOICE (255)
extern uint16_t instrument_voice_for_note_event(int instrument_number, int note, bool is_note_off, bool *pstolen);
extern int instrument_get_num_voices(int instrument_number, uint16_t *amy_voices);
extern int instrument_all_notes_off(int instrument_number, uint16_t *amy_voices);
extern int instrument_sustain(int instrument_number, bool sustain, uint16_t *amy_voices);
extern int instrument_get_patch_number(int instrument_number);
extern uint32_t instrument_get_flags(int instrument_number);
extern uint16_t instrument_noteon_delay_ms(int instrument_number);
extern void instrument_set_noteon_delay_ms(int instrument_number, uint16_t noteon_delay_ms);
extern bool instrument_grab_midi_notes(int instrument_number);
extern void instrument_set_grab_midi_notes(int instrument_number, bool grab_midi_notes);
extern int instrument_bank_number(int instrument_number);
extern void instrument_set_bank_number(int instrument_number, int bank_number);

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
extern void algo_note_on(uint16_t osc, float freq);
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
extern int16_t * pcm_load(uint16_t preset_number, uint32_t length, uint32_t samplerate, uint8_t channels, uint8_t midinote, uint32_t loopstart, uint32_t loopend);
extern int pcm_load_file();
extern void pcm_unload_preset(uint16_t preset_number);
extern void pcm_unload_all_presets();

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

// On Arduino, something doesn't allow ESP_TASK_PRIO_MAX in tasks
#ifdef ARDUINO
#define AMY_RENDER_TASK_PRIORITY (20) 
#define AMY_FILL_BUFFER_TASK_PRIORITY (20)
#else
#define AMY_RENDER_TASK_PRIORITY (ESP_TASK_PRIO_MAX)
#define AMY_FILL_BUFFER_TASK_PRIORITY (ESP_TASK_PRIO_MAX)
#endif
#define AMY_RENDER_TASK_COREID (0)
#define AMY_FILL_BUFFER_TASK_COREID (1)
#define AMY_RENDER_TASK_STACK_SIZE (12 * 1024) // 8
#define AMY_FILL_BUFFER_TASK_STACK_SIZE (16 * 1024) // 16
#define AMY_RENDER_TASK_NAME      "amy_r_task"
#define AMY_FILL_BUFFER_TASK_NAME "amy_fb_task"
#include "esp_err.h"
#endif

// envelopes
extern SAMPLE compute_breakpoint_scale(uint16_t osc, uint8_t bp_set, uint16_t sample_offset);
extern SAMPLE compute_mod_scale(uint16_t osc);
extern SAMPLE compute_mod_value(uint16_t mod_osc);
extern void retrigger_mod_source(uint16_t osc);

// deltas
extern void deltas_pool_init();
extern void deltas_pool_free();
extern struct delta *delta_get(struct delta *d);  // clone existing delta values if d not NULL.
extern struct delta *delta_release(struct delta *d);  // returns d->next of the node just released.
extern void delta_release_list(struct delta *d);  // releases a whole list of deltas.
extern int32_t delta_list_len(struct delta *d);
extern int32_t delta_num_free();  // The size of the remaining pool.

extern int peek_stack(char *tag);

#endif
