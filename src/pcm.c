// pcm.c

#include "amy.h"
#include "transfer.h"

#ifdef AMY_DAISY
#define malloc_caps(a, b) qspi_malloc(a)
#define free(a) qspi_free(a)
#endif


// This is for any in-memory PCM samples.
typedef struct {
    uint8_t channels;
    char filename[MAX_FILENAME_LEN]; // if 0 / NULL this uses sample_ram
    uint32_t file_handle;
    uint8_t file_parsed;
    uint16_t file_bits_per_sample;
    uint32_t file_bytes_remaining;
    int16_t * sample_ram;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
    uint32_t samplerate;
    float log2sr;
    uint8_t rom;
} memorypcm_preset_t;

// linked list of memorypcm presets
typedef struct memorypcm_ll_t{
    memorypcm_preset_t *preset;
    struct memorypcm_ll_t *next;
    uint16_t preset_number;
} memorypcm_ll_t;


memorypcm_ll_t * memorypcm_ll_start;

#define PCM_AMY_LOG2_SAMPLE_RATE log2f(PCM_AMY_SAMPLE_RATE / ZERO_LOGFREQ_IN_HZ)


memorypcm_preset_t * memorypreset_for_preset_number(uint16_t preset_number) {
    // run through the LL looking for the preset -- can be either memory or sample
    memorypcm_ll_t *preset = memorypcm_ll_start;
    while(preset != NULL) {
        if(preset->preset_number == preset_number) {
            if(preset->preset->sample_ram != NULL || preset->preset->file_handle > 0) {
                return preset->preset;
            }
        }
        preset = preset->next;
    }
    return NULL;
}

void pcm_init() {
    memorypcm_ll_start = NULL;
}

// How many bits used for fractional part of PCM table index.
#define PCM_INDEX_FRAC_BITS 8
// The number of bits used to hold the table index.
#define PCM_INDEX_BITS (31 - PCM_INDEX_FRAC_BITS)
// File-streaming buffer size multiplier (in blocks).
#define PCM_FILE_BUFFER_MULT 8


void pcm_note_on(uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        memorypcm_preset_t *preset = memorypreset_for_preset_number(synth[osc]->preset);
        if(preset->rom) {
            // baked-in PCM - don't overrun.
            if(synth[osc]->preset >= pcm_samples) synth[osc]->preset = 0;
        }
        synth[osc]->phase = 0; // s16.15 index into the table; as if a PHASOR into a 16 bit sample table.
        // Special case: We use the msynth feedback flag to indicate note-off for looping PCM.  As a result, it's explicitly NOT set in amy:hold_and_modify for PCM voices.  Set it here.
        msynth[osc]->feedback = synth[osc]->feedback;

    }
}

void pcm_mod_trigger(uint16_t osc) {
    pcm_note_on(osc);
}


void pcm_note_off(uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        uint32_t length = 0;
        memorypcm_preset_t * preset = memorypreset_for_preset_number(synth[osc]->preset);
        if(preset != NULL) {
            length = preset->length;
        } else {
            length = pcm_map[synth[osc]->preset].length;
        }

        if(msynth[osc]->feedback == 0) {
            // Non-looping note: Set phase to the end to cause immediate stop.
            synth[osc]->phase = F2P(length / (float)(1 << PCM_INDEX_BITS));
        } else {
            // Looping is requested, disable future looping, sample will play through to end.
            // (sending a second note-off will stop it immediately).
            msynth[osc]->feedback = 0;
        }
    }
}

// Get either memory preset or baked in preset for preset number
memorypcm_preset_t * get_preset_for_preset_number(uint16_t preset_number) {
    // Get the memory preset. If we can't find it, it's a ROM preset. So copy params in from ROM preset
    memorypcm_preset_t * preset_p = memorypreset_for_preset_number(preset_number);
    if(preset_p == NULL) {
        static memorypcm_preset_t preset;
        memset(&preset, 0, sizeof(preset));
        const pcm_map_t cpreset =  pcm_map[preset_number];
        preset.sample_ram = (int16_t*)pcm + cpreset.offset;
        preset.length = cpreset.length;
        preset.loopstart = cpreset.loopstart;
        preset.loopend = cpreset.loopend;
        preset.midinote = cpreset.midinote;
        preset.samplerate = PCM_AMY_SAMPLE_RATE;
        preset.log2sr = PCM_AMY_LOG2_SAMPLE_RATE;
        preset.rom = 1;
        return &preset;
    } else {
        return preset_p;
    }
    return NULL;
}

uint32_t fill_sample_from_file(memorypcm_preset_t *preset_p, uint32_t frames_needed) {
    uint32_t bytes_per_frame = preset_p->channels * (preset_p->file_bits_per_sample / 8);
    uint32_t frames_available = 0;
    if (bytes_per_frame > 0) {
        frames_available = preset_p->file_bytes_remaining / bytes_per_frame;
    }
    if (frames_available > 0 && frames_needed > frames_available) {
        frames_needed = frames_available;
    }
    //fprintf(stderr, "fill_sample_from_file: handle %d bytes_remaining %d frames_needed %d\n", preset_p->file_handle, preset_p->file_bytes_remaining, frames_needed);
    uint32_t frames_read = wave_read_pcm_frames_s16(
        preset_p->file_handle,
        preset_p->channels,
        preset_p->file_bits_per_sample,
        &preset_p->file_bytes_remaining,
        preset_p->sample_ram,
        frames_needed);
    return frames_read;
}

SAMPLE render_pcm(SAMPLE* buf, uint16_t osc) {
    uint8_t is_file_flag = 0;
    if(AMY_IS_SET(synth[osc]->preset)) {
        memorypcm_preset_t * preset = get_preset_for_preset_number(synth[osc]->preset);
        // Presets can be > 32768 samples long.
        // We need s16.15 fixed-point indexing.
        float logfreq = msynth[osc]->logfreq;
        // If osc[midi_note] is set, shift the freq by the preset's default base_note.
        if (AMY_IS_SET(synth[osc]->midi_note)) {
            logfreq -= logfreq_for_midi_note(preset->midinote);
        }
        float playback_freq = freq_of_logfreq(PCM_AMY_LOG2_SAMPLE_RATE + logfreq);
        if (preset->file_handle != 0) { // file
            is_file_flag = 1;
            float frames_per_output = playback_freq / (float)AMY_SAMPLE_RATE;
            uint32_t frames_needed = (uint32_t)ceilf(frames_per_output * AMY_BLOCK_SIZE) + 1;
            uint32_t max_frames = AMY_BLOCK_SIZE * PCM_FILE_BUFFER_MULT;
            if (frames_needed > max_frames) {
                frames_needed = max_frames;
            }
            //fprintf(stderr, "render_pcm: osc %d preset %d filling file sample buffer: frames_needed=%d bytes_remaining=%d playback_freq=%f\n",
            //        osc, synth[osc]->preset, frames_needed, preset->file_bytes_remaining, playback_freq);
            uint32_t t = fill_sample_from_file(preset, frames_needed);
        }

        SAMPLE max_value = 0;
        SAMPLE amp = F2S(msynth[osc]->amp);
        PHASOR step = F2P((playback_freq / (float)AMY_SAMPLE_RATE) / (float)(1 << PCM_INDEX_BITS));
        const LUTSAMPLE* table = preset->sample_ram;
        if(is_file_flag) {
            synth[osc]->phase = 0; // always from start for file samples
        }
        uint32_t base_index = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
        //fprintf(stderr, "render_pcm: time=%.3f preset=%d base_index=%d length=%d loopstart=%d loopend=%d fb=%f is_unset_note_off %d\n", amy_global.total_blocks*AMY_BLOCK_SIZE / (float)AMY_SAMPLE_RATE, synth[osc]->preset, base_index, preset->length, preset->loopstart, preset->loopend, msynth[osc]->feedback, AMY_IS_UNSET(synth[osc]->note_off_clock));
        for(uint16_t i=0; i < AMY_BLOCK_SIZE; i++) {
            SAMPLE frac = S_FRAC_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
            LUTSAMPLE b = table[base_index];
            LUTSAMPLE c = b;
            if (base_index < preset->length) c = table[base_index + 1];
            SAMPLE sample = L2S(b) + MUL0_SS(L2S(c - b), frac);
            synth[osc]->phase = P_WRAPPED_SUM(synth[osc]->phase, step);
            base_index = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
            if(base_index >= preset->length) { // end
                //fprintf(stderr, "render_pcm: osc %d preset %d reached end at i=%d base_index=%d length=%d\n",
                //       osc, synth[osc]->preset, i, base_index, preset->length);
                synth[osc]->status = SYNTH_OFF;// is this right?
                sample = 0;
            } else {
                if(msynth[osc]->feedback > 0) { // still looping.  The feedback flag is cleared by pcm_note_off.
                    if(base_index >= preset->loopend) { // loopend
                        // back to loopstart
                        int32_t loop_len = preset->loopend - preset->loopstart;
                        synth[osc]->phase -= F2P(loop_len / (float)(1 << PCM_INDEX_BITS));
                        base_index -= loop_len;
                    }
                }
            }
            SAMPLE value = buf[i] + MUL4_SS(amp, sample);
            buf[i] = value;
            if (value < 0) value = -value;
            if (value > max_value) max_value = value;        
        }
        //printf("render_pcm: osc %d preset %d len %d base_ix %d phase %f step %f tablestep %f amp %f\n",
        //       osc, synth[osc]->preset, preset->length, base_index, P2F(synth[osc]->phase), P2F(step), (1 << PCM_INDEX_BITS) * P2F(step), S2F(msynth[osc]->amp));
        if(!is_file_flag) { 
            return max_value; 
        } else { 
            return 1; 
        }
    }
    return 0;
}


SAMPLE compute_mod_pcm(uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        memorypcm_preset_t *preset = get_preset_for_preset_number(synth[osc]->preset);
        float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
        PHASOR step = F2P(((float)preset->samplerate / mod_sr) / (1 << PCM_INDEX_BITS));
        const LUTSAMPLE* table = preset->sample_ram;
        uint32_t base_index = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
        SAMPLE sample;
        if(base_index >= preset->length) { // end
            fprintf(stderr, "synth off\n");
            synth[osc]->status = SYNTH_OFF;// is this right?
            sample = 0;
        } else {
            sample = L2S(table[base_index]);
            synth[osc]->phase = P_WRAPPED_SUM(synth[osc]->phase, step);
        }
        return MUL4_SS(F2S(msynth[osc]->amp), sample);
    }
    return 0;
}


int pcm_load_file(uint16_t preset_number, const char *filename, uint8_t midinote,
                  uint32_t loopstart, uint32_t loopend) {
    pcm_unload_preset(preset_number);
    if (filename == NULL || filename[0] == '\0') {
        return 0;
    }
    if (amy_external_fopen_hook == NULL || amy_external_fclose_hook == NULL) {
        fprintf(stderr, "fopen hook not enabled on platform\n");
        return 0;
    }
    uint32_t handle = amy_external_fopen_hook((char *)filename, "rb");
    if (handle == 0) {
        fprintf(stderr, "Could not open file %s\n", filename);
        return 0;
    }
    wave_info_t info = {0};
    uint32_t data_bytes = 0;
    if (!wave_parse_header(handle, &info, &data_bytes)) {
        fprintf(stderr, "Could not parse WAVE file %s\n", filename);
        amy_external_fclose_hook(handle);
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
    memory_preset->loopstart = loopstart;
    memory_preset->length = total_frames;
    if (loopend == 0 && total_frames > 0) {
        memory_preset->loopend = total_frames - 1;
    } else {
        memory_preset->loopend = loopend;
    }
    memory_preset->file_parsed = 1;
    memory_preset->rom = 0;
    memory_preset->file_bits_per_sample = info.bits_per_sample;
    memory_preset->file_bytes_remaining = total_frames * info.channels * (info.bits_per_sample / 8);
    memory_preset->file_handle = handle;
    memory_preset->sample_ram = malloc_caps(buffer_frames * sizeof(int16_t),
                                                     amy_global.config.ram_caps_sample);
    new_preset_pointer->preset = memory_preset;
    fprintf(stderr, "read file %s frames %d channels %d preset %d handle %d\n", filename, total_frames, info.channels, preset_number, handle);
    return 1;
}


// load mono samples (let python parse wave files) into preset # 
// set loopstart, loopend, midinote, samplerate (and log2sr)
// return the allocated sample ram that AMY will fill in.
int16_t * pcm_load(uint16_t preset_number, uint32_t length, uint32_t samplerate, uint8_t midinote, uint32_t loopstart, uint32_t loopend) {
    // if preset was already a memorypcm, we need to unload it
    pcm_unload_preset(preset_number); // this is a no-op if preset doesn't exist or is a const pcm
    // now alloc a new LL entry and preset (the old LL entry is removed with pcm_unload_preset)
    memorypcm_ll_t *new_preset_pointer = malloc_caps(sizeof(memorypcm_ll_t) + sizeof(memorypcm_preset_t) + length * sizeof(int16_t),
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
    memory_preset->channels = 1;
    memory_preset->file_parsed = 0;
    memory_preset->file_bits_per_sample = 0;
    memory_preset->file_bytes_remaining = 0;
    memory_preset->file_handle = 0;
    memory_preset->rom = 0;
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
	    //fprintf(stderr, "unload_preset: unloading %d\n", (*preset_pointer)->preset_number);
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
	//fprintf(stderr, "unload_all_presets: unloading %d\n", preset_pointer->preset_number);
        free(preset_pointer);
        // Go to the next one
        preset_pointer = next_pointer;
    }
    memorypcm_ll_start = NULL;
}
