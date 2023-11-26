// algorithms.c
// FM2 and partial synths that involve combinations of oscillators
#include "amy.h"

#define NUM_ALGO_BPS 5

typedef struct {
    float freq;
    float freq_ratio;
    float amp;
    float amp_rate[NUM_ALGO_BPS];
    float amp_time[NUM_ALGO_BPS];
    int8_t lfo_target;
} operator_parameters_t;

typedef struct  {
    uint8_t algo;
    float feedback;
    float pitch_rate[NUM_ALGO_BPS];
    uint16_t pitch_time[NUM_ALGO_BPS];
    float lfo_freq;
    int8_t lfo_wave;
    float lfo_amp_amp;
    float lfo_pitch_amp;
    operator_parameters_t ops[MAX_ALGO_OPS];
} algorithms_parameters_t;



#include "fm.h"
// Thank you MFSA for the DX7 op structure , borrowed here \/ \/ \/ 
enum FmOperatorFlags {
    OUT_BUS_ONE = 1 << 0,
    OUT_BUS_TWO = 1 << 1,
    OUT_BUS_ADD = 1 << 2,
    // there is no 1 << 3
    IN_BUS_ONE = 1 << 4,
    IN_BUS_TWO = 1 << 5,
    FB_IN = 1 << 6,
    FB_OUT = 1 << 7
};
struct FmAlgorithm { uint8_t ops[MAX_ALGO_OPS]; };
struct FmAlgorithm algorithms[33] = {
    // 6     5     4     3     2      1   
    { { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 } }, // 0 
    { { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 } }, // 1
    { { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 } }, // 2
    { { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 } }, // 3
    { { 0x41, 0x11, 0x94, 0x01, 0x11, 0x14 } }, // 4
    { { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 } }, // 5
    { { 0x41, 0x94, 0x01, 0x14, 0x01, 0x14 } }, // 6
    { { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 } }, // 7
    { { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 } }, // 8
    { { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 } }, // 9
    { { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 } }, // 10
    { { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 } }, // 11
    { { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 } }, // 12
    { { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 } }, // 13
    { { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 } }, // 14
    { { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 } }, // 15
    { { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 } }, // 16
    { { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 } }, // 17
    { { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 } }, // 18
    { { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 } }, // 19
    { { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 } }, // 20
    { { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 } }, // 21
    { { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 } }, // 22
    { { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 } }, // 23
    { { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 } }, // 24
    { { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 } }, // 25
    { { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 } }, // 26
    { { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 } }, // 27
    { { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 } }, // 28
    { { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 } }, // 29
    { { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 } }, // 30
    { { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 } }, // 31
    { { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 } }, // 32
};
// End of MSFA stuff

// a = 0
void zero(SAMPLE* a) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        a[i] = 0;
    }
}


// b = a + b
void add(SAMPLE* a, SAMPLE* b) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        b[i] += a[i];
    }
}

// b = a 
void copy(SAMPLE* a, SAMPLE* b) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        b[i] = a[i];
    }
}

void render_mod(SAMPLE *in, SAMPLE* out, uint8_t osc, SAMPLE feedback_level, uint8_t algo_osc) {

    hold_and_modify(osc);
    //printf("render_mod: osc %d msynth.amp %f\n", osc, S2F(msynth[osc].amp));

    // out = buf
    // in = mod
    // so render_mod is mod, buf (out)
    if(synth[osc].wave == SINE) render_fm_sine(out, osc, in, feedback_level, algo_osc);
}

void note_on_mod(uint8_t osc, uint8_t algo_osc) {
    synth[osc].note_on_clock = total_samples;
    synth[osc].status = IS_ALGO_SOURCE; // to ensure it's rendered
    if(synth[osc].wave==SINE) fm_sine_note_on(osc, algo_osc);
}

void algo_note_off(uint8_t osc) {
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        if(synth[osc].algo_source[i] >=0 ) {
            uint8_t o = synth[osc].algo_source[i];
            synth[o].note_on_clock = -1;
            synth[o].note_off_clock = total_samples;
        }
    }
    // osc note off, start release
    synth[osc].note_on_clock = -1;
    synth[osc].note_off_clock = total_samples;          
}

void algo_custom_setup_patch(uint8_t osc, uint8_t * target_oscs) {
    // Set up the voices from a DX7 patch.
    // 9 voices total - operators 1,2,3,4,5,6, the root voice (silent), and two LFOs (amp then pitch)
    // osc == root osc (the control one with the parameters in it)
    // target_oscs = list of 8 osc numbers
    // target_oscs[0] = op6, [1] = op5, [2] = op4, [3] = op3, [4] = op2, [5] = op1, 
    // [6] = amp lfo, [7] = pitch lfo
    algorithms_parameters_t p = fm_patches[synth[osc].patch % ALGO_PATCHES];
    synth[osc].algorithm = p.algo;
    synth[osc].feedback = F2S(p.feedback);

    synth[osc].mod_source = target_oscs[7];
    synth[osc].mod_target = TARGET_FREQ;
    float time_ratio = 1;
    if(synth[osc].ratio >=0 ) time_ratio = synth[osc].ratio;

    // amp LFO
    synth[target_oscs[6]].freq = p.lfo_freq * time_ratio;
    synth[target_oscs[6]].wave = p.lfo_wave;
    synth[target_oscs[6]].status = IS_MOD_SOURCE;
    synth[target_oscs[6]].amp = F2S(p.lfo_amp_amp);
    // pitch LFO
    synth[target_oscs[7]].freq = p.lfo_freq * time_ratio;
    synth[target_oscs[7]].wave = p.lfo_wave;
    synth[target_oscs[7]].status = IS_MOD_SOURCE;
    synth[target_oscs[7]].amp = F2S(p.lfo_pitch_amp);


    float last_release_time= 0;
    float last_release_value = 0;
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        // TODO: ADD PER-OP AMP MOD via MOD SENS (SEE FM.PY)
        synth[osc].algo_source[i] = target_oscs[i];
        operator_parameters_t op = p.ops[i];
        synth[target_oscs[i]].freq = op.freq;
        if(synth[target_oscs[i]].freq < 0) synth[target_oscs[i]].freq = 0;
        synth[target_oscs[i]].status = IS_ALGO_SOURCE;
        synth[target_oscs[i]].ratio = op.freq_ratio;
        synth[target_oscs[i]].amp = F2S(op.amp);
        synth[target_oscs[i]].breakpoint_target[0] = TARGET_AMP+TARGET_DX7_EXPONENTIAL;
        synth[target_oscs[i]].phase = 0.25;
        synth[target_oscs[i]].mod_target = op.lfo_target;
        for(uint8_t j=0;j<NUM_ALGO_BPS;j++) {
            synth[target_oscs[i]].breakpoint_values[0][j] = op.amp_rate[j];
            synth[target_oscs[i]].breakpoint_times[0][j] =  ms_to_samples((int)((float)op.amp_time[j]/time_ratio));
        }
        // Calculate the last release time for the root note's amp BP
        if(op.amp_time[4] > last_release_time) {
            last_release_time = op.amp_time[4];
            last_release_value = op.amp_rate[4];
        }
    }

    // Set an overarching amp target for the root note that is the latest operator amp, so the note dies eventually
    synth[osc].breakpoint_times[0][0] = ms_to_samples((int)((float)0/time_ratio));
    synth[osc].breakpoint_values[0][0] = 1;
    synth[osc].breakpoint_times[0][1] = ms_to_samples((int)((float)last_release_time/time_ratio));    
    synth[osc].breakpoint_values[0][1] = last_release_value;
    synth[osc].breakpoint_target[0] = TARGET_AMP+TARGET_DX7_EXPONENTIAL;

    // And the pitch BP for the root note
    for(uint8_t i=0;i<NUM_ALGO_BPS;i++) {
        synth[osc].breakpoint_values[1][i] = p.pitch_rate[i];
        synth[osc].breakpoint_times[1][i] = ms_to_samples((int)((float)p.pitch_time[i]/time_ratio));
    }
    synth[osc].breakpoint_target[1] = TARGET_FREQ+TARGET_TRUE_EXPONENTIAL;

}

// The default way is to use consecutive osc #s
void algo_setup_patch(uint8_t osc) {
    uint8_t target_oscs[8];
    for(uint8_t i=0;i<8;i++) target_oscs[i] = osc+i+1;
    algo_custom_setup_patch(osc, target_oscs);
}

void algo_note_on(uint8_t osc) {    
    // trigger all the source operator voices
    if(synth[osc].patch >= 0) { 
        algo_setup_patch(osc);
    }
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        if(synth[osc].algo_source[i] >=0 ) {
            note_on_mod(synth[osc].algo_source[i], osc);
        }
    }            
}

void algo_init() {
}




void render_algo(SAMPLE* buf, uint8_t osc) { 
    SAMPLE scratch[6][BLOCK_SIZE];

    struct FmAlgorithm algo = algorithms[synth[osc].algorithm];

    // starts at op 6
    SAMPLE* in_buf;
    SAMPLE* out_buf = NULL;

    // TODO, i think i need at most 2 of these buffers, maybe 3?? 
    for (int i = 0; i < 6; ++i)
        zero(scratch[i]);
    uint8_t ops_used = 0;
    for(uint8_t op=0;op<MAX_ALGO_OPS;op++) {
        if(synth[osc].algo_source[op] >=0 && synth[synth[osc].algo_source[op]].status == IS_ALGO_SOURCE) {
            ops_used++;
            SAMPLE feedback_level = 0;
            if(algo.ops[op] & FB_IN) { 
                feedback_level = synth[osc].feedback; 
            } // main algo voice stores feedback, not the op 
            
            if(algo.ops[op] & IN_BUS_ONE) { 
                in_buf = scratch[0]; 
            } else if(algo.ops[op] & IN_BUS_TWO) { 
                in_buf = scratch[1]; 
            } else {
                // no in_buf
                in_buf = scratch[5]; // NULL;
            }

            if(!(algo.ops[op] & OUT_BUS_ADD)) {
                if(algo.ops[op] & OUT_BUS_ONE) { 
                    zero(scratch[2]);
                    out_buf = scratch[2]; 
                }
                if(algo.ops[op] & OUT_BUS_TWO) { 
                    zero(scratch[3]);
                    out_buf = scratch[3]; 
                }
            } else {
                zero(scratch[4]);
                out_buf = scratch[4]; 
            }

            render_mod(in_buf, out_buf, synth[osc].algo_source[op], feedback_level, osc);
            if(!(algo.ops[op] & OUT_BUS_ADD)) {
                if((algo.ops[op] & OUT_BUS_ONE) ) {
                    copy(out_buf, scratch[0]);
                }

                if((algo.ops[op] & OUT_BUS_TWO) ) {
                    copy(out_buf, scratch[1]);
                }
            } else {
                if(algo.ops[op] & OUT_BUS_ONE) {
                    add(scratch[4], scratch[0]);
                } else if(algo.ops[op] & OUT_BUS_TWO) {
                    add(scratch[4], scratch[1]); 
                } else {
                    add(scratch[4], buf);
                }
            }
        }
    }
    // TODO, i need to figure out what happens on note offs for algo_sources.. they should still render..
    if(ops_used == 0) ops_used = 1;
    SAMPLE amp = MUL0_SS(msynth[osc].amp, F2S(1.0f / (float)ops_used));
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        buf[i] = MUL0_SS(buf[i], amp);
    }
}
