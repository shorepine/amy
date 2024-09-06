#ifndef _DELAY_H

// How many bits used for fractional part of delay line index.
#define DELAY_INDEX_FRAC_BITS 15
// The number of bits used to hold the delay line index.
#define DELAY_INDEX_BITS (31 - DELAY_INDEX_FRAC_BITS)

typedef struct delay_line {
    SAMPLE *samples;
    int len;
    int log_2_len;
    int fixed_delay;
    int next_in;
} delay_line_t;


delay_line_t *new_delay_line(int len, int fixed_delay, int ram_type /* e.g. MALLOC_CAP_INTERNAL */);
void free_delay_line(delay_line_t *d);

void apply_variable_delay(SAMPLE *block, delay_line_t *delay_line, SAMPLE *delay_samples, SAMPLE mod_scale, SAMPLE mix_level, SAMPLE feedback_level);
void apply_fixed_delay(SAMPLE *block, delay_line_t *delay_line, uint32_t delay_samples, SAMPLE mix_level, SAMPLE feedback, SAMPLE filter_coef);

void config_stereo_reverb(float a_liveness, float crossover_hz, float damping);
void init_stereo_reverb(void);
void stereo_reverb(SAMPLE *r_in, SAMPLE *l_in, SAMPLE *r_out, SAMPLE *l_out, int n_samples, SAMPLE level);

#endif // !_DELAY_H
