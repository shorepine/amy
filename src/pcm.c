// pcm.c

#include "amy.h"
#include "transfer.h"

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


// Hann window for the granular time-stretcher, Q15.  At 50% overlap Hann
// windows sum to exactly 1.0 (COLA), so two overlapping grains reconstruct
// unity gain.  Filled once at pcm_init (float trig at init time only; the
// render path is all fixed point).
static int16_t stretch_win[PCM_STRETCH_GRAIN];

void pcm_init() {
    memorypcm_ll_start = NULL;
    for (int i = 0; i < PCM_STRETCH_GRAIN; ++i) {
        float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)PCM_STRETCH_GRAIN));
        stretch_win[i] = (int16_t)(w * 32767.0f);
    }
}
void pcm_deinit() {
    pcm_unload_all_presets();
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

// Sample value close enough to zero to stop sample
#define PCM_ZERO_THRESH 16
// How far forward to search for a zero crossing
#define PCM_MAX_ZERO_SEARCH_LEN 512

int pcm_find_next_zero_crossing(uint16_t osc, uint32_t base_index) {
    // Find next zero or zero crossing beyond base_index in PCM under osc.
    int index = -1;
    if(AMY_IS_SET(synth[osc]->preset)) {
        memorypcm_preset_t rom_local;
        memorypcm_preset_t *preset =
            get_preset_for_preset_number(synth[osc]->preset, &rom_local);
        uint32_t sample_length = preset->length;
        const LUTSAMPLE* table = preset->sample_ram;
        int last_sign = 0;
        int sign;
        if (preset->type != AMY_PCM_TYPE_FILE
            && table != NULL
            && sample_length != 0) {
            const LUTSAMPLE* table = preset->sample_ram;
            //uint32_t start_index = base_index;  // for debug only
            LUTSAMPLE min_val = SAMPLE_MAX;
            LUTSAMPLE val;
            for(uint16_t i=0; i < PCM_MAX_ZERO_SEARCH_LEN; i++) {
                // For non-file samples, we have to check for end of sample/looping.
                if (base_index >= sample_length) break;
                if (preset->channels == 2) {
                    if (synth[osc]->wave == PCM_LEFT) {
                        val = table[base_index * 2];
                    } else if (synth[osc]->wave == PCM_RIGHT) {
                        val = table[base_index * 2 + 1];
                    } else { // PCM or PCM_MIX
                        val = (LUTSAMPLE)(((int32_t)table[base_index * 2] + (int32_t)table[base_index * 2 + 1]) / 2);
                    }
                } else {
                    val = table[base_index];
                }
                sign = 1;
                if (val < 0) {sign = -1; val = -val;}
                if (val < min_val) {
                    min_val = val;
                    index = base_index;
                }
                if ((val <= PCM_ZERO_THRESH) || ((sign * last_sign) == -1))
                    break;
                last_sign = sign;
                ++base_index;
            }
            //fprintf(stderr, "time %.3f: pcm_find_zero: osc %d start %d base %d len %d min_val %d index %d sign %d last %d\n",
            //        amy_global.time, osc, start_index, base_index, sample_length, min_val, index, sign, last_sign);
        }
    }
    return index;
}

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

// Configure the stretcher at note-on.  preset must be in-memory (not FILE).
static void pcm_stretch_note_on(uint16_t osc, memorypcm_preset_t *preset) {
    pcm_stretch_t *st = &synth[osc]->stretch;
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
}

// WSOLA-style spawn alignment: search this many frames either side of the
// nominal grain start for the best phase match with the still-playing grain.
// Without it, a pure tone through the stretcher lands on the nearest line of
// a comb spaced at the hop rate (~86 Hz) instead of the exact target pitch;
// broadband drums barely care, tonal material does.  Costs
// (2*SEARCH+1)*SEARCH_WIN int multiplies per hop; set SEARCH to 0 to disable
// on very small parts.
#define PCM_STRETCH_SEARCH 64
#define PCM_STRETCH_SEARCH_WIN 64

static int32_t pcm_stretch_best_offset(const LUTSAMPLE *table, uint8_t channels, uint16_t wave,
                                       uint32_t length, uint32_t target, uint32_t nominal) {
#if PCM_STRETCH_SEARCH > 0
    // Bail if either segment would run off the sample.
    if (target + PCM_STRETCH_SEARCH_WIN >= length) return 0;
    int32_t lo = -(int32_t)PCM_STRETCH_SEARCH, hi = PCM_STRETCH_SEARCH;
    if ((int32_t)nominal + lo < 0) lo = -(int32_t)nominal;
    if (nominal + hi + PCM_STRETCH_SEARCH_WIN >= length) {
        if (nominal + PCM_STRETCH_SEARCH_WIN >= length) return 0;
        hi = (int32_t)(length - PCM_STRETCH_SEARCH_WIN - 1 - nominal);
    }
    if (lo > hi) return 0;
    int32_t best_d = 0, best_corr = INT32_MIN;
    for (int32_t d = lo; d <= hi; ++d) {
        int32_t corr = 0;
        for (int32_t i = 0; i < PCM_STRETCH_SEARCH_WIN; ++i) {
            int32_t a = pcm_stretch_read(table, channels, wave, target + i) >> 4;
            int32_t b = pcm_stretch_read(table, channels, wave, nominal + d + i) >> 4;
            corr += a * b;
        }
        if (corr > best_corr) { best_corr = corr; best_d = d; }
    }
    return best_d;
#else
    (void)table; (void)channels; (void)wave; (void)length; (void)target; (void)nominal;
    return 0;
#endif
}

// Spawn a replacement grain at the current input timeline position.
static void pcm_stretch_spawn(pcm_stretch_t *st, memorypcm_preset_t *preset, uint16_t wave,
                              bool looping, uint32_t loopstart, uint32_t loopend) {
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
        d = pcm_stretch_best_offset(preset->sample_ram, preset->channels, wave, length, target, frame);
    }
    st->grain[gi].active = 1;
    st->grain[gi].start_frame = (uint32_t)((int32_t)frame + d);
    st->grain[gi].phase_q16 = st->in_pos_q16 & 0xffff;
    st->grain[gi].win_pos = 0;
    st->in_pos_q16 += st->hop_advance_q16;
}

static SAMPLE render_pcm_stretch(SAMPLE *buf, uint16_t osc, memorypcm_preset_t *preset) {
    pcm_stretch_t *st = &synth[osc]->stretch;
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
    SAMPLE amp = F2S(msynth[osc]->amp);
    const LUTSAMPLE *table = preset->sample_ram;
    uint32_t length = preset->length;
    bool looping = mode_is_looping(msynth[osc]->state);
    uint32_t loopstart = msynth[osc]->loopstart;
    uint32_t loopend = (msynth[osc]->loopend > loopstart && msynth[osc]->loopend <= length) ? msynth[osc]->loopend : length;
    uint16_t i = 0;
    if (msynth[osc]->pcm_delay) {
        // sample_offset: leave the head of the note-on block silent.
        i = msynth[osc]->pcm_delay;
        msynth[osc]->pcm_delay = 0;
    }
    for (; i < AMY_BLOCK_SIZE; i++) {
        if (st->hop_counter == 0)
            pcm_stretch_spawn(st, preset, synth[osc]->wave, looping, loopstart, loopend);
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
        bool want_stretch = AMY_IS_SET(synth[osc]->fit_ticks)
            && preset->type != AMY_PCM_TYPE_FILE
            && preset->sample_ram != NULL && preset->length > 0;
        bool fresh_start = true;
        if (synth[osc]->status == SYNTH_AUDIBLE && preset->type != AMY_PCM_TYPE_FILE
            && !want_stretch && !synth[osc]->stretch.active) {
            // Restarting a currently-playing (non-file) PCM, delay reonset to next zero crossing to avoid click.
            // (Not for the fit engine: its grains are windowed, so a restart is click-free by construction.)
            fresh_start = false;
            uint32_t base_index = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
            msynth[osc]->loopend = pcm_find_next_zero_crossing(osc, base_index);
            msynth[osc]->loopstart = INT_OF_P(phase, PCM_INDEX_BITS);;
            msynth[osc]->state = PCM_LOOP_ONCE_INTERNAL;
            msynth[osc]->next_state = synth[osc]->mode;
            //fprintf(stderr, "time %.3f osc %d RESTART amp %.3f last_amp %.3f\n", amy_global.time, osc, msynth[osc]->amp, msynth[osc]->last_amp);
        } else {
            synth[osc]->phase = phase;
            msynth[osc]->loopstart = preset->loopstart;
            msynth[osc]->loopend = preset->loopend;
            // Copy the looping mode from the wave mode field.  Can be updated on note_off.
            msynth[osc]->state = synth[osc]->mode;
        }
        // sample_offset (po): begin this note-on partway into its render
        // block, so slices of arbitrary length can butt-join sample-
        // accurately.  Only meaningful on a fresh start; a zero-crossing
        // restart already has fuzzy timing by design.
        msynth[osc]->pcm_delay = 0;
        if (fresh_start && AMY_IS_SET(synth[osc]->sample_offset))
            msynth[osc]->pcm_delay = synth[osc]->sample_offset % AMY_BLOCK_SIZE;
        if (want_stretch) pcm_stretch_note_on(osc, preset);
        else synth[osc]->stretch.active = 0;
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
        if (synth[osc]->stretch.active && preset->type != AMY_PCM_TYPE_FILE
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

        SAMPLE amp = F2S(msynth[osc]->amp);
        PHASOR step = F2P((playback_freq / (float)AMY_SAMPLE_RATE) / (float)(1 << (PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS)));
        const LUTSAMPLE* table = preset->sample_ram;
        uint32_t base_index_base = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
        uint32_t base_index = base_index_base;
        PHASOR phase = (synth[osc]->phase - (base_index_base << PCM_INDEX_FRAC_BITS)) << PCM_INDEX_STEP_EXTRA_BITS;
        // sample_offset (po): a fresh note-on starts partway into this block,
        // leaving the head silent, so slices can butt-join sample-accurately.
        uint16_t start_i = 0;
        if (msynth[osc]->pcm_delay) {
            start_i = msynth[osc]->pcm_delay;
            msynth[osc]->pcm_delay = 0;
        }
        for(uint16_t i=start_i; i < AMY_BLOCK_SIZE; i++) {
            SAMPLE frac = S_FRAC_OF_P(phase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS);
            LUTSAMPLE b = 0;
            LUTSAMPLE c = 0;
            uint32_t next_index = base_index + 1;
            // For non-file samples, we have to check for end of sample/looping.
            if(preset->type != AMY_PCM_TYPE_FILE) {
                if ((msynth[osc]->state == PCM_LOOP
                     || msynth[osc]->state == PCM_LOOP_ONCE_INTERNAL
                     || msynth[osc]->state == PCM_LOOP_STOP
                     || msynth[osc]->state == PCM_LOOP_FOREVER)
                    && base_index >= msynth[osc]->loopend) { // loopend
                    // still looping.  The state may be modified by pcm_note_off.
                    // back to loopstart
                    phase &= ((1L << (PCM_INDEX_FRAC_BITS + PCM_INDEX_STEP_EXTRA_BITS)) - 1);
                    base_index_base = msynth[osc]->loopstart + (base_index - msynth[osc]->loopend);
                    if (msynth[osc]->state == PCM_LOOP_ONCE_INTERNAL) {
                        msynth[osc]->state = msynth[osc]->next_state;  // Only loops once.
                        msynth[osc]->loopstart = preset->loopstart;
                        msynth[osc]->loopend = preset->loopend;
                    }
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
            buf[i] = value;   
            if (value < 0) value = -value;
            if (value > max_value) max_value = value;  
            phase = P_WRAPPED_SUM(phase, step);
            base_index = base_index_base + INT_OF_P(phase, PCM_INDEX_BITS - PCM_INDEX_STEP_EXTRA_BITS);
        }
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
        free(preset_pointer);
        // Go to the next one
        preset_pointer = next_pointer;
    }
    memorypcm_ll_start = NULL;
}
