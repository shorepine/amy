#include "amy.h"


// Filters tend to get weird under this ratio -- this corresponds to 4.4Hz 
#define LOWEST_RATIO 0.0001

SAMPLE coeffs[AMY_OSCS][5];
SAMPLE delay[AMY_OSCS][2];

SAMPLE eq_coeffs[3][5];
SAMPLE eq_delay[3][2];


float dsps_sqrtf_f32_ansi(float f)
{
    int* f_ptr = (int*)&f;
    const int result = 0x1fbb4000 + (*f_ptr >> 1);
    float* f_result = (float*)&result;
    return *f_result;   
}

int8_t dsps_biquad_gen_lpf_f32(SAMPLE *coeffs, float f, float qFactor)
{
    if (qFactor <= 0.0001) {
        qFactor = 0.0001;
    }
    float Fs = 1;

    float w0 = 2 * M_PI * f / Fs;
    float c = cosf(w0);
    float s = sinf(w0);
    float alpha = s / (2 * qFactor);

    float b0 = (1 - c) / 2;
    float b1 = 1 - c;
    float b2 = b0;
    float a0 = 1 + alpha;
    float a1 = -2 * c;
    float a2 = 1 - alpha;

    coeffs[0] = F2S(b0 / a0);
    coeffs[1] = F2S(b1 / a0);
    coeffs[2] = F2S(b2 / a0);
    coeffs[3] = F2S(a1 / a0);
    coeffs[4] = F2S(a2 / a0);

    //printf("lpf f=%f q=%f b0 %f b1 %f b2 %f a1 %f a2 %f\n", f, qFactor,
    //       S2F(coeffs[0]), S2F(coeffs[1]), S2F(coeffs[2]), S2F(coeffs[3]), S2F(coeffs[4]));

    return 0;
}

int8_t dsps_biquad_gen_hpf_f32(SAMPLE *coeffs, float f, float qFactor)
{
    if (qFactor <= 0.0001) {
        qFactor = 0.0001;
    }
    float Fs = 1;

    float w0 = 2 * M_PI * f / Fs;
    float c = cosf(w0);
    float s = sinf(w0);
    float alpha = s / (2 * qFactor);

    float b0 = (1 + c) / 2;
    float b1 = -(1 + c);
    float b2 = b0;
    float a0 = 1 + alpha;
    float a1 = -2 * c;
    float a2 = 1 - alpha;

    coeffs[0] = F2S(b0 / a0);
    coeffs[1] = F2S(b1 / a0);
    coeffs[2] = F2S(b2 / a0);
    coeffs[3] = F2S(a1 / a0);
    coeffs[4] = F2S(a2 / a0);
    return 0;
}

int8_t dsps_biquad_gen_bpf_f32(SAMPLE *coeffs, float f, float qFactor)
{
    if (qFactor <= 0.0001) {
        qFactor = 0.0001;
    }
    float Fs = 1;

    float w0 = 2 * M_PI * f / Fs;
    float c = cosf(w0);
    float s = sinf(w0);
    float alpha = s / (2 * qFactor);

    float b0 = s / 2;
    float b1 = 0;
    float b2 = -b0;
    float a0 = 1 + alpha;
    float a1 = -2 * c;
    float a2 = 1 - alpha;

    coeffs[0] = F2S(b0 / a0);
    coeffs[1] = F2S(b1 / a0);
    coeffs[2] = F2S(b2 / a0);
    coeffs[3] = F2S(a1 / a0);
    coeffs[4] = F2S(a2 / a0);
    return 0;
}

#define FILT_MUL_SS MUL8_SS

int8_t dsps_biquad_f32_ansi(const SAMPLE *input, SAMPLE *output, int len, SAMPLE *coef, SAMPLE *w) {
    for (int i = 0 ; i < len ; i++) {
        SAMPLE d0 = input[i] - FILT_MUL_SS(coef[3], w[0]) - FILT_MUL_SS(coef[4], w[1]);
        output[i] = FILT_MUL_SS(coef[0], d0) + FILT_MUL_SS(coef[1], w[0]) + FILT_MUL_SS(coef[2], w[1]);
        w[1] = w[0];
        w[0] = d0;
    }
    return 0;
}

void update_filter(uint16_t osc) {
    // reset the delay for a filter
    // normal mod / adsr will just change the coeffs
    delay[osc][0] = 0; delay[osc][1] = 0;
}

void filters_init() {
    // update the parametric filters 
    dsps_biquad_gen_lpf_f32(eq_coeffs[0], EQ_CENTER_LOW /(float)AMY_SAMPLE_RATE, 0.707);
    dsps_biquad_gen_bpf_f32(eq_coeffs[1], EQ_CENTER_MED /(float)AMY_SAMPLE_RATE, 1.000);
    dsps_biquad_gen_hpf_f32(eq_coeffs[2], EQ_CENTER_HIGH/(float)AMY_SAMPLE_RATE, 0.707);
    for(uint16_t i=0;i<AMY_OSCS;i++) { delay[i][0] = 0; delay[i][1] = 0; }
    eq_delay[0][0] = 0; eq_delay[0][1] = 0;
    eq_delay[1][0] = 0; eq_delay[1][1] = 0;
    eq_delay[2][0] = 0; eq_delay[2][1] = 0;
}


void parametric_eq_process(SAMPLE *block) {
    SAMPLE output[3][AMY_BLOCK_SIZE];
    dsps_biquad_f32_ansi(block, output[0], AMY_BLOCK_SIZE, eq_coeffs[0], eq_delay[0]);
    dsps_biquad_f32_ansi(block, output[1], AMY_BLOCK_SIZE, eq_coeffs[1], eq_delay[1]);
    dsps_biquad_f32_ansi(block, output[2], AMY_BLOCK_SIZE, eq_coeffs[2], eq_delay[2]);
    for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++)
        block[i] = (FILT_MUL_SS(output[0][i], global.eq[0])
                    - FILT_MUL_SS(output[1][i], global.eq[1])
                    + FILT_MUL_SS(output[2][i], global.eq[2]));
}


void filter_process(SAMPLE * block, uint16_t osc) {
    SAMPLE output[AMY_BLOCK_SIZE];
    float ratio = msynth[osc].filter_freq/(float)AMY_SAMPLE_RATE;
    if(ratio < LOWEST_RATIO) ratio = LOWEST_RATIO;
    if(synth[osc].filter_type==FILTER_LPF)
        dsps_biquad_gen_lpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    if(synth[osc].filter_type==FILTER_BPF)
        dsps_biquad_gen_bpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    if(synth[osc].filter_type==FILTER_HPF)
        dsps_biquad_gen_hpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    dsps_biquad_f32_ansi(block, output, AMY_BLOCK_SIZE, coeffs[osc], delay[osc]);
    for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) {
        block[i] = output[i];
    }
}

void filters_deinit() {
}

