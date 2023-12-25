#include "amy.h"
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

// Filters tend to get weird under this ratio -- this corresponds to 4.4Hz 
#define LOWEST_RATIO 0.0001

#define FILT_NUM_DELAYS  4    // Need 4 memories for DFI filters, if used (only 2 for DFII).

SAMPLE coeffs[AMY_OSCS][5];
SAMPLE filter_delay[AMY_OSCS][FILT_NUM_DELAYS];

SAMPLE eq_coeffs[3][5];
SAMPLE eq_delay[AMY_NCHANS][3][FILT_NUM_DELAYS];


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

    // Where exactly are those poles?  Impose minima on (1 - r) and w0.
    float r = MIN(0.99, sqrtf(a2 / a0));
    w0 = MAX(0.01, w0);
    a1 = a0 * (-2 * r * cosf(w0));
    a2 = a0 * r * r;

    coeffs[0] = F2S(b0 / a0);
    coeffs[1] = F2S(b1 / a0);
    coeffs[2] = F2S(b2 / a0);
    coeffs[3] = F2S(a1 / a0);
    coeffs[4] = F2S(a2 / a0);

    //printf("Flpf t=%f f=%f q=%f b0 %f b1 %f b2 %f a1 %f a2 %f r %f theta %f\n", total_samples / (float)AMY_SAMPLE_RATE, f * AMY_SAMPLE_RATE, qFactor,
    //       b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0, r, w0);
    //printf("Slpf t=%f f=%f q=%f b0 %f b1 %f b2 %f a1 %f a2 %f\n", total_samples / (float)AMY_SAMPLE_RATE, f * AMY_SAMPLE_RATE, qFactor,
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

#define FILT_MUL_SS MUL8F_SS
//#define FILT_MUL_SS MUL8_SS  // Goes unstable for TestFilter
#define FILTER_SCALEUP_BITS 2  // Apply this gain to input before filtering to avoid underflow in intermediate value.  Reduces peak sample value to 64, not 256.

int8_t dsps_biquad_f32_ansi(const SAMPLE *input, SAMPLE *output, int len, SAMPLE *coef, SAMPLE *w) {
    // Zeros then poles - Direct Form I
    // We need 2 memories for input, and 2 for output.
    SAMPLE x1 = w[0];
    SAMPLE x2 = w[1];
    SAMPLE y1 = w[2];
    SAMPLE y2 = w[3];
    for (int i = 0 ; i < len ; i++) {
        SAMPLE x0 = SHIFTL(input[i], FILTER_SCALEUP_BITS);
        SAMPLE w0 = FILT_MUL_SS(coef[0], x0) + FILT_MUL_SS(coef[1], x1) + FILT_MUL_SS(coef[2], x2);
        SAMPLE y0 = w0 - FILT_MUL_SS(coef[3], y1) - FILT_MUL_SS(coef[4], y2);
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
        output[i] = SHIFTR(y0, FILTER_SCALEUP_BITS);
    }
    w[0] = x1;
    w[1] = x2;
    w[2] = y1;
    w[3] = y2;
    return 0;
}

int8_t dsps_biquad_f32_ansi_commuted(const SAMPLE *input, SAMPLE *output, int len, SAMPLE *coef, SAMPLE *w) {
    // poles before zeros, for Direct Form II
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
    filter_delay[osc][0] = 0; filter_delay[osc][1] = 0;
}

void filters_init() {
    // update the parametric filters 
    dsps_biquad_gen_lpf_f32(eq_coeffs[0], EQ_CENTER_LOW /(float)AMY_SAMPLE_RATE, 0.707);
    dsps_biquad_gen_bpf_f32(eq_coeffs[1], EQ_CENTER_MED /(float)AMY_SAMPLE_RATE, 1.000);
    dsps_biquad_gen_hpf_f32(eq_coeffs[2], EQ_CENTER_HIGH/(float)AMY_SAMPLE_RATE, 0.707);
    for(uint16_t i=0;i<AMY_OSCS;i++) { filter_delay[i][0] = 0; filter_delay[i][1] = 0; }
    for (int c = 0; c < AMY_NCHANS; ++c) {
        for (int b = 0; b < 3; ++b) {
            for (int d = 0; d < 2; ++d) {
                eq_delay[c][b][d] = 0;
            }
        }
    }
}


void parametric_eq_process(SAMPLE *block) {
    SAMPLE output[2][AMY_BLOCK_SIZE];
    for(int c = 0; c < AMY_NCHANS; ++c) {
        SAMPLE *cblock = block + c * AMY_BLOCK_SIZE;
        dsps_biquad_f32_ansi(cblock, output[0], AMY_BLOCK_SIZE, eq_coeffs[0], eq_delay[c][0]);
        dsps_biquad_f32_ansi(cblock, output[1], AMY_BLOCK_SIZE, eq_coeffs[1], eq_delay[c][1]);
        for(int i = 0; i < AMY_BLOCK_SIZE; ++i)
            output[0][i] = FILT_MUL_SS(output[0][i], global.eq[0]) - FILT_MUL_SS(output[1][i], global.eq[1]);
        dsps_biquad_f32_ansi(cblock, output[1], AMY_BLOCK_SIZE, eq_coeffs[2], eq_delay[c][2]);
        for(int i = 0; i < AMY_BLOCK_SIZE; ++i)
            cblock[i] = output[0][i] + FILT_MUL_SS(output[1][i], global.eq[2]);
    }
}


void hpf_buf(SAMPLE *buf, SAMPLE *state) {
    // Implement a dc-blocking HPF with a pole at (decay + 0j) and a zero at (1 + 0j).
    SAMPLE pole = F2S(0.99);
    SAMPLE xn1 = state[0];
    SAMPLE yn1 = state[1];
    for (uint16_t i = 0; i < AMY_BLOCK_SIZE; ++i) {
        SAMPLE w = buf[i] - xn1;
        xn1 = buf[i];
        buf[i] = w + MUL4_SS(pole, yn1);
        yn1 = buf[i];
    }
    state[0] = xn1;
    state[1] = yn1;
}

void filter_process(SAMPLE * block, uint16_t osc) {
    float ratio = freq_of_logfreq(msynth[osc].filter_logfreq)/(float)AMY_SAMPLE_RATE;
    if(ratio < LOWEST_RATIO) ratio = LOWEST_RATIO;
    if(synth[osc].filter_type==FILTER_LPF)
        dsps_biquad_gen_lpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    if(synth[osc].filter_type==FILTER_BPF)
        dsps_biquad_gen_bpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    if(synth[osc].filter_type==FILTER_HPF)
        dsps_biquad_gen_hpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    dsps_biquad_f32_ansi(block, block, AMY_BLOCK_SIZE, coeffs[osc], filter_delay[osc]);
    //dsps_biquad_f32_ansi_commuted(block, block, AMY_BLOCK_SIZE, coeffs[osc], filter_delay[osc]);
    // Final high-pass to remove residual DC offset from sub-fundamental LPF.
    hpf_buf(block, &synth[osc].hpf_state[0]);
}

void filters_deinit() {
}

