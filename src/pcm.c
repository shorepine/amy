// pcm.c

#include "amy.h"



#define PCM_AMY_LOG2_SAMPLE_RATE log2f(PCM_AMY_SAMPLE_RATE / ZERO_LOGFREQ_IN_HZ)
#ifdef PCM_MUTABLE
#define PCM_AMY_LOG2_SAMPLE_RATE_FULL log2f(AMY_SAMPLE_RATE / ZERO_LOGFREQ_IN_HZ)

static float pt[] = {
    PCM_AMY_LOG2_SAMPLE_RATE,
    PCM_AMY_LOG2_SAMPLE_RATE_FULL,
};

#endif



void pcm_init() {

}

// How many bits used for fractional part of PCM table index.
#define PCM_INDEX_FRAC_BITS 15
// The number of bits used to hold the table index.
#define PCM_INDEX_BITS (31 - PCM_INDEX_FRAC_BITS)

void pcm_note_on(uint16_t osc) {
    //printf("pcm_note_on: osc=%d patch=%d logfreq=%f amp=%f\n",
    //       osc, synth[osc].patch, synth[osc].logfreq, synth[osc].amp);
    if(synth[osc].patch >= pcm_samples + PCM_USER) synth[osc].patch = 0;
    // if no freq given, just play it at midinote
    if(synth[osc].logfreq_coefs[0] <= 0) {
        // This will result in PCM_SAMPLE_RATE when the midi_note == patch->midinote.
#ifdef PCM_MUTABLE
        int patch = synth[osc].patch;
        synth[osc].logfreq_coefs[0] = pt[pcm_map[patch].sample_rate_index] - logfreq_for_midi_note(pcm_map[synth[osc].patch].midinote);
#else
        synth[osc].logfreq_coefs[0] = PCM_AMY_LOG2_SAMPLE_RATE - logfreq_for_midi_note(pcm_map[synth[osc].patch].midinote);
#endif
    }
    synth[osc].phase = 0; // s16.15 index into the table; as if a PHASOR into a 16 bit sample table. 
}

void pcm_mod_trigger(uint16_t osc) {
    pcm_note_on(osc);
}

void pcm_note_off(uint16_t osc) {
    // if looping set, disable looping; sample should play through to end.
    if(msynth[osc].feedback > 0) {
        msynth[osc].feedback = 0;
    } else {
        // Set phase to the end to cause immediate stop.
        synth[osc].phase = F2P(pcm_map[synth[osc].patch].length / (float)(1 << PCM_INDEX_FRAC_BITS));
    }
}

SAMPLE render_pcm(SAMPLE* buf, uint16_t osc) {
    // Patches can be > 32768 samples long.
    // We need s16.15 fixed-point indexing.
    PMUTATE pcm_map_t* patch = &pcm_map[synth[osc].patch];
    float logfreq = msynth[osc].logfreq;
    // If osc[midi_note] is unset, apply patch's default here.
    if (!AMY_IS_SET(synth[osc].midi_note))  logfreq += logfreq_for_midi_note(patch->midinote);
    float playback_freq = freq_of_logfreq(logfreq);  // PCM_SAMPLE_RATE modified by

    SAMPLE max_value = 0;
    SAMPLE amp = F2S(msynth[osc].amp);
    PHASOR step = F2P((playback_freq / (float)AMY_SAMPLE_RATE) / (float)(1 << PCM_INDEX_BITS));
    const LUTSAMPLE* table;
#ifdef PCM_MUTABLE
    if (patch->sample == NULL) {
        table = pcm + patch->offset;
    } else {
        table = patch->sample;
    }
#else
    table = pcm + patch->offset;
#endif
    uint32_t base_index = INT_OF_P(synth[osc].phase, PCM_INDEX_BITS);
    for(uint16_t i=0; i < AMY_BLOCK_SIZE; i++) {
        SAMPLE frac = S_FRAC_OF_P(synth[osc].phase, PCM_INDEX_BITS);
        LUTSAMPLE b = table[base_index];
        LUTSAMPLE c = b;
        if (base_index < patch->length) c = table[base_index + 1];
        SAMPLE sample = L2S(b) + MUL0_SS(L2S(c - b), frac);
        synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, step);
        base_index = INT_OF_P(synth[osc].phase, PCM_INDEX_BITS);
        if(base_index >= patch->length) { // end
            synth[osc].status = STATUS_OFF;// is this right? 
            sample = 0;
        } else {
            if(msynth[osc].feedback > 0) { // loop       
                if(base_index >= patch->loopend) { // loopend
                    // back to loopstart
                    int32_t loop_len = patch->loopend - patch->loopstart;
                    synth[osc].phase -= F2P(loop_len / (float)(1 << PCM_INDEX_FRAC_BITS));
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

SAMPLE compute_mod_pcm(uint16_t osc) {
    float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
    PHASOR step = F2P((PCM_AMY_SAMPLE_RATE / mod_sr) / (1 << PCM_INDEX_BITS));
    PMUTATE pcm_map_t* patch = &pcm_map[synth[osc].patch];
    const LUTSAMPLE* table;
#ifdef PCM_MUTABLE
    if (patch->sample == NULL) {
        table = pcm + patch->offset;
    } else {
        table = patch->sample;
    }
#else
    table = pcm + patch->offset;
#endif
    uint32_t base_index = INT_OF_P(synth[osc].phase, PCM_INDEX_BITS);
    SAMPLE sample;
    if(base_index >= patch->length) { // end
        synth[osc].status = STATUS_OFF;// is this right? 
        sample = 0;
    } else {
        sample = L2S(table[base_index]);
        synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, step);
    }
    return MUL4_SS(F2S(msynth[osc].amp), sample);
}
