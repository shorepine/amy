// pcm.c

#include "amy.h"
#include "transfer.h"

#ifdef AMY_DAISY
#define malloc_caps(a, b) qspi_malloc(a)
#define free(a) qspi_free(a)
#endif


// This is for any in-memory PCM samples.
typedef struct {
    int16_t * sample_ram;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
    uint32_t samplerate;
    float log2sr;
} memorypcm_preset_t;

// This is for file-based PCM samples that stream from disk.
typedef struct {
    wav_reader_t *wav_reader;
    int16_t *ring_buffer;
    uint32_t ring_buffer_size;
    uint32_t ring_buffer_read_pos;
    uint32_t ring_buffer_write_pos;
    uint32_t file_read_pos;  // Current position in the file
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
    uint32_t samplerate;
    float log2sr;
    uint32_t refill_threshold;  // When to refill the buffer
} filepcm_preset_t;

// Unified preset structure for rendering
typedef struct {
    int16_t *sample_ram;  // For memory presets, points to sample_ram; for file presets, points to ring_buffer
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
    uint32_t samplerate;
    float log2sr;
    // File-specific fields (only used for file presets)
    filepcm_preset_t *file_preset;  // NULL for memory presets
} unified_pcm_preset_t;

// linked list of memorypcm presets
typedef struct memorypcm_ll_t{
    memorypcm_preset_t *preset;
    struct memorypcm_ll_t *next;
    uint16_t preset_number;
} memorypcm_ll_t;

// linked list of filepcm presets
typedef struct filepcm_ll_t{
    filepcm_preset_t *preset;
    struct filepcm_ll_t *next;
    uint16_t preset_number;
} filepcm_ll_t;


memorypcm_ll_t * memorypcm_ll_start;
filepcm_ll_t * filepcm_ll_start;

#define PCM_AMY_LOG2_SAMPLE_RATE log2f(PCM_AMY_SAMPLE_RATE / ZERO_LOGFREQ_IN_HZ)


memorypcm_preset_t * memorypreset_for_preset_number(uint16_t preset_number) {
    // run through the LL looking for the preset
    memorypcm_ll_t *preset = memorypcm_ll_start;
    while(preset != NULL) {
        if(preset->preset_number == preset_number) {
            if(preset->preset->sample_ram != NULL) {
                return preset->preset;
            }
        }
        preset = preset->next;
    }
    return NULL;
}

filepcm_preset_t * filepreset_for_preset_number(uint16_t preset_number) {
    // run through the LL looking for the preset
    filepcm_ll_t *preset = filepcm_ll_start;
    while(preset != NULL) {
        if(preset->preset_number == preset_number) {
            if(preset->preset->wav_reader != NULL) {
                return preset->preset;
            }
        }
        preset = preset->next;
    }
    return NULL;
}

void pcm_init() {
    memorypcm_ll_start = NULL;
    filepcm_ll_start = NULL;
}

// How many bits used for fractional part of PCM table index.
#define PCM_INDEX_FRAC_BITS 8
// The number of bits used to hold the table index.
#define PCM_INDEX_BITS (31 - PCM_INDEX_FRAC_BITS)


void pcm_note_on(uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        memorypcm_preset_t *preset = memorypreset_for_preset_number(synth[osc]->preset);
        if(preset == NULL) {
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

// Get either memory preset, file preset, or baked in preset for preset number
unified_pcm_preset_t get_preset_for_preset_number(uint16_t preset_number) {
    unified_pcm_preset_t preset;
    
    // First check for file preset
    filepcm_preset_t * file_preset_p = filepreset_for_preset_number(preset_number);
    if(file_preset_p != NULL) {
        preset.sample_ram = file_preset_p->ring_buffer;
        preset.length = file_preset_p->length;
        preset.loopstart = file_preset_p->loopstart;
        preset.loopend = file_preset_p->loopend;
        preset.midinote = file_preset_p->midinote;
        preset.samplerate = file_preset_p->samplerate;
        preset.log2sr = file_preset_p->log2sr;
        preset.file_preset = file_preset_p;
        return preset;
    }
    
    // Then check for memory preset
    memorypcm_preset_t * memory_preset_p = memorypreset_for_preset_number(preset_number);
    if(memory_preset_p != NULL) {
        preset.sample_ram = memory_preset_p->sample_ram;
        preset.length = memory_preset_p->length;
        preset.loopstart = memory_preset_p->loopstart;
        preset.loopend = memory_preset_p->loopend;
        preset.midinote = memory_preset_p->midinote;
        preset.samplerate = memory_preset_p->samplerate;
        preset.log2sr = memory_preset_p->log2sr;
        preset.file_preset = NULL;
        return preset;
    }
    
    // Finally, use baked-in preset
    const pcm_map_t cpreset = pcm_map[preset_number];
    preset.sample_ram = (int16_t*)pcm + cpreset.offset;
    preset.length = cpreset.length;
    preset.loopstart = cpreset.loopstart;
    preset.loopend = cpreset.loopend;
    preset.midinote = cpreset.midinote;
    preset.samplerate = PCM_AMY_SAMPLE_RATE;
    preset.log2sr = PCM_AMY_LOG2_SAMPLE_RATE;
    preset.file_preset = NULL;
    return preset;
}

// Helper function to refill the ring buffer for file-based PCM
void refill_ring_buffer(filepcm_preset_t *file_preset) {
    if (file_preset == NULL || file_preset->wav_reader == NULL) return;
    
    uint32_t available_space = file_preset->ring_buffer_size - 
                              ((file_preset->ring_buffer_write_pos - file_preset->ring_buffer_read_pos) % file_preset->ring_buffer_size);
    
    if (available_space < file_preset->refill_threshold) {
        // Calculate how many samples we can read
        uint32_t samples_to_read = MIN(available_space, file_preset->length - file_preset->file_read_pos);
        if (samples_to_read == 0) return;
        
        // Read samples from file
        size_t samples_read = wav_read(file_preset->wav_reader, file_preset->file_read_pos, samples_to_read, 
                                      file_preset->ring_buffer + file_preset->ring_buffer_write_pos);
        
        if (samples_read > 0) {
            file_preset->ring_buffer_write_pos = (file_preset->ring_buffer_write_pos + samples_read) % file_preset->ring_buffer_size;
            file_preset->file_read_pos += samples_read;
        }
    }
}

SAMPLE render_pcm(SAMPLE* buf, uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        unified_pcm_preset_t preset = get_preset_for_preset_number(synth[osc]->preset);
        
        // Refill ring buffer if this is a file-based preset
        if (preset.file_preset != NULL) {
            refill_ring_buffer(preset.file_preset);
        }
        
        // Presets can be > 32768 samples long.
        // We need s16.15 fixed-point indexing.
        float logfreq = msynth[osc]->logfreq;
        // If osc[midi_note] is set, shift the freq by the preset's default base_note.
        if (AMY_IS_SET(synth[osc]->midi_note))  logfreq -= logfreq_for_midi_note(preset.midinote);
        float playback_freq = freq_of_logfreq(PCM_AMY_LOG2_SAMPLE_RATE + logfreq);

        SAMPLE max_value = 0;
        SAMPLE amp = F2S(msynth[osc]->amp);
        PHASOR step = F2P((playback_freq / (float)AMY_SAMPLE_RATE) / (float)(1 << PCM_INDEX_BITS));
        const LUTSAMPLE* table = preset.sample_ram;
        uint32_t base_index = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
        //fprintf(stderr, "render_pcm: time=%.3f preset=%d base_index=%d length=%d loopstart=%d loopend=%d fb=%f is_unset_note_off %d\n", amy_global.total_blocks*AMY_BLOCK_SIZE / (float)AMY_SAMPLE_RATE, synth[osc]->preset, base_index, preset->length, preset->loopstart, preset->loopend, msynth[osc]->feedback, AMY_IS_UNSET(synth[osc]->note_off_clock));
        for(uint16_t i=0; i < AMY_BLOCK_SIZE; i++) {
            SAMPLE frac = S_FRAC_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
            LUTSAMPLE b = table[base_index];
            LUTSAMPLE c = b;
            if (base_index < preset.length) c = table[base_index + 1];
            SAMPLE sample = L2S(b) + MUL0_SS(L2S(c - b), frac);
            synth[osc]->phase = P_WRAPPED_SUM(synth[osc]->phase, step);
            base_index = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
            if(base_index >= preset.length) { // end
                synth[osc]->status = SYNTH_OFF;// is this right?
                sample = 0;
            } else {
                if(msynth[osc]->feedback > 0) { // still looping.  The feedback flag is cleared by pcm_note_off.
                    if(base_index >= preset.loopend) { // loopend
                        // back to loopstart
                        int32_t loop_len = preset.loopend - preset.loopstart;
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

        return max_value;
    }
    return 0;
}


SAMPLE compute_mod_pcm(uint16_t osc) {
    if(AMY_IS_SET(synth[osc]->preset)) {
        unified_pcm_preset_t preset = get_preset_for_preset_number(synth[osc]->preset);
        float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
        PHASOR step = F2P(((float)preset.samplerate / mod_sr) / (1 << PCM_INDEX_BITS));
        const LUTSAMPLE* table = preset.sample_ram;
        uint32_t base_index = INT_OF_P(synth[osc]->phase, PCM_INDEX_BITS);
        SAMPLE sample;
        if(base_index >= preset.length) { // end
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



// load mono samples into preset # 
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
    memory_preset->sample_ram = (int16_t *)(((uint8_t *)memory_preset) + sizeof(memorypcm_preset_t));
    if(loopend == 0) {  // loop whole sample
        memory_preset->loopend = memory_preset->length-1;
    } else {
        memory_preset->loopend = loopend;
    }
    new_preset_pointer->preset = memory_preset;
    return memory_preset->sample_ram;
}

// Load PCM samples from a file using streaming with ring buffer
int pcm_file_load(uint16_t preset_number, char* filename, uint8_t midinote, uint32_t loopstart, uint32_t loopend) {
    // if preset was already a memorypcm or filepcm, we need to unload it
    pcm_unload_preset(preset_number);
    
    // Open the WAV file
    AMY_FILE *fp = get_file_pointer(filename);
    if (fp == NULL) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return -1;
    }
    
    // Parse the WAV file
    wav_reader_t *wav_reader = wav_open(fp);
    if (wav_reader == NULL) {
        fprintf(stderr, "Failed to parse WAV file: %s\n", filename);
        close_file(fp);
        return -1;
    }
    
    // Check if it's mono 16-bit PCM
    if (wav_reader->num_channels != 1 || wav_reader->bits_per_sample != 16) {
        fprintf(stderr, "WAV file must be mono 16-bit PCM: %s\n", filename);
        wav_close(wav_reader);
        close_file(fp);
        return -1;
    }
    
    // Calculate ring buffer size (at least 8 * AMY_BLOCK_SIZE)
    uint32_t ring_buffer_size = 8 * AMY_BLOCK_SIZE;
    if (wav_reader->num_samples < ring_buffer_size) {
        ring_buffer_size = wav_reader->num_samples;
    }
    
    // Allocate memory for the linked list entry and preset
    filepcm_ll_t *new_preset_pointer = malloc_caps(sizeof(filepcm_ll_t) + sizeof(filepcm_preset_t) + ring_buffer_size * sizeof(int16_t),
                                                   amy_global.config.ram_caps_sample);
    if (new_preset_pointer == NULL) {
        fprintf(stderr, "No RAM left for file PCM load\n");
        wav_close(wav_reader);
        close_file(fp);
        return -1;
    }
    
    // Set up the linked list
    new_preset_pointer->next = filepcm_ll_start;
    filepcm_ll_start = new_preset_pointer;
    new_preset_pointer->preset_number = preset_number;
    
    // Set up the preset structure
    filepcm_preset_t *file_preset = (filepcm_preset_t *)(((uint8_t *)new_preset_pointer) + sizeof(filepcm_ll_t));
    file_preset->wav_reader = wav_reader;
    file_preset->ring_buffer = (int16_t *)(((uint8_t *)file_preset) + sizeof(filepcm_preset_t));
    file_preset->ring_buffer_size = ring_buffer_size;
    file_preset->ring_buffer_read_pos = 0;
    file_preset->ring_buffer_write_pos = 0;
    file_preset->file_read_pos = 0;
    file_preset->length = wav_reader->num_samples;
    file_preset->samplerate = wav_reader->sample_rate;
    file_preset->log2sr = log2f((float)wav_reader->sample_rate / ZERO_LOGFREQ_IN_HZ);
    file_preset->midinote = midinote;
    file_preset->loopstart = loopstart;
    file_preset->loopend = (loopend == 0) ? wav_reader->num_samples - 1 : loopend;
    file_preset->refill_threshold = 2 * AMY_BLOCK_SIZE;  // Refill when we have less than 2 blocks left
    
    new_preset_pointer->preset = file_preset;
    
    // Load initial data into the ring buffer
    size_t initial_samples = MIN(ring_buffer_size, wav_reader->num_samples);
    size_t samples_read = wav_read(wav_reader, 0, initial_samples, file_preset->ring_buffer);
    if (samples_read > 0) {
        file_preset->ring_buffer_write_pos = samples_read;
        file_preset->file_read_pos = samples_read;
    }
    
    return 0;  // Success
}

void pcm_unload_preset(uint16_t preset_number) {
    // First try to unload from memory PCM list
    memorypcm_ll_t **preset_pointer = &memorypcm_ll_start;
    while(*preset_pointer != NULL) {
        if((*preset_pointer)->preset_number == preset_number) {
	    memorypcm_ll_t *next = (*preset_pointer)->next;
	    //fprintf(stderr, "unload_preset: unloading memory preset %d\n", (*preset_pointer)->preset_number);
            // free the memory we allocated
            free((*preset_pointer));
	    // close up the list
	    *preset_pointer = next;
	    return;
        } else {
            preset_pointer = &(*preset_pointer)->next;
        }
    }
    
    // Then try to unload from file PCM list
    filepcm_ll_t **file_preset_pointer = &filepcm_ll_start;
    while(*file_preset_pointer != NULL) {
        if((*file_preset_pointer)->preset_number == preset_number) {
	    filepcm_ll_t *next = (*file_preset_pointer)->next;
	    //fprintf(stderr, "unload_preset: unloading file preset %d\n", (*file_preset_pointer)->preset_number);
            // Close the WAV file and free the memory
            if ((*file_preset_pointer)->preset->wav_reader != NULL) {
                wav_close((*file_preset_pointer)->preset->wav_reader);
            }
            free((*file_preset_pointer));
	    // close up the list
	    *file_preset_pointer = next;
	    return;
        } else {
            file_preset_pointer = &(*file_preset_pointer)->next;
        }
    }
    //fprintf(stderr, "pcm_unload_preset: preset %d not found\n", preset_number);  // This happens during a routine load_preset.
}

void pcm_unload_all_presets() {
    // Unload all memory presets
    memorypcm_ll_t *preset_pointer = memorypcm_ll_start;
    while(preset_pointer != NULL) {
        memorypcm_ll_t *next_pointer = preset_pointer->next;
	//fprintf(stderr, "unload_all_presets: unloading memory preset %d\n", preset_pointer->preset_number);
        free(preset_pointer);
        // Go to the next one
        preset_pointer = next_pointer;
    }
    memorypcm_ll_start = NULL;
    
    // Unload all file presets
    filepcm_ll_t *file_preset_pointer = filepcm_ll_start;
    while(file_preset_pointer != NULL) {
        filepcm_ll_t *next_pointer = file_preset_pointer->next;
	//fprintf(stderr, "unload_all_presets: unloading file preset %d\n", file_preset_pointer->preset_number);
        // Close the WAV file
        if (file_preset_pointer->preset->wav_reader != NULL) {
            wav_close(file_preset_pointer->preset->wav_reader);
        }
        free(file_preset_pointer);
        // Go to the next one
        file_preset_pointer = next_pointer;
    }
    filepcm_ll_start = NULL;
}
