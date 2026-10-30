// pcm.c

#include "amy.h"
#include "transfer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#endif

#ifdef AMY_DAISY
#define malloc_caps(a, b) qspi_malloc(a)
#define free(a) qspi_free(a)
#endif


// This is for any in-memory PCM samples.
typedef struct {
    uint8_t type; 
    char filename[MAX_FILENAME_LEN];
    uint8_t channels;
    uint32_t file_handle;
    uint32_t file_bytes_remaining;
    int16_t * sample_ram;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    float midinote;   // fractional, so a sample's tuning correction can live in the preset
    uint32_t samplerate;
    float log2sr;
#ifdef AMY_WAVETABLE
    int16_t *wt_rungs;                            // the band-limited copies of a wavetable, rung after rung; NULL until built
    LUT wt_set[AMY_WAVETABLE_LADDER_RUNGS + 2];   // the table and its rungs as a lutset, zeroed until built
#endif
} memorypcm_preset_t;

// linked list of memorypcm presets
typedef struct memorypcm_ll_t{
    memorypcm_preset_t *preset;
    struct memorypcm_ll_t *next;
    uint16_t preset_number;
} memorypcm_ll_t;


memorypcm_ll_t * memorypcm_ll_start;

#define PCM_AMY_LOG2_SAMPLE_RATE log2f(PCM_AMY_SAMPLE_RATE / ZERO_LOGFREQ_IN_HZ)

#ifdef GAMMA9001
#include "pcm_gamma9001.h"
// Set by the platform at boot: web links the drums.bin blob in and passes it,
// ESP32-S3 passes the esp_partition_mmap'd partition. NULL = banks unavailable.
const int16_t * gamma9001_pcm = NULL;
void amy_set_gamma9001_pcm(const int16_t * data) {
    gamma9001_pcm = data;
}
#endif


// Get either memory preset, file preset or baked in preset for preset number.
// For ROM presets, fill the caller-provided rom_local and return it.
memorypcm_preset_t * get_preset_for_preset_number(uint16_t preset_number,
                                                  memorypcm_preset_t *rom_local) {
    // Get the memory preset. If we can't find it, it could be a ROM preset. So copy params in from ROM preset
    memorypcm_ll_t *preset = memorypcm_ll_start;
    while(preset != NULL) {
        if(preset->preset_number == preset_number) {
            if(preset->preset->sample_ram != NULL || preset->preset->file_handle > 0) {
                return preset->preset;
            }
        }
        preset = preset->next;
    }

#ifdef GAMMA9001
    // Gamma9001 drum banks live at GAMMA9001_PRESET_BASE+, read straight out
    // of the platform-provided blob (memory presets above may still shadow them).
    if (preset_number >= GAMMA9001_PRESET_BASE &&
        preset_number < GAMMA9001_PRESET_BASE + GAMMA9001_NUM_SAMPLES &&
        gamma9001_pcm != NULL && rom_local != NULL) {
        const pcm_map_t *g = &gamma9001_map[preset_number - GAMMA9001_PRESET_BASE];
        memset(rom_local, 0, sizeof(*rom_local));
        rom_local->sample_ram = (int16_t *)gamma9001_pcm + g->offset;
        rom_local->length = g->length;
        rom_local->loopstart = g->loopstart;
        rom_local->loopend = g->loopend;
        rom_local->midinote = g->midinote;
        rom_local->samplerate = GAMMA9001_SAMPLE_RATE;
        rom_local->log2sr = log2f((float)GAMMA9001_SAMPLE_RATE / ZERO_LOGFREQ_IN_HZ);
        rom_local->type = AMY_PCM_TYPE_GAMMA;
        rom_local->channels = 1;
        return rom_local;
    }
#endif

    // No memory preset found, so try ROM preset. default to 0 if out of range
    if (preset_number >= pcm_samples) preset_number = 0; 
    if (rom_local == NULL) {
        return NULL;
    }
    memset(rom_local, 0, sizeof(*rom_local));
    const pcm_map_t cpreset =  pcm_map[preset_number];
    uint32_t offset = cpreset.offset;
    uint32_t length = cpreset.length;
#ifdef PCM_LENGTH
    if (offset >= PCM_LENGTH) {
        offset = 0;
        length = 0;
    } else if (length > (PCM_LENGTH - offset)) {
        length = PCM_LENGTH - offset;
    }
#endif
    rom_local->sample_ram = (int16_t*)pcm + offset;
    rom_local->length = length;
    rom_local->loopstart = cpreset.loopstart;
    rom_local->loopend = cpreset.loopend;
    if (rom_local->loopstart > rom_local->length) {
        rom_local->loopstart = 0;
    }
    if (rom_local->loopend > rom_local->length) {
        rom_local->loopend = rom_local->length;
    }
    rom_local->midinote = cpreset.midinote;
    rom_local->samplerate = PCM_AMY_SAMPLE_RATE;
    rom_local->log2sr = PCM_AMY_LOG2_SAMPLE_RATE;
    rom_local->type = AMY_PCM_TYPE_ROM;
    rom_local->channels = 1;
    return rom_local;
}

const int16_t *pcm_get_sample_ram_for_preset(uint16_t preset_number, uint32_t *length) {
    memorypcm_preset_t rom_local;
    memorypcm_preset_t *preset = get_preset_for_preset_number(preset_number, &rom_local);
    if (length != NULL) {
        *length = (preset != NULL) ? preset->length : 0;
    }
    if (preset == NULL) {
        return NULL;
    }
    return preset->sample_ram;
}

// Read back an in-memory PCM preset -- one filled by load_sample or recorded by
// start_sample -- so an embedder can save it.  Unlike the lookup above there is
// no ROM fallback: returns NULL unless preset_number is a memory preset (ROM,
// gamma and file-streamed presets all return NULL).  The returned RAM holds
// frames * channels interleaved samples and is only valid until the preset is
// unloaded or reloaded.  Any out-pointer may be NULL.
const int16_t *pcm_get_memory_preset(uint16_t preset_number, uint32_t *frames, uint8_t *channels,
                                     uint32_t *samplerate, float *midinote) {
    for (memorypcm_ll_t *p = memorypcm_ll_start; p != NULL; p = p->next) {
        if (p->preset_number != preset_number) continue;
        memorypcm_preset_t *preset = p->preset;
        if (preset->type != AMY_PCM_TYPE_MEMORY || preset->sample_ram == NULL) return NULL;
        if (frames != NULL) *frames = preset->length;
        if (channels != NULL) *channels = preset->channels;
        if (samplerate != NULL) *samplerate = preset->samplerate;
        if (midinote != NULL) *midinote = preset->midinote;
        return preset->sample_ram;
    }
    return NULL;
}


// Hann window for the granular time-stretcher, Q15.  At 50% overlap Hann
// windows sum to exactly 1.0 (COLA), so two overlapping grains reconstruct
// unity gain.  Filled once at pcm_init (float trig at init time only; the
// render path is all fixed point).
static int16_t stretch_win[PCM_STRETCH_GRAIN];

// Wavetable pitch ladders (contract in amy.h). A rung is a table of the
// preset's length, each frame the frame's Fourier series cut at the rung's
// limit with the mean kept (a fixed-point real FFT per frame with Q26
// twiddles, an inverse per rung with the bins above the limit zeroed); the
// rungs sit one after another in one allocation. The lutset holds the base
// table at 127, then the rungs at their limits, then a zeroed terminator.
// Build and free change the set under the render lock, so a block reads the
// whole set or none; a rebuild writes the rungs in place.
#ifdef AMY_WAVETABLE
#define WAVETABLE_LADDER_MAX_LIMIT 63   // caps every entry of wavetable_ladder_limits
_Static_assert(2 * WAVETABLE_LADDER_MAX_LIMIT < WAVETABLE_SAMPLES_PER_CYCLE / 2,
               "WAVETABLE_LADDER_MAX_LIMIT: a kept bin k and its partner 128 - k would overlap");
static const uint8_t wavetable_ladder_limits[AMY_WAVETABLE_LADDER_RUNGS] = { 63, 31, 15, 7 };
// cos(2 pi n / 256) at Q26, the FFT's fixed-point scale, filled by pcm_init:
// a quarter wave from cosf, so float precision (about 2^-24 relative),
// mirrored with 0 and +-1 exact. The accuracy is the measured rung error
// against the exact series, not the Q26 scale.
static int32_t wavetable_ladder_cos_q26[WAVETABLE_SAMPLES_PER_CYCLE];
// The ROM wavetables have no preset object; their rungs and sets live here
// (pcm_wavetable_samples of them, the sample header's count, capped).
#define WAVETABLE_LADDER_ROM_MAX 8
static int16_t *rom_wt_rungs[WAVETABLE_LADDER_ROM_MAX];
static LUT rom_wt_set[WAVETABLE_LADDER_ROM_MAX][AMY_WAVETABLE_LADDER_RUNGS + 2];

// The rungs pointer and set of a preset: a memory preset's own, a ROM
// wavetable's static ones, else NULL (file and gamma presets, non-wavetable ROM).
static int wavetable_ladder_slots(uint16_t preset_number, int16_t ***rungs, LUT **set) {
    memorypcm_ll_t *p = memorypcm_ll_start;
    while (p != NULL) {
        if (p->preset_number == preset_number) {
            if (p->preset->type != AMY_PCM_TYPE_MEMORY) return 0;
            *rungs = &p->preset->wt_rungs; *set = p->preset->wt_set;
            return 1;
        }
        p = p->next;
    }
    if (preset_number >= pcm_wavetable_base && preset_number < pcm_wavetable_base + pcm_wavetable_samples
        && preset_number - pcm_wavetable_base < WAVETABLE_LADDER_ROM_MAX) {
        *rungs = &rom_wt_rungs[preset_number - pcm_wavetable_base];
        *set = rom_wt_set[preset_number - pcm_wavetable_base];
        return 1;
    }
    return 0;
}

// Take the set back and free the rungs, if there are any.
static void wavetable_ladder_retract(int16_t **rungs, LUT *set) {
    if (*rungs == NULL) return;
    amy_grab_render_lock();
    memset(set, 0, sizeof(LUT) * (AMY_WAVETABLE_LADDER_RUNGS + 2));
    free(*rungs);
    *rungs = NULL;
    amy_release_render_lock();
}

static void wavetable_ladder_drop(memorypcm_preset_t *preset) {
    if (preset->type == AMY_PCM_TYPE_MEMORY) wavetable_ladder_retract(&preset->wt_rungs, preset->wt_set);
}
#else
#define wavetable_ladder_drop(preset) ((void)0)
#endif

void pcm_init() {
    memorypcm_ll_start = NULL;
    for (int i = 0; i < PCM_STRETCH_GRAIN; ++i) {
        float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)PCM_STRETCH_GRAIN));
        stretch_win[i] = (int16_t)(w * 32767.0f);
    }
#ifdef AMY_WAVETABLE
    const int N = WAVETABLE_SAMPLES_PER_CYCLE;
    int32_t *C = wavetable_ladder_cos_q26;
    for (int n = 1; n < N / 4; ++n)
        C[n] = (int32_t)lroundf(cosf(2.0f * (float)M_PI * (float)n / (float)N) * (float)(1 << 26));
    C[0] = 1 << 26;
    C[N / 4] = 0;
    for (int n = 0; n <= N / 4; ++n) {   // cos(N/2 -+ n) = -cos(n), cos(N - n) = cos(n)
        C[N / 2 - n] = -C[n];
        C[N / 2 + n] = -C[n];
        if (n > 0) C[N - n] = C[n];
    }
#endif
}
void pcm_deinit() {
    pcm_unload_all_presets();
#ifdef AMY_WAVETABLE
    for (int i = 0; i < WAVETABLE_LADDER_ROM_MAX; ++i)
        wavetable_ladder_retract(&rom_wt_rungs[i], rom_wt_set[i]);
#endif
}

// How many bits used for fractional part of PCM table index.
#define PCM_INDEX_FRAC_BITS 8
// The number of bits used to hold the table index.
#define PCM_INDEX_BITS (31 - PCM_INDEX_FRAC_BITS)
// The phase advance step within a block is calculated with this many additional bits beyond PCM_INDEX_FRAC_BITS
#define PCM_INDEX_STEP_EXTRA_BITS 8

static void fclose_if_file(memorypcm_preset_t *preset) {
    if (preset == NULL) {
        return;
    }
    if (preset->type == AMY_PCM_TYPE_FILE &&
        preset->file_handle != 0 &&
        amy_global.config.amy_external_fclose_hook != NULL) {
        amy_global.config.amy_external_fclose_hook(preset->file_handle);
        preset->file_handle = 0;
    }
}

static bool mode_is_looping(uint16_t mode) {
    return mode == PCM_LOOP || mode == PCM_LOOP_STOP || mode == PCM_LOOP_FOREVER;
}

// True if `preset_number` streams from a file rather than sitting in memory.
// `filename_out`, when non-NULL, receives the file name for the message.
static bool preset_is_file(uint16_t preset_number, const char **filename_out) {
    if (AMY_IS_UNSET(preset_number)) return false;
    memorypcm_preset_t rom_local;
    memorypcm_preset_t *preset = get_preset_for_preset_number(preset_number, &rom_local);
    if (preset == NULL || preset->type != AMY_PCM_TYPE_FILE) return false;
    if (filename_out != NULL) *filename_out = preset->filename;
    return true;
}

// A file-backed preset streams through a small sliding buffer rather than
// sitting in a table we can index freely, so there is nothing to loop back
// into: render_pcm refills from the file each block and rewinds phase to the
// top of the fresh buffer. A PCM_LOOP* mode on such a preset can never do
// what it says.
//
// Rather than accept the command and quietly do something else at note-on,
// refuse it where it is issued -- when the mode changes, and when the preset
// number changes -- so the configuration never reaches a state it can't
// honor, and the user hears about it while the offending command is still in
// front of them. Called with the *proposed* mode and preset; returns false if
// the command should be dropped.
//
// (Whole-file looping *would* be implementable on top of the fseek+re-parse
// rewind pcm_note_on already does; what a stream can never honor is the
// loopstart/loopend marks. That's a bigger change than this one.)
bool pcm_loop_config_allowed(uint16_t osc, uint16_t mode, uint16_t preset_number,
                             bool mode_is_the_new_part) {
    // mode means nothing outside PCM, so don't second-guess other waves.
    if (synth[osc]->wave != PCM) return true;
    if (!mode_is_looping(mode)) return true;
    const char *filename = NULL;
    if (!preset_is_file(preset_number, &filename)) return true;
    if (mode_is_the_new_part) {
        fprintf(stderr, "amy: osc %d preset %d streams from %s, which cannot loop; "
                        "ignoring mode=%d. Use load_sample() to loop.\n",
                osc, preset_number, filename ? filename : "a file", mode);
    } else {
        fprintf(stderr, "amy: preset %d streams from %s, which cannot loop, but osc %d "
                        "is in mode=%d; ignoring preset=%d. Set a non-loop mode first, "
                        "or use load_sample().\n",
                preset_number, filename ? filename : "a file", osc, mode, preset_number);
    }
    return false;
}

// Source frames over which the note a retrigger replaces fades out under the
// new one: gain 1 at the note-on, 0 at the end.  Fixed, so the new note starts
// on its note-on whatever the old waveform's phase.
#define PCM_RETRIG_FADE_SHIFT 6
#define PCM_RETRIG_FADE_FRAMES (1 << PCM_RETRIG_FADE_SHIFT)

///////////////////////////////////////////////////////////////////////////
// "fit": real-time pitch-invariant time stretch / time-invariant pitch shift.
//
// Synchronous granular overlap-add: two Hann-windowed grains, 50% overlap.
// Every PCM_STRETCH_HOP output samples a new grain spawns at the input
// timeline position, which advances at (input frames / output samples) set
// by the fit target -- independent of the per-sample read step *within* a
// grain, which carries the pitch (note tuning + preset samplerate
// conversion).  Decoupling those two rates is the whole trick:
//   fit=N ticks : timeline rate = remaining_frames / target_samples,
//                 grain step = pitch ratio  -> duration locked, pitch free
//   fit=0       : timeline rate = preset_sr / AMY_SAMPLE_RATE (original
//                 duration), grain step = pitch ratio -> pitch shift at
//                 constant length
// This was chosen over an FFT phase vocoder deliberately: no FFT, no big
// tables, a handful of multiplies per sample, all s.15/Q16 fixed point --
// it runs on every AMY platform including ESP32-S3/RP2350 class parts.
//
// The first note-on hop starts BOTH grains at the same input position, one
// at the top of the window and one at the halfway (peak) point.  Their
// windows sum to 1 over the first hop, so the first ~12 ms reproduce the
// input exactly: drum transients are not softened by a window fade-in.

// Read one frame for the stretcher, honoring PCM_LEFT/PCM_RIGHT/mix.
static inline LUTSAMPLE pcm_stretch_read(const LUTSAMPLE *table, uint8_t channels, uint16_t wave, uint32_t idx) {
    if (channels == 2) {
        if (wave == PCM_LEFT) return table[idx * 2];
        if (wave == PCM_RIGHT) return table[idx * 2 + 1];
        return (LUTSAMPLE)(((int32_t)table[idx * 2] + (int32_t)table[idx * 2 + 1]) / 2);
    }
    return table[idx];
}

// Is the osc's granular stretcher running?  Only a PCM-kind osc has one.
static inline bool pcm_stretch_active(uint16_t osc) {
    return synth[osc]->wave_kind == WAVE_KIND_PCM && synth[osc]->stretch != NULL
        && synth[osc]->stretch->active;
}

// Configure the stretcher at note-on.  preset must be in-memory (not FILE).
// The stretcher is allocated at the osc's first fit note-on and kept (see
// osc_release_allocs()).  Returns false on OOM: the note plays unstretched.
static bool pcm_stretch_note_on(uint16_t osc, memorypcm_preset_t *preset) {
    pcm_stretch_t *st = synth[osc]->stretch;
    if (st == NULL) {
        st = (pcm_stretch_t *)malloc_caps(sizeof(pcm_stretch_t), amy_global.config.ram_caps_oscs);
        if (st == NULL) {
            amy_oom("pcm_stretch_note_on: out of memory for osc %d\n", osc);
            return false;
        }
        memset(st, 0, sizeof(*st));
        synth[osc]->stretch = st;
    }
    uint32_t start_frame = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
    if (start_frame >= preset->length) start_frame = 0;
    uint32_t remaining = preset->length - start_frame;
    // Target output duration in samples at AMY_SAMPLE_RATE.
    float target;
    if (synth[osc]->fit_ticks > 0) {
        target = synth[osc]->fit_ticks * (float)amy_global.us_per_tick * (float)AMY_SAMPLE_RATE / 1000000.0f;
    } else {
        // fit=0: keep the sample's own duration; note pitch changes only.
        target = (float)remaining * (float)AMY_SAMPLE_RATE / (float)preset->samplerate;
    }
    if (target < 1.0f) target = 1.0f;
    st->hop_advance_q16 = (uint32_t)(((float)remaining * (float)PCM_STRETCH_HOP / target) * 65536.0f);
    st->in_pos_q16 = ((uint64_t)start_frame << 16) + st->hop_advance_q16;
    for (int g = 0; g < PCM_STRETCH_GRAINS; ++g) {
        st->grain[g].active = 1;
        st->grain[g].start_frame = start_frame;
        st->grain[g].phase_q16 = 0;
        // Grain 0 starts at the window peak (as if born a hop ago), grain 1
        // at the top; see the note above about preserving the transient.
        st->grain[g].win_pos = (g == 0) ? PCM_STRETCH_HOP : 0;
    }
    st->hop_counter = PCM_STRETCH_HOP;
    // fit=N ticks is tempo-locked, so remember what a tick was worth here;
    // render_pcm_stretch rescales the rate if that changes mid-note.  fit=0
    // asks for the sample's own duration, which no tempo can move, so it is
    // flagged (0) as not tempo-locked.
    st->us_per_tick_ref = (synth[osc]->fit_ticks > 0) ? amy_global.us_per_tick : 0;
    st->ended = 0;
    st->active = 1;
    return true;
}

// WSOLA-style spawn alignment: search this many frames either side of the
// nominal grain start for the best phase match with the still-playing grain.
// Without it, a pure tone through the stretcher lands on the nearest line of
// a comb spaced at the hop rate (~86 Hz at 44.1 kHz) instead of the exact
// target pitch; broadband drums barely care, tonal material does.
//
// The half-width is per-osc at runtime (fit_search / 'pS'); PCM_STRETCH_SEARCH
// is the default when it's unset and PCM_STRETCH_SEARCH_MAX the ceiling any
// request is clamped to.  Setting PCM_STRETCH_SEARCH to 0 compiles the search
// out entirely for very small parts, and then 'pS' does nothing.
//
// How wide it needs to be is set by the lowest pitch present: the search has
// to be able to reach half a period, so the default 64 covers periods down to
// about AMY_SAMPLE_RATE/128 -- fine from ~345 Hz up (at 44.1 kHz), useless on
// a bass line, where the aligner lands on a comb line instead of the note.  A
// 110 Hz tone needs 200 frames of reach, and sounds like 141 Hz without them
// (experiments/sampler/fit_search_test.py measures it).
#define PCM_STRETCH_SEARCH 64
#define PCM_STRETCH_SEARCH_MAX 512
// Multiplies per candidate offset, so the whole search costs
// (2*search+1)*TAPS per hop no matter how wide it is: the comparison window
// grows with the search (it also has to span about a period, or the
// correlation can't tell one lag from another) but the stride opens up to
// match, keeping the tap count fixed.  Decimating like that aliases, which
// would matter if the wide settings were for bright material -- they're for
// bass, where the taps still land many per period.
#define PCM_STRETCH_SEARCH_TAPS 64

// Resolve the per-osc request ('pS'): unset means the default, anything over
// the ceiling is clamped rather than refused so a part can ask for "as wide
// as you can" without knowing the build's budget.
static inline uint16_t pcm_stretch_search_width(uint16_t osc) {
    uint16_t s = synth[osc]->fit_search;
    if (AMY_IS_UNSET(s)) return PCM_STRETCH_SEARCH;
    if (s > PCM_STRETCH_SEARCH_MAX) return PCM_STRETCH_SEARCH_MAX;
    return s;
}

static int32_t pcm_stretch_best_offset(const LUTSAMPLE *table, uint8_t channels, uint16_t wave,
                                       uint32_t length, uint32_t target, uint32_t nominal,
                                       uint16_t search) {
#if PCM_STRETCH_SEARCH > 0
    if (search == 0) return 0;
    uint32_t stride = (search > PCM_STRETCH_SEARCH_TAPS) ? (search / PCM_STRETCH_SEARCH_TAPS) : 1;
    uint32_t win = stride * PCM_STRETCH_SEARCH_TAPS;   // == 64 at the default
    // Bail if either segment would run off the sample.
    if (target + win >= length) return 0;
    int32_t lo = -(int32_t)search, hi = (int32_t)search;
    if ((int32_t)nominal + lo < 0) lo = -(int32_t)nominal;
    if (nominal + hi + win >= length) {
        if (nominal + win >= length) return 0;
        hi = (int32_t)(length - win - 1 - nominal);
    }
    if (lo > hi) return 0;
    int32_t best_d = 0, best_corr = INT32_MIN;
    for (int32_t d = lo; d <= hi; ++d) {
        int32_t corr = 0;
        for (uint32_t i = 0; i < win; i += stride) {
            int32_t a = pcm_stretch_read(table, channels, wave, target + i) >> 4;
            int32_t b = pcm_stretch_read(table, channels, wave, nominal + d + i) >> 4;
            corr += a * b;
        }
        if (corr > best_corr) { best_corr = corr; best_d = d; }
    }
    return best_d;
#else
    (void)table; (void)channels; (void)wave; (void)length; (void)target; (void)nominal;
    (void)search;
    return 0;
#endif
}

// Spawn a replacement grain at the current input timeline position.
static void pcm_stretch_spawn(pcm_stretch_t *st, memorypcm_preset_t *preset, uint16_t wave,
                              bool looping, uint32_t loopstart, uint32_t loopend, uint16_t search) {
    uint32_t length = preset->length;
    st->hop_counter = PCM_STRETCH_HOP;
    if (st->ended) return;
    uint32_t frame = (uint32_t)(st->in_pos_q16 >> 16);
    if (looping) {
        // Wrap the timeline into the loop region.
        uint32_t le = (loopend > loopstart && loopend <= length) ? loopend : length;
        uint32_t ls = (loopstart < le) ? loopstart : 0;
        if (frame >= le) {
            frame = ls + ((frame - le) % (le - ls));
            st->in_pos_q16 = ((uint64_t)frame << 16) | (st->in_pos_q16 & 0xffff);
        }
    } else if (frame >= length) {
        // Input exhausted; let the live grains fade out.
        st->ended = 1;
        return;
    }
    // Reuse the expired grain (it hits win_pos == PCM_STRETCH_GRAIN exactly
    // when the spawn is due); fall back to the oldest.
    int gi = 0;
    for (int g = 0; g < PCM_STRETCH_GRAINS; ++g) {
        if (!st->grain[g].active) { gi = g; break; }
        if (st->grain[g].win_pos > st->grain[gi].win_pos) gi = g;
    }
    // Phase-align the new grain against the one it will overlap: search near
    // the nominal start for the segment that best matches what that grain is
    // about to play.  The timeline itself (in_pos) is never adjusted, so the
    // alignment jitter can't accumulate into a tempo error.
    int32_t d = 0;
    int other = 1 - gi;  // PCM_STRETCH_GRAINS == 2
    if (st->grain[other].active) {
        uint32_t target = st->grain[other].start_frame + (st->grain[other].phase_q16 >> 16);
        d = pcm_stretch_best_offset(preset->sample_ram, preset->channels, wave, length, target, frame,
                                    search);
    }
    st->grain[gi].active = 1;
    st->grain[gi].start_frame = (uint32_t)((int32_t)frame + d);
    st->grain[gi].phase_q16 = st->in_pos_q16 & 0xffff;
    st->grain[gi].win_pos = 0;
    st->in_pos_q16 += st->hop_advance_q16;
}

static SAMPLE render_pcm_stretch(SAMPLE *buf, uint16_t osc, memorypcm_preset_t *preset) {
    pcm_stretch_t *st = synth[osc]->stretch;  // pcm_stretch_active() checked it
    // A tempo change has to reach notes that are ALREADY sounding: fit= locks
    // a note to a number of ticks, and a tick just got longer or shorter, so
    // a note left alone would run on at the old tempo and land off the grid.
    //
    // The timeline rate is (input frames / target output samples) and the
    // target is proportional to us_per_tick, so following the change is a
    // rescale of the existing rate, not a recomputation from what is left.
    // That distinction matters: "input frames remaining" is a finite quantity
    // only for a one-shot, while a looping preset wraps forever -- the
    // rescale is correct for both, and stays correct across repeated changes
    // because the reference is updated each time.
    if (st->us_per_tick_ref != 0 && st->us_per_tick_ref != amy_global.us_per_tick
        && amy_global.us_per_tick > 0) {
        st->hop_advance_q16 = (uint32_t)(((uint64_t)st->hop_advance_q16
                                          * st->us_per_tick_ref) / amy_global.us_per_tick);
        st->us_per_tick_ref = amy_global.us_per_tick;
    }
    SAMPLE max_value = 0;
    float logfreq = msynth[osc]->logfreq;
    if (AMY_IS_SET(synth[osc]->midi_note)) {
        logfreq -= logfreq_for_midi_note(preset->midinote);
    }
    float playback_freq = freq_of_logfreq(preset->log2sr + logfreq);
    // Same exact-native-rate correction as render_pcm.
    if (logfreq == 0 && AMY_IS_UNSET(synth[osc]->midi_note))
        playback_freq = (float)preset->samplerate;
    // Per-sample read step within a grain: the pitch, recomputed per block so
    // envelopes/LFOs on freq keep working.
    uint32_t pitch_step_q16 = (uint32_t)((playback_freq / (float)AMY_SAMPLE_RATE) * 65536.0f);
    // Ramp the gain across the block, as render_pcm does.
    SAMPLE amp = F2S(msynth[osc]->last_amp);
    SAMPLE amp_step = SHIFTR(F2S(msynth[osc]->amp) - amp, BLOCK_SIZE_BITS);
    msynth[osc]->last_amp = msynth[osc]->amp;
    const LUTSAMPLE *table = preset->sample_ram;
    uint32_t length = preset->length;
    // Re-read each block, so 'pS' can be swept while the note is sounding.
    uint16_t search = pcm_stretch_search_width(osc);
    bool looping = mode_is_looping(msynth[osc]->state);
    uint32_t loopstart = msynth[osc]->loopstart;
    uint32_t loopend = (msynth[osc]->loopend > loopstart && msynth[osc]->loopend <= length) ? msynth[osc]->loopend : length;
    uint16_t i = 0;
    if (msynth[osc]->pcm_delay) {
        // sample_offset: leave the head of the note-on block silent.
        i = msynth[osc]->pcm_delay;
        msynth[osc]->pcm_delay = 0;
    }
    amp += amp_step * i;
    for (; i < AMY_BLOCK_SIZE; i++) {
        if (st->hop_counter == 0)
            pcm_stretch_spawn(st, preset, synth[osc]->wave, looping, loopstart, loopend, search);
        st->hop_counter--;
        SAMPLE out = 0;
        uint8_t any_active = 0;
        for (int g = 0; g < PCM_STRETCH_GRAINS; ++g) {
            if (!st->grain[g].active) continue;
            any_active = 1;
            uint32_t idx = st->grain[g].start_frame + (st->grain[g].phase_q16 >> 16);
            if (looping && idx >= loopend)
                idx = loopstart + ((idx - loopend) % (loopend - loopstart));
            if (idx < length) {
                LUTSAMPLE b = pcm_stretch_read(table, preset->channels, synth[osc]->wave, idx);
                LUTSAMPLE c = (idx + 1 < length) ? pcm_stretch_read(table, preset->channels, synth[osc]->wave, idx + 1) : b;
                SAMPLE frac = (SAMPLE)(st->grain[g].phase_q16 & 0xffff) << (S_FRAC_BITS - 16);
                SAMPLE samp = L2S(b) + MUL4_SS(L2S(c - b), frac);
                out += MUL0_SS(samp, L2S(stretch_win[st->grain[g].win_pos]));
            }
            st->grain[g].phase_q16 += pitch_step_q16;
            if (++st->grain[g].win_pos >= PCM_STRETCH_GRAIN) st->grain[g].active = 0;
        }
        if (!any_active) {
            if (st->ended) {
                synth[osc]->status = SYNTH_OFF;
                st->active = 0;
            }
            break;
        }
        SAMPLE value = buf[i] + MUL4_SS(amp, out);
        amp += amp_step;
        buf[i] = value;
        if (value < 0) value = -value;
        if (value > max_value) max_value = value;
    }
    return max_value;
}

void pcm_note_on(uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        memorypcm_preset_t rom_local;
        memorypcm_preset_t *preset =
            get_preset_for_preset_number(synth[osc]->preset, &rom_local);
        if (preset->type == AMY_PCM_TYPE_FILE) {
            if (preset->file_handle != 0) {
                wave_info_t info = {0};
                uint32_t data_bytes = 0;
                amy_global.config.amy_external_fseek_hook(preset->file_handle, 0);
                if (wave_parse_header(preset->file_handle, &info, &data_bytes)) {
                    preset->channels = info.channels;
                    preset->samplerate = info.sample_rate;
                    preset->log2sr = log2f((float)info.sample_rate / ZERO_LOGFREQ_IN_HZ);
                    preset->file_bytes_remaining = data_bytes;
                } else {
                    amy_global.config.amy_external_fclose_hook(preset->file_handle);
                }
            }
        } else if (preset->type == AMY_PCM_TYPE_ROM) {
            // baked-in PCM - don't overrun.
            if(synth[osc]->preset >= pcm_samples) synth[osc]->preset = 0;
        }
        PHASOR phase;
        if (AMY_IS_SET(synth[osc]->trigger_phase)) {
            // trigger_phase (P) sets the sample start point for this
            // note-on (start_frame / 2^PCM_INDEX_BITS).
            phase = F2P(synth[osc]->trigger_phase);
        } else {
            phase = 0; // s16.15 index into the table; as if a PHASOR into a 16 bit sample table.
        }
        // Does this note-on want the granular fit engine?  Only for in-memory
        // presets: it needs random access, which a streamed file can't give.
        // Normally a no-op: WAVE set the kind.  But config for another kind
        // can have claimed the union since, and fit is read below.
        osc_set_wave_kind(osc, WAVE_KIND_PCM);
        bool want_stretch = AMY_IS_SET(synth[osc]->fit_ticks)
            && preset->type != AMY_PCM_TYPE_FILE
            && preset->sample_ram != NULL && preset->length > 0;
        uint32_t base_index = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
        // A note-on repeated before its note has rendered (the position is still the start) has
        // nothing to fade; it keeps whatever fade is running.  Any other note-on ends one.
        bool unrendered = synth[osc]->status == SYNTH_AUDIBLE && synth[osc]->phase == phase;
        if (!unrendered) msynth[osc]->fade_end = 0;
        if (synth[osc]->status == SYNTH_AUDIBLE && !unrendered && preset->type != AMY_PCM_TYPE_FILE
            && !want_stretch && !pcm_stretch_active(osc) && base_index < preset->length
            && msynth[osc]->amp > AMP_THRESH_PLUS) {
            // Restarting a currently-playing (non-file) PCM: the old note keeps playing from where it
            // is, fading out over PCM_RETRIG_FADE_FRAMES under the new one (render_pcm), to avoid a click.
            // (Not for the fit engine: its grains are windowed, so a restart is click-free by construction.
            // Not past the end of the preset either, e.g. after a switch to a shorter one, and not when the
            // envelope has already faded the old note to silence: nothing to fade in either case.)
            msynth[osc]->fade_phase = synth[osc]->phase;
            PHASOR fade_end = synth[osc]->phase + I2P(PCM_RETRIG_FADE_FRAMES, PCM_INDEX_BITS);
            if (fade_end > I2P(preset->length, PCM_INDEX_BITS)) fade_end = I2P(preset->length, PCM_INDEX_BITS);
            msynth[osc]->fade_end = fade_end;
            // The old note fades at the gain it had (held for the fade, not advanced).
            msynth[osc]->fade_amp = msynth[osc]->last_amp;
        }
        // The new note starts from its own envelope (hold_and_modify starts a zero last_amp there), not
        // ramping from the gain of whatever played before: a retriggered note, or one a note-off stopped
        // (PCM_PLAY_STOP turns the osc off without resetting it).
        if (!unrendered) msynth[osc]->last_amp = 0;
        synth[osc]->phase = phase;
        msynth[osc]->loopstart = preset->loopstart;
        msynth[osc]->loopend = preset->loopend;
        // Copy the looping mode from the wave mode field.  Can be updated on note_off.
        msynth[osc]->state = synth[osc]->mode;
        // sample_offset (po): begin this note-on partway into its render
        // block, so slices of arbitrary length can butt-join sample-
        // accurately.
        msynth[osc]->pcm_delay = 0;
        if (AMY_IS_SET(synth[osc]->sample_offset))
            msynth[osc]->pcm_delay = synth[osc]->sample_offset % AMY_BLOCK_SIZE;
        if (!(want_stretch && pcm_stretch_note_on(osc, preset)) && synth[osc]->stretch != NULL)
            synth[osc]->stretch->active = 0;
        // Make sure PCM waveforms are excluded from auto-termination, so we don't cut-off samples with silent gaps.  May be modified by note_off.
        synth[osc]->terminate_on_silence = 0;
    }
}

void pcm_mod_trigger(uint16_t osc) {
    pcm_note_on(osc);
}


void pcm_note_off(uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        if (msynth[osc]->state == PCM_PLAY_STOP
            || msynth[osc]->state == PCM_LOOP_STOP) {
            // PCM mode where note off causes immediate stop.
            //
            // This used to seek phase past the end of the sample and let
            // render_pcm notice on the next block. That worked only for
            // in-memory presets: a streamed one refills from the file and
            // resets phase to 0 every block, so the seek was thrown away and
            // the clip played on to end-of-file, ignoring note-off entirely.
            // PCM_PLAY_STOP is the DEFAULT mode, so that hit every
            // disk_sample() note-off. Stopping the osc says what we mean and
            // works for both kinds -- and it no longer needs the preset
            // lookup that the seek needed just to find the sample length.
            synth[osc]->status = SYNTH_OFF;
        } else if (msynth[osc]->state == PCM_LOOP_FOREVER) {
            // Sending one note-off to a LOOP_FOREVER loop downgrades it to a stoppable loop.
            msynth[osc]->state = PCM_LOOP;
            // Allow the engine to terminate it when it goes to silence (e.g. from envelope).
            synth[osc]->terminate_on_silence = 1;
        } else if (msynth[osc]->state == PCM_LOOP || msynth[osc]->state == PCM_PLAY) {
            // Looping was enabled but after stop we just play through to the end.
            // (sending a second note-off will stop it immediately).
            msynth[osc]->state = PCM_PLAY_STOP;
        }
    }
}


uint32_t fill_sample_from_file(memorypcm_preset_t *preset_p, uint32_t frames_needed) {
    //fprintf(stderr, "fsff %ld frames\n", frames_needed);
    uint32_t bytes_per_frame = preset_p->channels * 2;
    uint32_t frames_available = 0;
    if (bytes_per_frame > 0) {
        frames_available = preset_p->file_bytes_remaining / bytes_per_frame;
    }
    if (frames_available > 0 && frames_needed > frames_available) {
        frames_needed = frames_available;
    }
    uint32_t frames_read = wave_read_pcm_frames_s16(
        preset_p->file_handle,
        preset_p->channels,
        &preset_p->file_bytes_remaining,
        preset_p->sample_ram,
        frames_needed);
    return frames_read;
}

SAMPLE render_pcm(SAMPLE* buf, uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        SAMPLE max_value = 0;
        memorypcm_preset_t rom_local;
        memorypcm_preset_t *preset =
            get_preset_for_preset_number(synth[osc]->preset, &rom_local);
        // fit= notes render through the granular stretch engine instead.
        if (pcm_stretch_active(osc) && preset->type != AMY_PCM_TYPE_FILE
            && preset->sample_ram != NULL && preset->length > 0) {
            return render_pcm_stretch(buf, osc, preset);
        }
        float logfreq = msynth[osc]->logfreq;
        // If osc[midi_note] is set, shift the freq by the preset's default base_note.
        if (AMY_IS_SET(synth[osc]->midi_note)) {
            logfreq -= logfreq_for_midi_note(preset->midinote);
        }
        float playback_freq = freq_of_logfreq(preset->log2sr + logfreq);
        // Unpitched playback (no note, no freq mods): use the preset's rate
        // exactly.  The log2f/exp2f roundtrip above lands a hair off the
        // native rate, which makes untransposed samples drift by ~1 sample
        // every few seconds -- enough to spoil sample-accurate butt-joins of
        // slices against their source.
        if (logfreq == 0 && AMY_IS_UNSET(synth[osc]->midi_note))
            playback_freq = (float)preset->samplerate;
        uint32_t sample_length = preset->length;
        if (preset->type == AMY_PCM_TYPE_FILE) {
            float frames_per_output = playback_freq / (float)AMY_SAMPLE_RATE;
            uint32_t frames_needed = (uint32_t)ceilf(frames_per_output * AMY_BLOCK_SIZE) + 1;
            uint32_t max_frames = AMY_BLOCK_SIZE * PCM_FILE_BUFFER_MULT;
            if (frames_needed > max_frames) {
                frames_needed = max_frames;
            }
            sample_length = fill_sample_from_file(preset, frames_needed);
            if(sample_length != frames_needed) {
                // reached end of file
                synth[osc]->status = SYNTH_OFF;
            }
            synth[osc]->phase = 0;
        }
        if (preset->sample_ram == NULL || sample_length == 0) {
            synth[osc]->status = SYNTH_OFF;
            return 0;
        }

        // msynth amp is the envelope at the end of this block, last_amp where the previous block ended;
        // ramp between them across the block, as render_lut does, so an envelope change is a slope
        // rather than a once-per-block step in the gain.
        SAMPLE amp = F2S(msynth[osc]->last_amp);
        SAMPLE amp_step = SHIFTR(F2S(msynth[osc]->amp) - amp, BLOCK_SIZE_BITS);
        PHASOR step = F2P((playback_freq / (float)AMY_SAMPLE_RATE) / (float)(1 << (PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS)));
        const LUTSAMPLE* table = preset->sample_ram;
        uint32_t base_index_base = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
        uint32_t base_index = base_index_base;
        PHASOR phase = (synth[osc]->phase - (base_index_base << PCM_INDEX_FRAC_BITS)) << PCM_INDEX_STEP_EXTRA_BITS;
        if (msynth[osc]->fade_end) {
            // The note this one retriggered, fading out under it (see pcm_note_on): the same table
            // and step, from its own position, gain = (distance left to fade_end) / PCM_RETRIG_FADE_FRAMES,
            // 1 where it was armed and 0 at the end, at the gain it had (fade_amp).  Bounded by the
            // table as it is now, in case the preset changed under a running fade.
            if (msynth[osc]->fade_end > I2P(sample_length, PCM_INDEX_BITS))
                msynth[osc]->fade_end = I2P(sample_length, PCM_INDEX_BITS);
            uint32_t fbase = INT_OF_P(msynth[osc]->fade_phase, PCM_INDEX_BITS);
            PHASOR fphase = (msynth[osc]->fade_phase - (fbase << PCM_INDEX_FRAC_BITS)) << PCM_INDEX_STEP_EXTRA_BITS;
            uint32_t findex = fbase;
            PHASOR fpos = msynth[osc]->fade_phase;
            SAMPLE famp = F2S(msynth[osc]->fade_amp);
            for (uint16_t i = 0; i < AMY_BLOCK_SIZE && fpos < msynth[osc]->fade_end; i++) {
                SAMPLE frac = S_FRAC_OF_P(fphase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS);
                LUTSAMPLE b = pcm_stretch_read(table, preset->channels, synth[osc]->wave, findex);
                LUTSAMPLE c = (findex + 1 < sample_length) ? pcm_stretch_read(table, preset->channels, synth[osc]->wave, findex + 1) : b;
                SAMPLE sample = L2S(b) + MUL4_SS(L2S(c - b), frac);
                SAMPLE gain = SHIFTL((SAMPLE)(msynth[osc]->fade_end - fpos), S_FRAC_BITS - PCM_INDEX_FRAC_BITS - PCM_RETRIG_FADE_SHIFT);
                SAMPLE value = buf[i] + MUL4_SS(famp, MUL0_SS(sample, gain));
                buf[i] = value;
                if (value < 0) value = -value;
                if (value > max_value) max_value = value;
                fphase = P_WRAPPED_SUM(fphase, step);
                findex = fbase + INT_OF_P(fphase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS);
                fpos = I2P(findex, PCM_INDEX_BITS) + (S_FRAC_OF_P(fphase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS) >> (S_FRAC_BITS - (PCM_INDEX_FRAC_BITS)));
            }
            if (fpos >= msynth[osc]->fade_end) msynth[osc]->fade_end = 0;
            else msynth[osc]->fade_phase = fpos;
        }
        // sample_offset (po): a fresh note-on starts partway into this block,
        // leaving the head silent, so slices can butt-join sample-accurately.
        uint16_t start_i = 0;
        if (msynth[osc]->pcm_delay) {
            start_i = msynth[osc]->pcm_delay;
            msynth[osc]->pcm_delay = 0;
        }
        amp += amp_step * start_i;
        for(uint16_t i=start_i; i < AMY_BLOCK_SIZE; i++) {
            SAMPLE frac = S_FRAC_OF_P(phase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS);
            LUTSAMPLE b = 0;
            LUTSAMPLE c = 0;
            uint32_t next_index = base_index + 1;
            // For non-file samples, we have to check for end of sample/looping.
            if(preset->type != AMY_PCM_TYPE_FILE) {
                if ((msynth[osc]->state == PCM_LOOP
                     || msynth[osc]->state == PCM_LOOP_STOP
                     || msynth[osc]->state == PCM_LOOP_FOREVER)
                    && base_index >= msynth[osc]->loopend) { // loopend
                    // still looping.  The state may be modified by pcm_note_off.
                    // back to loopstart
                    phase &= ((1L << (PCM_INDEX_FRAC_BITS + PCM_INDEX_STEP_EXTRA_BITS)) - 1);
                    base_index_base = msynth[osc]->loopstart + (base_index - msynth[osc]->loopend);
                    //fprintf(stderr, "time %.3f sample %d LOOP: old_index %d new_index %d phase 0x%lx\n", amy_global.time, i, base_index, base_index_base, phase);
                    base_index = base_index_base;
                } else if(base_index >= sample_length) { // end
                    synth[osc]->status = SYNTH_OFF;// is this right?
                    buf[i] = 0;
                    break;
                }
            }
            if (preset->channels == 2) {
                uint32_t base_offset = base_index * 2;
                uint32_t next_offset = next_index * 2;
                if (synth[osc]->wave == PCM_LEFT) {
                    b = table[base_offset];
                    c = (next_index < sample_length) ? table[next_offset] : b;
                } else if (synth[osc]->wave == PCM_RIGHT) {
                    b = table[base_offset + 1];
                    c = (next_index < sample_length) ? table[next_offset + 1] : b;
                } else { // PCM or PCM_MIX
                    LUTSAMPLE bl = table[base_offset];
                    LUTSAMPLE br = table[base_offset + 1];
                    b = (LUTSAMPLE)(((int32_t)bl + (int32_t)br) / 2);
                    if (next_index < sample_length) {
                        LUTSAMPLE cl = table[next_offset];
                        LUTSAMPLE cr = table[next_offset + 1];
                        c = (LUTSAMPLE)(((int32_t)cl + (int32_t)cr) / 2);
                    } else {
                        c = b;
                    }
                }
            } else {
                b = table[base_index];
                c = (next_index < sample_length) ? table[next_index] : b;
            }
            SAMPLE sample = L2S(b) + MUL4_SS(L2S(c - b), frac);
            SAMPLE value = buf[i] + MUL4_SS(amp, sample);
            amp += amp_step;
            buf[i] = value;   
            if (value < 0) value = -value;
            if (value > max_value) max_value = value;  
            phase = P_WRAPPED_SUM(phase, step);
            base_index = base_index_base + INT_OF_P(phase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS);
        }
        msynth[osc]->last_amp = msynth[osc]->amp;
        //synth[osc]->phase = phase;
        synth[osc]->phase = I2P(base_index, PCM_INDEX_BITS) + (S_FRAC_OF_P(phase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS) >> (S_FRAC_BITS - (PCM_INDEX_FRAC_BITS)) ); //  + PCM_INDEX_STEP_EXTRA_BITS
        //fprintf(stderr, "\rtime %.3f osc %d render_pcm7: preset %d len %d base_ix 0x%lx phase 0x%lx sfracofp 0x%lx step 0x%lx synthphase 0x%lx amp %.3f\n",
        //        amy_global.time, osc, synth[osc]->preset, preset->length, base_index, phase, S_FRAC_OF_P(phase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS) >> (S_FRAC_BITS - (PCM_INDEX_FRAC_BITS + PCM_INDEX_STEP_EXTRA_BITS)), step, synth[osc]->phase, S2F(msynth[osc]->amp));
        return max_value; 
        // i don't believe we ever need to detect silence in a sample. it will shut itself off at the end.
    }
    return 0;
}


SAMPLE compute_mod_pcm(uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        SAMPLE buf[AMY_BLOCK_SIZE];
        memset(buf, 0, sizeof(buf));
        render_pcm(buf, osc);
        return buf[0];
    }
    return 0;
}


int pcm_load_file() {
    // We pass the inputs to this as aliases in the amy_global structure. This is to not destroy the MP heap for amy->AMYboard
    uint8_t midinote = amy_global.transfer_stored_bytes;
    uint16_t preset_number = amy_global.transfer_file_handle;
    char * filename = amy_global.transfer_filename;

    pcm_unload_preset(preset_number);
    if (filename == NULL || filename[0] == '\0') {
        return 0;
    }
    if (amy_global.config.amy_external_fopen_hook == NULL || amy_global.config.amy_external_fclose_hook == NULL) {
        fprintf(stderr, "fopen hook not enabled on platform\n");
        return 0;
    }
    uint32_t handle = amy_global.config.amy_external_fopen_hook((char *)filename, "rb");
    if (handle == 0) {
        fprintf(stderr, "Could not open file %s\n", filename);
        return 0;
    }
    wave_info_t info = {0};
    uint32_t data_bytes = 0;
    if (!wave_parse_header(handle, &info, &data_bytes)) {
        fprintf(stderr, "Could not parse WAVE file %s\n", filename);
        amy_global.config.amy_external_fclose_hook(handle);
        return 0;
    }
    uint32_t total_frames = 0;
    if (info.channels > 0) {
        total_frames = data_bytes / (info.channels * 2);
    }
    uint32_t buffer_frames = AMY_BLOCK_SIZE * PCM_FILE_BUFFER_MULT;
    memorypcm_ll_t *new_preset_pointer = malloc_caps(
        sizeof(memorypcm_ll_t) + sizeof(memorypcm_preset_t) + buffer_frames * sizeof(int16_t),
        amy_global.config.ram_caps_sample);
    if (new_preset_pointer == NULL) {
        fprintf(stderr, "No RAM left for sample load\n");
        return 0;
    }
    new_preset_pointer->next = memorypcm_ll_start;
    memorypcm_ll_start = new_preset_pointer;
    new_preset_pointer->preset_number = preset_number;
    memorypcm_preset_t *memory_preset =
        (memorypcm_preset_t *)(((uint8_t *)new_preset_pointer) + sizeof(memorypcm_ll_t));
    strncpy(memory_preset->filename, filename, MAX_FILENAME_LEN - 1);
    memory_preset->filename[MAX_FILENAME_LEN - 1] = '\0';
    memory_preset->channels = info.channels;
    memory_preset->samplerate = info.sample_rate;
    memory_preset->log2sr = log2f((float)info.sample_rate / ZERO_LOGFREQ_IN_HZ);
    memory_preset->midinote = midinote;
    memory_preset->length = total_frames;
    memory_preset->type = AMY_PCM_TYPE_FILE;
    memory_preset->file_bytes_remaining = total_frames * info.channels * 2;
    memory_preset->file_handle = handle;
    memory_preset->sample_ram = malloc_caps(buffer_frames * info.channels * sizeof(int16_t),
                                                     amy_global.config.ram_caps_sample);
    new_preset_pointer->preset = memory_preset;
    //fprintf(stderr, "read file %s frames %ld channels %d preset %d handle %ld\n", filename, total_frames, info.channels, preset_number, handle);
    return 1;
}


// load mono samples (let python parse wave files) into preset # 
// set loopstart, loopend, midinote, samplerate (and log2sr)
// return the allocated sample ram that AMY will fill in.
int16_t * pcm_load(uint16_t preset_number, uint32_t length, uint32_t samplerate, uint8_t channels, float midinote, uint32_t loopstart, uint32_t loopend) {
    // if preset was already a memorypcm, we need to unload it
    pcm_unload_preset(preset_number); // this is a no-op if preset doesn't exist or is a const pcm
    // now alloc a new LL entry and preset (the old LL entry is removed with pcm_unload_preset)
    memorypcm_ll_t *new_preset_pointer = malloc_caps(sizeof(memorypcm_ll_t) + sizeof(memorypcm_preset_t) + length * channels * sizeof(int16_t),
						     amy_global.config.ram_caps_sample);
    if(new_preset_pointer  == NULL) {
        fprintf(stderr, "No RAM left for sample load\n");
        return NULL; // no ram for sample
    }
    new_preset_pointer->next = memorypcm_ll_start;
    memorypcm_ll_start = new_preset_pointer;
    new_preset_pointer->preset_number = preset_number;
    memorypcm_preset_t *memory_preset = (memorypcm_preset_t *)(((uint8_t *)new_preset_pointer) + sizeof(memorypcm_ll_t));
    memory_preset->samplerate = samplerate;
    memory_preset->log2sr = log2f((float)samplerate / ZERO_LOGFREQ_IN_HZ);
    memory_preset->midinote = midinote;
    memory_preset->loopstart = loopstart;
    memory_preset->length = length;
    memory_preset->channels = channels;
    memory_preset->filename[0] = '\0';
    memory_preset->file_bytes_remaining = 0;
    memory_preset->file_handle = 0;
    memory_preset->type = AMY_PCM_TYPE_MEMORY;
    memory_preset->sample_ram = (int16_t *)(((uint8_t *)memory_preset) + sizeof(memorypcm_preset_t));
#ifdef AMY_WAVETABLE
    memory_preset->wt_rungs = NULL;
    memset(memory_preset->wt_set, 0, sizeof(memory_preset->wt_set));
#endif
    if(loopend == 0) {  // loop whole sample
        memory_preset->loopend = memory_preset->length-1;
    } else {
        memory_preset->loopend = loopend;
    }
    new_preset_pointer->preset = memory_preset;
    return memory_preset->sample_ram;
}

void pcm_unload_preset(uint16_t preset_number) {
    // run through the LL looking for the preset
    memorypcm_ll_t **preset_pointer = &memorypcm_ll_start;
    while(*preset_pointer != NULL) {
        if((*preset_pointer)->preset_number == preset_number) {
            memorypcm_ll_t *next = (*preset_pointer)->next;
            fclose_if_file((*preset_pointer)->preset);
            wavetable_ladder_drop((*preset_pointer)->preset);
            // free the memory we allocated
            free((*preset_pointer));
            // close up the list
            *preset_pointer = next;
            return;
        } else {
            preset_pointer = &(*preset_pointer)->next;
        }
    }
    //fprintf(stderr, "pcm_unload_preset: preset %d not found\n", preset_number);  // This happens during a routine load_preset.
}

void pcm_unload_all_presets() {
    memorypcm_ll_t *preset_pointer = memorypcm_ll_start;
    while(preset_pointer != NULL) {
        memorypcm_ll_t *next_pointer = preset_pointer->next;
        fclose_if_file(preset_pointer->preset);
        wavetable_ladder_drop(preset_pointer->preset);
        free(preset_pointer);
        // Go to the next one
        preset_pointer = next_pointer;
    }
    memorypcm_ll_start = NULL;
}

#ifdef AMY_WAVETABLE
// 128-point complex FFT in place on int32 data: radix 2, decimation in time,
// W^t = cos - i sin of 2 pi t / 256 at Q26, each product pair summed in 64
// bits and rounded back to the data's units. The forward is unscaled; the
// inverse halves every stage, rounded, so it returns the frame and no stage
// grows past its input.
static void wavetable_ladder_fft128_q(int32_t *re, int32_t *im, int inverse) {
    const int N = WAVETABLE_SAMPLES_PER_CYCLE, M = N / 2;
    const int32_t *C = wavetable_ladder_cos_q26;
    const int sh = inverse ? 1 : 0;   // also the rounding offset
    for (int i = 1, j = 0; i < M; ++i) {
        int bit = M >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) {
            int32_t t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2, stride = N / 2; len <= M; len <<= 1, stride >>= 1) {
        int half = len >> 1;
        for (int j = 0; j < half; ++j) {
            int32_t wr = C[j * stride];
            int32_t s = C[(j * stride + 3 * N / 4) & (N - 1)];
            int32_t wi = inverse ? s : -s;
            for (int i = j; i < M; i += len) {
                int l = i + half;
                int32_t tr = (int32_t)(((int64_t)wr * re[l] - (int64_t)wi * im[l] + (1 << 25)) >> 26);
                int32_t ti = (int32_t)(((int64_t)wr * im[l] + (int64_t)wi * re[l] + (1 << 25)) >> 26);
                int32_t ar = re[i], ai = im[i];
                re[l] = (ar - tr + sh) >> sh; im[l] = (ai - ti + sh) >> sh;
                re[i] = (ar + tr + sh) >> sh; im[i] = (ai + ti + sh) >> sh;
            }
        }
    }
}

// y in 2^-7 sample units, rounded half up, clamped.
static int16_t wavetable_ladder_round_q7(int32_t y) {
    int32_t v = (y + 64) >> 7;
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

// One frame of every rung, out[r] cut at wavetable_ladder_limits[r]: a real
// FFT of the frame (the even and odd samples packed as one complex 128-point
// transform, then split), and per rung the inverse with the bins above the
// limit zeroed, in fixed point with the samples at 2^-7.
// The bounds that fit int32: forward bins |Z| <= 128 sqrt 2 2^15, 2^29.5
// here; |X[k]| <= sum |x| <= 2^23, 2^30; per rung Parseval gives |Z'|^2 <=
// 128 sum y^2 <= 128 sum x^2, 2^29.5 again, and the halving inverse stays
// under its input. The split's sums of two bins (er, ei, or_, oi) reach
// 2^30.5 and fit, and in the inverse butterfly a +/- t reaches 2^30.5 before
// the halving. At limits <= 63 a bin k and its partner M - k are never both
// kept. The split and the inverse prep take both halves in 64 bits and round
// once.
static void wavetable_ladder_frame(const int16_t *in, int16_t *const out[AMY_WAVETABLE_LADDER_RUNGS]) {
    const int N = WAVETABLE_SAMPLES_PER_CYCLE, M = N / 2, QT = 3 * N / 4;
    const int32_t *C = wavetable_ladder_cos_q26;
    int32_t re[WAVETABLE_SAMPLES_PER_CYCLE / 2], im[WAVETABLE_SAMPLES_PER_CYCLE / 2];
    int32_t xr[WAVETABLE_LADDER_MAX_LIMIT + 1], xi[WAVETABLE_LADDER_MAX_LIMIT + 1];
    for (int m = 0; m < M; ++m) { re[m] = in[2 * m] * 128; im[m] = in[2 * m + 1] * 128; }
    wavetable_ladder_fft128_q(re, im, 0);
    // X[k] = E[k] + W^k O[k], E = (Z[k] + conj Z[M-k]) / 2, O = (Z[k] - conj Z[M-k]) / 2i,
    // taken as 2X = 2E + W^k 2O.
    for (int k = 0; k <= WAVETABLE_LADDER_MAX_LIMIT; ++k) {
        int j = (M - k) & (M - 1);
        int32_t er = re[k] + re[j], ei = im[k] - im[j];
        int32_t or_ = im[k] + im[j], oi = re[j] - re[k];
        int32_t c = C[k], s = C[(k + QT) & (N - 1)];
        xr[k] = (int32_t)(((int64_t)er * (1 << 26) + (int64_t)c * or_ + (int64_t)s * oi + (1 << 26)) >> 27);
        xi[k] = (int32_t)(((int64_t)ei * (1 << 26) + (int64_t)c * oi - (int64_t)s * or_ + (1 << 26)) >> 27);
    }
    for (int r = 0; r < AMY_WAVETABLE_LADDER_RUNGS; ++r) {
        const int limit = wavetable_ladder_limits[r];
        // Z[k] = E[k] + i O[k], E = (X[k] + conj X[M-k]) / 2, O = (X[k] - conj X[M-k]) W^-k / 2,
        // X zero above the limit (and at M), taken as 2Z = (A + B) + i (D W^-k), D = A - conj B,
        // A = X[k] and B = X[M-k].
        for (int k = 0; k < M; ++k) {
            int32_t ar = 0, ai = 0, br = 0, bi = 0;
            if (k <= limit) { ar = xr[k]; ai = xi[k]; }
            if (M - k <= limit) { br = xr[M - k]; bi = xi[M - k]; }
            int32_t dr = ar - br, di = ai + bi;
            int32_t c = C[k], s = C[(k + QT) & (N - 1)];
            re[k] = (int32_t)(((int64_t)(ar + br) * (1 << 26) - (int64_t)dr * s - (int64_t)di * c + (1 << 26)) >> 27);
            im[k] = (int32_t)(((int64_t)(ai - bi) * (1 << 26) + (int64_t)dr * c - (int64_t)di * s + (1 << 26)) >> 27);
        }
        wavetable_ladder_fft128_q(re, im, 1);
        for (int m = 0; m < M; ++m) {
            out[r][2 * m] = wavetable_ladder_round_q7(re[m]);
            out[r][2 * m + 1] = wavetable_ladder_round_q7(im[m]);
        }
    }
}

// Every rung of the table, frame by frame.
static void wavetable_ladder_rungs(const int16_t *base, int16_t *table, uint32_t rung_length, uint32_t cycles) {
    for (uint32_t c = 0; c < cycles; ++c) {
        int16_t *out[AMY_WAVETABLE_LADDER_RUNGS];
        for (int r = 0; r < AMY_WAVETABLE_LADDER_RUNGS; ++r)
            out[r] = table + r * rung_length + c * WAVETABLE_SAMPLES_PER_CYCLE;
        wavetable_ladder_frame(base + c * WAVETABLE_SAMPLES_PER_CYCLE, out);
    }
}

int pcm_wavetable_ladder_build(uint16_t preset_number) {
    int16_t **rungs; LUT *set;
    if (!wavetable_ladder_slots(preset_number, &rungs, &set)) return 0;
    memorypcm_preset_t rom_local;
    memorypcm_preset_t *preset = get_preset_for_preset_number(preset_number, &rom_local);
    if (preset == NULL || preset->channels != 1 || preset->sample_ram == NULL
        || preset->length < 2 * WAVETABLE_SAMPLES_PER_CYCLE) return 0;
    uint32_t cycles = preset->length / WAVETABLE_SAMPLES_PER_CYCLE;
    uint32_t rung_length = cycles * WAVETABLE_SAMPLES_PER_CYCLE;
    int16_t *table = *rungs;   // a rebuild writes the published rungs in place
    if (table == NULL) {
        table = (int16_t *)malloc_caps(AMY_WAVETABLE_LADDER_RUNGS * rung_length * sizeof(int16_t),
                                       amy_global.config.ram_caps_sample);
        if (table == NULL) return 0;
    }
    wavetable_ladder_rungs(preset->sample_ram, table, rung_length, cycles);
    if (*rungs == NULL) {
        LUT entry = { preset->sample_ram, WAVETABLE_SAMPLES_PER_CYCLE, WAVETABLE_LOG2_SAMPLES_PER_CYCLE, 127, 1.0f };
        amy_grab_render_lock();
        set[0] = entry;
        for (int r = 0; r < AMY_WAVETABLE_LADDER_RUNGS; ++r) {
            entry.table = table + r * rung_length;
            entry.highest_harmonic = wavetable_ladder_limits[r];
            set[1 + r] = entry;
        }
        *rungs = table;
        amy_release_render_lock();
    }
    return 1;
}

void pcm_wavetable_ladder_free(uint16_t preset_number) {
    int16_t **rungs; LUT *set;
    if (wavetable_ladder_slots(preset_number, &rungs, &set)) wavetable_ladder_retract(rungs, set);
}

const LUT *pcm_wavetable_lutset(uint16_t preset_number) {
    int16_t **rungs; LUT *set;
    if (!wavetable_ladder_slots(preset_number, &rungs, &set)) return NULL;
    return (set[0].table_size > 0) ? set : NULL;
}
#endif
