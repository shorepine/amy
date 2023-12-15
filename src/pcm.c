// pcm.c

#include "amy.h"

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
} pcm_map_t;

#if AMY_PCM_PATCHES_SIZE == 2
#include "pcm_large.h"
#include "pcm_samples_large.h"
#else
#include "pcm_small.h"
#include "pcm_samples_small.h"
#endif


void pcm_init() {
/*
    // For ESP, we can mmap the PCM blob on the luts partition -- do this if you are using OTA 
#ifdef ESP_PLATFORM
    spi_flash_mmap_handle_t mmap_handle;
    const esp_partition_t * pcm_part  = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "luts");
    esp_err_t err = esp_partition_mmap(pcm_part, 0, PCM_LENGTH*2, SPI_FLASH_MMAP_DATA, (const void**)&pcm, &mmap_handle);
    if(err != ESP_OK) {
        printf("err doing pcm mmap: %d %s\n", err, esp_err_to_name(err));
    }
#else
    pcm = pcm_desktop;
#endif
*/
}

// How many bits used for fractional part of PCM table index.
#define PCM_INDEX_FRAC_BITS 15
// The number of bits used to hold the table index.
#define PCM_INDEX_BITS (31 - PCM_INDEX_FRAC_BITS)

void pcm_note_on(uint16_t osc) {
    //printf("pcm_note_on: osc=%d patch=%d freq=%f amp=%f\n",
    //       osc, synth[osc].patch, synth[osc].freq, synth[osc].amp);
    if(synth[osc].patch < 0) synth[osc].patch = 0;
    // if no freq given, just play it at midinote
    if(synth[osc].freq <= 0)
        synth[osc].freq = PCM_AMY_SAMPLE_RATE; // / freq_for_midi_note(patch->midinote);
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
        synth[osc].phase = pcm_map[synth[osc].patch].length << PCM_INDEX_FRAC_BITS;
    }
}

void render_pcm(SAMPLE* buf, uint16_t osc) {
    // Patches can be > 32768 samples long.
    // We need s16.15 fixed-point indexing.
    const pcm_map_t* patch = &pcm_map[synth[osc].patch];
    float playback_freq = PCM_AMY_SAMPLE_RATE;
    if(msynth[osc].freq < PCM_AMY_SAMPLE_RATE) { // user adjusted freq 
        float base_freq = freq_for_midi_note(patch->midinote); 
        playback_freq = (msynth[osc].freq / base_freq) * PCM_AMY_SAMPLE_RATE;
    }
    SAMPLE amp = F2S(msynth[osc].amp);
    PHASOR step = F2P((playback_freq / (float)AMY_SAMPLE_RATE) / (float)(1 << PCM_INDEX_BITS));
    const LUTSAMPLE* table = pcm + patch->offset;
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
            synth[osc].status = OFF;// is this right? 
            sample = 0;
        } else {
            if(msynth[osc].feedback > 0) { // loop       
                if(base_index >= patch->loopend) { // loopend
                    // back to loopstart
                    int32_t loop_len = patch->loopend - patch->loopstart;
                    synth[osc].phase -= loop_len << PCM_INDEX_FRAC_BITS;
                    base_index -= loop_len;
                }
            }
        }
        buf[i] += MUL4_SS(amp, sample);
    }
    //printf("render_pcm: osc %d patch %d len %d base_ix %d phase %f step %f tablestep %f amp %f\n",
    //       osc, synth[osc].patch, patch->length, base_index, P2F(synth[osc].phase), P2F(step), (1 << PCM_INDEX_BITS) * P2F(step), S2F(msynth[osc].amp));
 }

SAMPLE compute_mod_pcm(uint16_t osc) {
    float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
    PHASOR step = F2P((PCM_AMY_SAMPLE_RATE / mod_sr) / (1 << PCM_INDEX_BITS));
    const pcm_map_t* patch = &pcm_map[synth[osc].patch];
    const LUTSAMPLE* table = pcm + patch->offset;
    uint32_t base_index = INT_OF_P(synth[osc].phase, PCM_INDEX_BITS);
    SAMPLE sample;
    if(base_index >= patch->length) { // end
        synth[osc].status = OFF;// is this right? 
        sample = 0;
    } else {
        sample = L2S(table[base_index]);
        synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, step);
    }
    return MUL4_SS(F2S(msynth[osc].amp), sample);
}
