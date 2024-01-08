// algorithms.c
// FM and partial synths that involve combinations of oscillators
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

// We never see 0x?6 (add to bus two)
// There are just instances of 0x?2 (write to bus two) and they are both 0x02
// We never see input BUS_ONE output ADD BUS_ONE

// algo   feedback  input    output
// 0x01             -            BUS_ONE
// 0x41   IN        -            BUS_ONE
// 0xc1   IN OUT    -            BUS_ONE
// 0x11             BUS_ONE      BUS_ONE  // This is the only case when we can't directly accumulate the (possibly zero'd) output because it's the input
// 0x05             -        ADD BUS_ONE
// 0xc5   IN OUT    -        ADD BUS_ONE
// 0x25             BUS_TWO  ADD BUS_ONE

// 0x02             -            BUS_TWO

// 0x04             -        ADD op
// 0x14             BUS_ONE  ADD op
// 0x94      OUT    BUS_ONE  ADD op
// 0xC4   IN OUT    BUS_ONE  ADD op

struct FmAlgorithm { uint8_t ops[MAX_ALGO_OPS]; };
const struct FmAlgorithm algorithms[33] = {
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
static inline void zero(SAMPLE* a) {
    bzero((void *)a, AMY_BLOCK_SIZE * sizeof(SAMPLE));
    //for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) {
    //    a[i] = 0;
    //}
}


// b = a 
static inline void copy(SAMPLE* a, SAMPLE* b) {
    bcopy((void *)a, (void *)b, AMY_BLOCK_SIZE * sizeof(SAMPLE));
    //for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) {
    //    b[i] = a[i];
    //}
}

void render_mod(SAMPLE *in, SAMPLE* out, uint16_t osc, SAMPLE feedback_level, uint16_t algo_osc, SAMPLE amp) {

    hold_and_modify(osc);
    //printf("render_mod: osc %d msynth.amp %f\n", osc, msynth[osc].amp);

    // out = buf
    // in = mod
    // so render_mod is mod, buf (out)
    if(synth[osc].wave == SINE) render_fm_sine(out, osc, in, feedback_level, algo_osc, amp);
}

void note_on_mod(uint16_t osc, uint16_t algo_osc) {
    synth[osc].note_on_clock = total_samples;
    synth[osc].status = IS_ALGO_SOURCE; // to ensure it's rendered
    if(synth[osc].wave==SINE) fm_sine_note_on(osc, algo_osc);
}

void algo_note_off(uint16_t osc) {
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        if(AMY_IS_SET(synth[osc].algo_source[i])) {
            uint16_t o = synth[osc].algo_source[i];
            AMY_UNSET(synth[o].note_on_clock);
            synth[o].note_off_clock = total_samples;
        }
    }
    // osc note off, start release
    AMY_UNSET(synth[osc].note_on_clock);
    synth[osc].note_off_clock = total_samples;          
}

void algo_custom_setup_patch(uint16_t osc, uint16_t * target_oscs) {
    // Set up the voices from a DX7 patch.
    // 9 voices total - operators 1,2,3,4,5,6, the root voice (silent), and two LFOs (amp then pitch)
    // osc == root osc (the control one with the parameters in it)
    // target_oscs = list of 8 osc numbers
    // target_oscs[0] = op6, [1] = op5, [2] = op4, [3] = op3, [4] = op2, [5] = op1, 
    // [6] = amp lfo, [7] = pitch lfo
    algorithms_parameters_t p = fm_patches[synth[osc].patch % ALGO_PATCHES];
    synth[osc].algorithm = p.algo;
    synth[osc].feedback = p.feedback;

    synth[osc].mod_source = target_oscs[7];
    synth[osc].mod_target = TARGET_FREQ;
    float time_ratio = 1;
    if(AMY_IS_SET(synth[osc].logratio)) time_ratio = exp2f(synth[osc].logratio);

    // amp LFO
    //synth[target_oscs[6]].freq = p.lfo_freq * time_ratio;
    synth[target_oscs[6]].logfreq_coefs[COEF_CONST] = logfreq_of_freq(p.lfo_freq * time_ratio);
    synth[target_oscs[6]].logfreq_coefs[COEF_NOTE] = 0;
    synth[target_oscs[6]].wave = p.lfo_wave;
    synth[target_oscs[6]].status = IS_MOD_SOURCE;
    synth[target_oscs[6]].amp_coefs[COEF_CONST] = p.lfo_amp_amp;
    synth[target_oscs[6]].amp_coefs[COEF_VEL] = 0;
    synth[target_oscs[6]].amp_coefs[COEF_EG0] = 0;

    // pitch LFO
    //synth[target_oscs[7]].freq = p.lfo_freq * time_ratio;
    synth[target_oscs[7]].logfreq_coefs[COEF_CONST] = logfreq_of_freq(p.lfo_freq * time_ratio);
    synth[target_oscs[6]].logfreq_coefs[COEF_NOTE] = 0;
    synth[target_oscs[7]].wave = p.lfo_wave;
    synth[target_oscs[7]].status = IS_MOD_SOURCE;
    synth[target_oscs[7]].amp_coefs[COEF_CONST] = p.lfo_pitch_amp;
    synth[target_oscs[6]].amp_coefs[COEF_VEL] = 0;
    synth[target_oscs[6]].amp_coefs[COEF_EG0] = 0;


    float last_release_time= 0;
    float last_release_value = 0;
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        // TODO: ADD PER-OP AMP MOD via MOD SENS (SEE FM.PY)
        synth[osc].algo_source[i] = target_oscs[i];
        operator_parameters_t op = p.ops[i];
        if (op.freq <= 0) {
            //synth[target_oscs[i]].freq = 0;
            synth[target_oscs[i]].logfreq_coefs[COEF_CONST] = 0;
        } else {
            //synth[target_oscs[i]].freq = op.freq;
            synth[target_oscs[i]].logfreq_coefs[COEF_CONST] = logfreq_of_freq(op.freq);
        }
        synth[target_oscs[i]].logfreq_coefs[COEF_NOTE] = 0;
        synth[target_oscs[i]].status = IS_ALGO_SOURCE;
        synth[target_oscs[i]].logratio = log2f(op.freq_ratio);
        synth[target_oscs[i]].amp_coefs[COEF_CONST] = op.amp;
        synth[target_oscs[i]].breakpoint_target[0] = TARGET_AMP + TARGET_DX7_EXPONENTIAL;
        synth[target_oscs[i]].amp_coefs[COEF_VEL] = 0;
        synth[target_oscs[i]].amp_coefs[COEF_EG0] = 1.0f;
        synth[target_oscs[i]].phase = F2P(0.25);
        synth[target_oscs[i]].mod_target = op.lfo_target;
        apply_target_to_coefs(target_oscs[i], op.lfo_target, COEF_MOD);
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
    synth[osc].breakpoint_target[0] = TARGET_AMP + TARGET_DX7_EXPONENTIAL;
    synth[osc].amp_coefs[COEF_CONST] = 0;
    synth[osc].amp_coefs[COEF_VEL] = 0;
    synth[osc].amp_coefs[COEF_EG0] = 1.0f;

    // And the pitch BP for the root note
    for(uint8_t i=0;i<NUM_ALGO_BPS;i++) {
        synth[osc].breakpoint_values[1][i] = p.pitch_rate[i];
        synth[osc].breakpoint_times[1][i] = ms_to_samples((int)((float)p.pitch_time[i]/time_ratio));
    }
    synth[osc].breakpoint_target[1] = TARGET_FREQ + TARGET_TRUE_EXPONENTIAL;
    synth[osc].logfreq_coefs[COEF_EG1] = 1.0f;
    synth[osc].logfreq_coefs[COEF_CONST] = -1.0f;
    //synth[osc].logfreq_coefs[COEF_NOTE] = 0;

}

// The default way is to use consecutive osc #s
void algo_setup_patch(uint16_t osc) {
    uint16_t target_oscs[8];
    for(uint8_t i=0;i<8;i++) target_oscs[i] = osc+i+1;
    algo_custom_setup_patch(osc, target_oscs);
}

void algo_note_on(uint16_t osc) {    
    // trigger all the source operator voices
    if(AMY_IS_SET(synth[osc].patch)) { 
        algo_setup_patch(osc);
    }
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        if(AMY_IS_SET(synth[osc].algo_source[i])) {
            note_on_mod(synth[osc].algo_source[i], osc);
        }
    }            
}

SAMPLE *** scratch;

void algo_deinit() {
    for(uint16_t i=0;i<AMY_CORES;i++) {
        for(uint16_t j=0;j<3;j++) free(scratch[i][j]);
        free(scratch[i]);
    }
    free(scratch);
}

void algo_init() {
    scratch = malloc_caps(sizeof(SAMPLE**)*AMY_CORES, FBL_RAM_CAPS);
    for(uint16_t i=0;i<AMY_CORES;i++) {
        scratch[i] = malloc_caps(sizeof(SAMPLE*)*3, FBL_RAM_CAPS);
        for(uint16_t j=0;j<3;j++) {
            scratch[i][j] = malloc_caps(sizeof(SAMPLE)*AMY_BLOCK_SIZE, FBL_RAM_CAPS);
        }
    }

}

void render_algo(SAMPLE* buf, uint16_t osc, uint8_t core) { 
    struct FmAlgorithm algo = algorithms[synth[osc].algorithm];

    // starts at op 6
    SAMPLE* in_buf;
    SAMPLE* out_buf = NULL;

    SAMPLE* const BUS_ONE = scratch[core][0];
    SAMPLE* const BUS_TWO = scratch[core][1];
    SAMPLE* const SCRATCH = scratch[core][2];

    //for (int i = 0; i < 3; ++i)
    //    zero(scratch[core][i]);
    
    SAMPLE amp = SHIFTR(F2S(msynth[osc].amp), 2);  // Arbitrarily divide FM voice output by 4 to make it more in line with other oscs.
    for(uint8_t op=0;op<MAX_ALGO_OPS;op++) {
        if(AMY_IS_SET(synth[osc].algo_source[op]) && synth[synth[osc].algo_source[op]].status == IS_ALGO_SOURCE) {
            SAMPLE feedback_level = 0;
            SAMPLE mod_amp = F2S(1.0f);
            if(algo.ops[op] & FB_IN) { 
                feedback_level = F2S(synth[osc].feedback);
            } // main algo voice stores feedback, not the op 

            if(algo.ops[op] & IN_BUS_ONE) { 
                in_buf = BUS_ONE;
            } else if(algo.ops[op] & IN_BUS_TWO) { 
                in_buf = BUS_TWO;
            } else {
                // no in_buf
                in_buf = NULL;
            }
            if ( (algo.ops[op] & IN_BUS_ONE)
                 && (algo.ops[op] & OUT_BUS_ONE)
                 /* && !(algo.ops[op] & OUT_BUS_ADD) */ ) {  // IN_BUS_ONE + OUT_BUS_ONE is never ADD
                // Input is BUS_ONE and output overwrites it, use a temp buffer.
                out_buf = SCRATCH;
            } else if (algo.ops[op] & OUT_BUS_ONE) {
                out_buf = BUS_ONE;
            } else if (algo.ops[op] & OUT_BUS_TWO) {
                out_buf = BUS_TWO;
            } else {
                out_buf = buf;
                // We apply the mod_amp to every output that goes into the final output buffer.
                mod_amp = amp;
            }
            if (!(algo.ops[op] & OUT_BUS_ADD)) {
                // Output is not being accumulated, so have to clear it first.
                zero(out_buf);
            }

            render_mod(in_buf, out_buf, synth[osc].algo_source[op], feedback_level, osc, mod_amp);

            if (out_buf == SCRATCH) {
                // We had to invoke the spare buffer, meaning we're overwriting BUS_ONE
                copy(out_buf, BUS_ONE);
            }
        }
    }
    // TODO, i need to figure out what happens on note offs for algo_sources.. they should still render..
}
