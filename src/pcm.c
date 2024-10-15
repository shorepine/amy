// pcm.c

#include "amy.h"



// This is for any in-memory PCM samples.
typedef struct {
    int16_t * sample_ram;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
    uint32_t samplerate;
    float log2sr;
} memorypcm_patch_t;

// linked list of memorypcm patches
typedef struct memorypcm_ll_t{
    memorypcm_patch_t *patch;
    struct memorypcm_ll_t *next;
    uint16_t patch_number;
} memorypcm_ll_t;


memorypcm_ll_t * memorypcm_ll_start;

#define PCM_AMY_LOG2_SAMPLE_RATE log2f(PCM_AMY_SAMPLE_RATE / ZERO_LOGFREQ_IN_HZ)


memorypcm_patch_t * memorypatch_for_patch_number(uint16_t patch_number) {
    // run through the LL looking for the patch
    memorypcm_ll_t *patch = memorypcm_ll_start;
    while(patch != NULL) {
        if(patch->patch_number == patch_number) {
            if(patch->patch->sample_ram != NULL) {
                return patch->patch;
            }
        }
        patch = patch->next;
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


void pcm_note_on(uint16_t osc) {
    if(AMY_IS_SET(synth[osc].patch)) {
        memorypcm_patch_t *patch = memorypatch_for_patch_number(synth[osc].patch);
        if(patch != NULL) {
            // memory pcm
            if(synth[osc].logfreq_coefs[COEF_CONST] <= 0) {
                synth[osc].logfreq_coefs[COEF_CONST] = patch->log2sr - logfreq_for_midi_note(patch->midinote);
            }
        } else {
            // const PCM
            if(synth[osc].patch >= pcm_samples) synth[osc].patch = 0;
            // if no freq given, just play it at midinote
            if(synth[osc].logfreq_coefs[COEF_CONST] <= 0) {
                // This will result in PCM_SAMPLE_RATE when the midi_note == patch->midinote.
                synth[osc].logfreq_coefs[COEF_CONST] = PCM_AMY_LOG2_SAMPLE_RATE - logfreq_for_midi_note(pcm_map[synth[osc].patch].midinote);
            }

        }
        synth[osc].phase = 0; // s16.15 index into the table; as if a PHASOR into a 16 bit sample table.
        // Special case: We use the msynth feedback flag to indicate note-off for looping PCM.  As a result, it's explicitly NOT set in amy:hold_and_modify for PCM voices.  Set it here.
        msynth[osc].feedback = synth[osc].feedback;
    }
}

void pcm_mod_trigger(uint16_t osc) {
    pcm_note_on(osc);
}


void pcm_note_off(uint16_t osc) {
    if(AMY_IS_SET(synth[osc].patch)) {
        uint32_t length = 0;
        memorypcm_patch_t * patch = memorypatch_for_patch_number(synth[osc].patch);
        if(patch != NULL) {
            length = patch->length;
        } else {
            length = pcm_map[synth[osc].patch].length;
        }

        if(msynth[osc].feedback == 0) {
            // Non-looping note: Set phase to the end to cause immediate stop.
            synth[osc].phase = F2P(length / (float)(1 << PCM_INDEX_BITS));
        } else {
            // Looping is requested, disable future looping, sample will play through to end.
            // (sending a second note-off will stop it immediately).
            msynth[osc].feedback = 0;
        }
    }
}

// Get either memory patch or baked in patch for patch number
memorypcm_patch_t get_patch_for_patch_number(uint16_t patch_number) {
    // Get the memory patch. If it's null, copy params in from ROM patch
    memorypcm_patch_t patch;
    memorypcm_patch_t * patch_p = memorypatch_for_patch_number(patch_number);
    if(patch_p == NULL) {
        const pcm_map_t cpatch =  pcm_map[patch_number];
        patch.sample_ram = (int16_t*)pcm + cpatch.offset;
        patch.midinote = cpatch.midinote;
        patch.length = cpatch.length;
        patch.loopstart = cpatch.loopstart;
        patch.loopend = cpatch.loopend;
    } else {
        patch.sample_ram = patch_p->sample_ram;
        patch.midinote = patch_p->midinote;
        patch.length = patch_p->length;
        patch.loopstart = patch_p->loopstart;
        patch.loopend = patch_p->loopend;
    }
    return patch;
}

SAMPLE render_pcm(SAMPLE* buf, uint16_t osc) {
    if(AMY_IS_SET(synth[osc].patch)) {
        memorypcm_patch_t patch = get_patch_for_patch_number(synth[osc].patch);
        // Patches can be > 32768 samples long.
        // We need s16.15 fixed-point indexing.
        float logfreq = msynth[osc].logfreq;
        // If osc[midi_note] is unset, apply patch's default here.
        if (AMY_IS_UNSET(synth[osc].midi_note))  logfreq += logfreq_for_midi_note(patch.midinote);
        float playback_freq = freq_of_logfreq(logfreq);  // PCM_SAMPLE_RATE modified by

        SAMPLE max_value = 0;
        SAMPLE amp = F2S(msynth[osc].amp);
        PHASOR step = F2P((playback_freq / (float)AMY_SAMPLE_RATE) / (float)(1 << PCM_INDEX_BITS));
        const LUTSAMPLE* table = patch.sample_ram;
        uint32_t base_index = INT_OF_P(synth[osc].phase, PCM_INDEX_BITS);
        //fprintf(stderr, "render_pcm: time=%.3f patch=%d base_index=%d length=%d loopstart=%d loopend=%d fb=%f is_unset_note_off %d\n", total_samples / (float)AMY_SAMPLE_RATE, synth[osc].patch, base_index, patch->length, patch->loopstart, patch->loopend, msynth[osc].feedback, AMY_IS_UNSET(synth[osc].note_off_clock));
        for(uint16_t i=0; i < AMY_BLOCK_SIZE; i++) {
            SAMPLE frac = S_FRAC_OF_P(synth[osc].phase, PCM_INDEX_BITS);
            LUTSAMPLE b = table[base_index];
            LUTSAMPLE c = b;
            if (base_index < patch.length) c = table[base_index + 1];
            SAMPLE sample = L2S(b) + MUL0_SS(L2S(c - b), frac);
            synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, step);
            base_index = INT_OF_P(synth[osc].phase, PCM_INDEX_BITS);
            if(base_index >= patch.length) { // end
                synth[osc].status = SYNTH_OFF;// is this right? 
                sample = 0;
            } else {
                if(msynth[osc].feedback > 0) { // still looping.  The feedback flag is cleared by pcm_note_off.
                    if(base_index >= patch.loopend) { // loopend
                        // back to loopstart
                        int32_t loop_len = patch.loopend - patch.loopstart;
                        synth[osc].phase -= F2P(loop_len / (float)(1 << PCM_INDEX_BITS));
                        base_index -= loop_len;
                    }
                }
            }
            SAMPLE value = buf[i] + MUL4_SS(amp, sample);
            buf[i] = value;
            if (value < 0) value = -value;
            if (value > max_value) max_value = value;        
        }
        //printf("render_pcm: osc %d patch %d len %d base_ix %d phase %f step %f tablestep %f amp %f\n",
        //       osc, synth[osc].patch, patch->length, base_index, P2F(synth[osc].phase), P2F(step), (1 << PCM_INDEX_BITS) * P2F(step), S2F(msynth[osc].amp));

        return max_value;
    }
    return 0;
}


SAMPLE compute_mod_pcm(uint16_t osc) {
    if(AMY_IS_SET(synth[osc].patch)) {
        memorypcm_patch_t patch = get_patch_for_patch_number(synth[osc].patch);
        float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
        PHASOR step = F2P(((float)patch.samplerate / mod_sr) / (1 << PCM_INDEX_BITS));
        const LUTSAMPLE* table = patch.sample_ram;
        uint32_t base_index = INT_OF_P(synth[osc].phase, PCM_INDEX_BITS);
        SAMPLE sample;
        if(base_index >= patch.length) { // end
            synth[osc].status = SYNTH_OFF;// is this right? 
            sample = 0;
        } else {
            sample = L2S(table[base_index]);
            synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, step);
        }
        return MUL4_SS(F2S(msynth[osc].amp), sample);
    }
    return 0;
}



// load mono samples (let python parse wave files) into patch # 
// set loopstart, loopend, midinote, samplerate (and log2sr)
// return the allocated sample ram that AMY will fill in.
int16_t * pcm_load(uint16_t patch_number, uint32_t length, uint32_t samplerate, uint8_t midinote, uint32_t loopstart, uint32_t loopend) {
    // if patch was already a memorypcm, we need to unload it
    pcm_unload_patch(patch_number); // this is a no-op if patch doesn't exist or is a const pcm
    // now alloc a new LL entry and patch (the old LL entry is removed with pcm_unload_patch)
    memorypcm_ll_t *prev_patch_pointer = NULL;
    memorypcm_ll_t *new_patch_pointer = malloc(sizeof(memorypcm_ll_t));
    if(memorypcm_ll_start == NULL) {
        memorypcm_ll_start = new_patch_pointer;
    } else { // find the end
        memorypcm_ll_t *patch_pointer = memorypcm_ll_start;
        while(patch_pointer != NULL) {
            prev_patch_pointer = patch_pointer;
            patch_pointer = patch_pointer->next;
        }
        prev_patch_pointer->next = new_patch_pointer;
    }
    new_patch_pointer->next = NULL;
    new_patch_pointer->patch_number = patch_number;
    // Now alloc a patch
    memorypcm_patch_t *memory_patch = malloc_caps(sizeof(memorypcm_patch_t), SAMPLE_RAM_CAPS);
    memory_patch->samplerate = samplerate;
    memory_patch->log2sr = log2f((float)samplerate / ZERO_LOGFREQ_IN_HZ);
    memory_patch->midinote = midinote;
    memory_patch->loopstart = loopstart;
    memory_patch->length = length;
    memory_patch->sample_ram = malloc_caps(length*2, SAMPLE_RAM_CAPS);
    if(memory_patch->sample_ram  == NULL) {
        fprintf(stderr, "No RAM left for sample load\n");
        free(memory_patch);
        free(new_patch_pointer);
        prev_patch_pointer->next = NULL;
        return NULL; // no ram for sample
    }
    if(loopend == 0) {  // loop whole sample
        memory_patch->loopend = memory_patch->length-1;
    } else {
        memory_patch->loopend = loopend;        
    }
    new_patch_pointer->patch = memory_patch;
    return memory_patch->sample_ram;
}

void pcm_unload_patch(uint16_t patch_number) {
    // run through the LL looking for the patch
    memorypcm_ll_t *patch_pointer = memorypcm_ll_start;
    memorypcm_ll_t *prev_patch_pointer = NULL;
    while(patch_pointer != NULL) {
        if(patch_pointer->patch_number == patch_number) {
            // free the ram for the sample, if set        
            if(patch_pointer->patch->sample_ram != NULL) {
                free(patch_pointer->patch->sample_ram);
            }
            // if this was the first one, reset that
            if(memorypcm_ll_start == patch_pointer) {
                memorypcm_ll_start = NULL;
            }
            // free the patch
            free(patch_pointer->patch);
            // reset the next pointer
            if(prev_patch_pointer != NULL) {
                prev_patch_pointer->next = patch_pointer->next;
            }
            // free the ll entry
            free(patch_pointer);
            // End the loop
            patch_pointer = NULL;
        } else {
            prev_patch_pointer = patch_pointer;
            patch_pointer = patch_pointer->next;
        }
    }
}

void pcm_unload_all_patches() {
    memorypcm_ll_t *patch_pointer = memorypcm_ll_start;
    while(patch_pointer != NULL) {
        memorypcm_ll_t *next_pointer = patch_pointer->next;
        if(patch_pointer->patch->sample_ram != NULL) {
            free(patch_pointer->patch->sample_ram);
        }
        // free the patch
        free(patch_pointer->patch);
        // free the LL entry
        free(patch_pointer);
        // Go to the next one
        patch_pointer = next_pointer;
    }
    memorypcm_ll_start = NULL;
}







