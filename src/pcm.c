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
    uint8_t midinote;
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


void pcm_init() {
    memorypcm_ll_start = NULL;
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
        if (synth[osc]->status == SYNTH_AUDIBLE && preset->type != AMY_PCM_TYPE_FILE) {
            // Restarting a currently-playing (non-file) PCM, delay reonset to next zero crossing to avoid click.
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
        float logfreq = msynth[osc]->logfreq;
        // If osc[midi_note] is set, shift the freq by the preset's default base_note.
        if (AMY_IS_SET(synth[osc]->midi_note)) {
            logfreq -= logfreq_for_midi_note(preset->midinote);
        }
        float playback_freq = freq_of_logfreq(preset->log2sr + logfreq);
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
        for(uint16_t i=0; i < AMY_BLOCK_SIZE; i++) {
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
int16_t * pcm_load(uint16_t preset_number, uint32_t length, uint32_t samplerate, uint8_t channels, uint8_t midinote, uint32_t loopstart, uint32_t loopend) {
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
