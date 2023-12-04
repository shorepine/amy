#include "amy.h"

#if (AMY_HAS_CHORUS==1) || (AMY_HAS_REVERB == 1)

#include "delay.h"

int is_power_of_two(int val) {
    // Returns log_2(val) if val == 2**n, else -1.
    int log_2_val = 0;
    while(val) {
        if (val == 1) return log_2_val;
        if (val & 1) return -1;
        val >>= 1;
        ++log_2_val;
    }
    return -1;  // zero is not a power of 2.
}


delay_line_t *new_delay_line(int len, int fixed_delay, int ram_type) {
    // Check that len is a power of 2.
    //printf("new_delay_line: len %d fixed_del %d\n", len, fixed_delay);
    int log_2_len = is_power_of_two(len);
    if (log_2_len < 0) {
        fprintf(stderr, "delay line len must be power of 2, not %d\n", len);
        abort();
    }
    delay_line_t *delay_line = (delay_line_t*)malloc_caps(sizeof(delay_line_t), ram_type); 
    delay_line->samples = (SAMPLE*)malloc_caps(len * sizeof(SAMPLE), ram_type);
    delay_line->len = len;
    delay_line->log_2_len = log_2_len;
    delay_line->fixed_delay = fixed_delay;
    delay_line->next_in = 0;
    for (int i = 0; i < len; ++i) {
        delay_line->samples[i] = 0;
    }
    return delay_line;
}

void free_delay_line(delay_line_t *delay_line) {
    free(delay_line->samples);
    free(delay_line);
}

static SAMPLE FRACTIONAL_SAMPLE(PHASOR phase, const SAMPLE *delay, int index_mask, int index_bits) {
    // Interpolated sample copied from oscillators.c:render_lut
    uint32_t base_index = INT_OF_P(phase, index_bits);
    SAMPLE frac = S_FRAC_OF_P(phase, index_bits);
    SAMPLE b = delay[base_index];
    SAMPLE c = delay[(base_index + 1) & index_mask];
    // linear interpolation.
    SAMPLE sample = b + MUL4_SS((c - b), frac);
    return sample;
}

void delay_line_in_out(SAMPLE *in, SAMPLE *out, int n_samples, SAMPLE* mod_in, SAMPLE mod_scale, delay_line_t *delay_line, SAMPLE mix_level, SAMPLE feedback_level) {
    // Read and write the next n_samples from/to the delay line.
    // mod_in is a per-sample modulation of the maximum delay, where 1 gives 
    // the max delay, -1 gives no delay, and 0 gives max_delay/2.
    // mod_scale is a constant scale factor applied to each value in mod_in, 
    // used e.g. to flip the sign of the delay.
    // Also supports input feedback from a non-modulated feedback delay output.
    int delay_len = delay_line->len;
    int index_mask = delay_len - 1; // will be all 1s because len is guaranteed 2**n.
    int index_bits = delay_line->log_2_len;

    int index_in = delay_line->next_in;
    int index_feedback = (index_in - delay_line->fixed_delay) & index_mask;

    SAMPLE *delay = delay_line->samples;
    SAMPLE half_mod_scale = mod_scale >> 1;
    while(n_samples-- > 0) {
        SAMPLE next_in = *in++ + MUL4_SS(feedback_level,
                                         delay[index_feedback++]);
        index_feedback &= index_mask;

        PHASOR phase_out = I2P(index_in, index_bits)
            - S2P(F2S(0.5) + MUL4_SS(half_mod_scale, *mod_in++));
        //if(index_out >= delay_len) index_out -= delay_len;
        //if(index_out < 0) index_out += delay_len;
        SAMPLE sample = FRACTIONAL_SAMPLE(phase_out, delay, index_mask, index_bits);
        *out++ += MUL4_SS(mix_level, sample);  // mix delayed + original.
        delay[index_in++] = next_in;
        index_in &= index_mask;
    }
    delay_line->next_in = index_in;
}

void delay_line_in_out_fixed_delay(SAMPLE *in, SAMPLE *out, int n_samples, SAMPLE mod_val, delay_line_t *delay_line, SAMPLE mix_level) {
    // Read and write the next n_samples from/to the delay line.
    // Simplified version of delay_line_in_out() that uses a single, fixed (but fractional)
    // mod_val for the whole block.
    int delay_len = delay_line->len;
    int index_mask = delay_len - 1; // will be all 1s because len is guaranteed 2**n.
    int index_bits = delay_line->log_2_len;

    int index_in = delay_line->next_in;

    SAMPLE *delay = delay_line->samples;
    PHASOR delay_phase = S2P((F2S(1.0f) + mod_val) >> 1);
    while(n_samples-- > 0) {
        SAMPLE next_in = *in++;

        PHASOR phase_out = I2P(index_in, index_bits) - delay_phase;
        SAMPLE sample = FRACTIONAL_SAMPLE(phase_out, delay, index_mask, index_bits);
        *out++ += MUL4_SS(mix_level, sample);  // mix delayed + original.
        delay[index_in++] = next_in;
        index_in &= index_mask;
    }
    delay_line->next_in = index_in;
}


void apply_variable_delay(SAMPLE *block, delay_line_t *delay_line, SAMPLE *delay_mod, SAMPLE delay_scale, SAMPLE mix_level, SAMPLE feedback_level) {
    delay_line_in_out(block, block, AMY_BLOCK_SIZE, delay_mod, delay_scale, delay_line, mix_level, feedback_level);
}

void apply_fixed_delay(SAMPLE *block, delay_line_t *delay_line, SAMPLE delay_mod_val, SAMPLE mix_level) {
    delay_line_in_out_fixed_delay(block, block, AMY_BLOCK_SIZE, delay_mod_val, delay_line, mix_level);
}


static inline SAMPLE DEL_OUT(delay_line_t *delay_line) {
    int out_index =
        (delay_line->next_in - delay_line->fixed_delay) & (delay_line->len - 1);
    return delay_line->samples[out_index];
}

static inline void DEL_IN(delay_line_t *delay_line, SAMPLE val) {
    delay_line->samples[delay_line->next_in++] = val;
    delay_line->next_in &= (delay_line->len - 1);
}

static inline SAMPLE LPF(SAMPLE samp, SAMPLE state, SAMPLE lpcoef, SAMPLE lpgain, SAMPLE gain) {
    // 1-pole lowpass filter (exponential smoothing).
    // Smoothing. lpcoef=1 => no smoothing; lpcoef=0.001 => much smoothing.
    state += MUL0_SS(lpcoef, samp - state);
    // Cross-fade between smoothed and original.  lpgain=0 => all smoothed, 1 => all dry.
    return MUL0_SS(gain >> 1, state + MUL0_SS(lpgain, samp - state));
}

SAMPLE f1state = 0, f2state = 0, f3state = 0, f4state = 0;

delay_line_t *delay_1 = NULL, *delay_2 = NULL, *delay_3 = NULL,
    *delay_4 = NULL;
delay_line_t *ref_1 = NULL, *ref_2 = NULL, *ref_3= NULL,
    *ref_4 = NULL, *ref_5 = NULL, *ref_6 = NULL;

#define INITIAL_XOVER_HZ 3000.0
#define INITIAL_LIVENESS 0.85
#define INITIAL_DAMPING 0.5

SAMPLE lpfcoef;
SAMPLE lpfgain;
SAMPLE liveness;

void config_stereo_reverb(float a_liveness, float crossover_hz, float damping) {
    //printf("config_stereo_reverb: liveness %f xover %f damping %f\n",
    //       a_liveness, crossover_hz, damping);
    // liveness (0..1) controls how much energy is preserved (larger = longer reverb).
    liveness = F2S(a_liveness);
    // crossover_hz is 3dB point of 1-pole lowpass freq.
    lpfcoef = F2S(6.2832f * crossover_hz / AMY_SAMPLE_RATE);
    if (lpfcoef > S2F(1.f))  lpfcoef = S2F(1.f);
    if (lpfcoef < 0)  lpfcoef = 0;
    lpfgain = S2F(1.f - damping);
}

// Delay 1 is 58.6435 ms
#define DELAY1SAMPS 2586
// Delay 2 is 69.4325 ms
#define DELAY2SAMPS 3062
// Delay 3 is 74.5234 ms
#define DELAY3SAMPS 3286
// Delay 4 is 86.1244 ms
#define DELAY4SAMPS 3798

// Power of 2 that encloses all the delays.
#define DELAY_POW2 4096

// Early reflections delays
#define REF1SAMPS 3319  // 75.2546 ms
#define REF2SAMPS 1920  // 43.5337 ms
#define REF3SAMPS 1138  // 25.796 ms
#define REF4SAMPS 855   // 19.392 ms
#define REF5SAMPS 722   // 16.364 ms
#define REF6SAMPS 602   // 13.645 ms


void init_stereo_reverb(void) {
    if (delay_1 == NULL) {
        delay_1 = new_delay_line(DELAY_POW2, DELAY1SAMPS, REVERB_RAM_CAPS);
        delay_2 = new_delay_line(DELAY_POW2, DELAY2SAMPS, REVERB_RAM_CAPS);
        delay_3 = new_delay_line(DELAY_POW2, DELAY3SAMPS, REVERB_RAM_CAPS);
        delay_4 = new_delay_line(DELAY_POW2, DELAY4SAMPS, REVERB_RAM_CAPS);

        ref_1 = new_delay_line(4096, REF1SAMPS, REVERB_RAM_CAPS);
        ref_2 = new_delay_line(2048, REF2SAMPS, REVERB_RAM_CAPS);
        ref_3 = new_delay_line(2048, REF3SAMPS, REVERB_RAM_CAPS);
        ref_4 = new_delay_line(1024, REF4SAMPS, REVERB_RAM_CAPS);
        ref_5 = new_delay_line(1024, REF5SAMPS, REVERB_RAM_CAPS);
        ref_6 = new_delay_line(1024, REF6SAMPS, REVERB_RAM_CAPS);
        
        config_stereo_reverb(INITIAL_LIVENESS, INITIAL_XOVER_HZ, INITIAL_DAMPING);
    }
}

void stereo_reverb(SAMPLE *r_in, SAMPLE *l_in, SAMPLE *r_out, SAMPLE *l_out, int n_samples, SAMPLE level) {
    // Stereo reverb.  *{r,l}_in each point to n_samples input samples.
    // n_samples are written to {r,l}_out.
    // Recreate
    // https://github.com/duvtedudug/Pure-Data/blob/master/extra/rev2%7E.pd
    // an instance of the Stautner-Puckette multichannel reverberator from
    // https://www.ee.columbia.edu/~dpwe/e4896/papers/StautP82-reverb.pdf
    while(n_samples--) {
        // Early echo reflections.
        SAMPLE in_r = *r_in++;
        SAMPLE in_l;
        if (l_in)   in_l = *l_in++;
        else   in_l = in_r;
        SAMPLE r_acc, l_acc;
        r_acc = MUL0_SS(F2S(0.0625f), in_r);
        l_acc = MUL0_SS(F2S(0.0625f), in_l);

        DEL_IN(ref_1, l_acc);
        SAMPLE d_out = DEL_OUT(ref_1);
        l_acc = r_acc - d_out;
        r_acc += d_out;

        DEL_IN(ref_2, l_acc);
        d_out = DEL_OUT(ref_2);
        l_acc = r_acc - d_out;
        r_acc += d_out;

        DEL_IN(ref_3, l_acc);
        d_out = DEL_OUT(ref_3);
        l_acc = r_acc - d_out;
        r_acc += d_out;

        DEL_IN(ref_4, l_acc);
        d_out = DEL_OUT(ref_4);
        l_acc = r_acc - d_out;
        r_acc += d_out;

        DEL_IN(ref_5, l_acc);
        d_out = DEL_OUT(ref_5);
        l_acc = r_acc - d_out;
        r_acc += d_out;

        DEL_IN(ref_6, l_acc);
        l_acc = DEL_OUT(ref_6);
        
        
        // Reverb delays & matrix.
        SAMPLE d1 = DEL_OUT(delay_1);
        d1 = LPF(d1, f1state, lpfcoef, lpfgain, liveness);
        d1 += r_acc;
        *r_out++ = in_r + MUL4_SS(level, d1);

        SAMPLE d2 = DEL_OUT(delay_2);
        d2 = LPF(d2, f2state, lpfcoef, lpfgain, liveness);
        d2 += l_acc;
        if (l_out != NULL)  *l_out++ = in_l + MUL4_SS(level, d2);

        SAMPLE d3 = DEL_OUT(delay_3);
        d3 = LPF(d3, f3state, lpfcoef, lpfgain, liveness);

        SAMPLE d4 = DEL_OUT(delay_4);
        d4 = LPF(d4, f3state, lpfcoef, lpfgain, liveness);

        // Mixing and feedback.
        DEL_IN(delay_1, d1 + d2 + d3 + d4);
        DEL_IN(delay_2, d1 - d2 + d3 - d4);
        DEL_IN(delay_3, d1 + d2 - d3 - d4);
        DEL_IN(delay_4, d1 - d2 - d3 + d4);
    }
}

#endif
