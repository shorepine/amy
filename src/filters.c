#include "amy.h"
#include "assert.h"

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

// Filters tend to get weird under this ratio -- this corresponds to 4.4Hz 
#define LOWEST_RATIO 0.0001

#define FILT_NUM_DELAYS  4    // Need 4 memories for DFI filters, if used (only 2 for DFII).

SAMPLE ** coeffs;
SAMPLE ** filter_delay;
SAMPLE ** eq_coeffs;
SAMPLE *** eq_delay;



float dsps_sqrtf_f32_ansi(float f)
{
    int* f_ptr = (int*)&f;
    const int result = 0x1fbb4000 + (*f_ptr >> 1);
    float* f_result = (float*)&result;
    return *f_result;  
}

int8_t dsps_biquad_gen_lpf_f32(SAMPLE *coeffs, float f, float qFactor)
{
    //qFactor = sqrtf(qFactor);
    
    if (qFactor < 0.51) {
        qFactor = 0.51;
    }
    if (f > 0.45) {
        f = 0.45;
    }
    float Fs = 1;

    float w0 = 2 * M_PI * f / Fs;
    w0 = MAX(0.01, w0);
    float c = cosf(w0);
    float s = sinf(w0);
    float alpha = s / (2 * qFactor);
    // sin w0 / (2 Q) < 1
    // => sin w0 < 2 Q
    // If Q >= 0.5, no problem.

    
    float b0 = (1 - c) / 2;
    float b1 = 1 - c;
    float b2 = b0;
    float a0 = 1 + alpha;
    float a1 = -2 * c;
    float a2 = 1 - alpha;

    // Where exactly are those poles?  Impose minima on (1 - r) and w0.
    float r = -99, ww = 0;
    if (false && a2 > 0) { 
        printf("before: r %f a %f %f %f\n", sqrtf(a2 / a0), a0, a1, a2);
        // Limit how close complex poles can get to the unit circle.
        r = MIN(0.99, sqrtf(a2 / a0));
        float alphadash = (1 - r * r) / (1 + r * r);
        float cosww = c / sqrtf(1 - alphadash * alphadash);
        if (fabs(cosww) < 1.0) {
            ww = acosf(cosww);
            a1 = a0 * (-2 * r * cosf(ww));
            a2 = a0 * r * r;
            printf(" after: r %f a %f %f %f\n", r, a0, a1, a2);
        }
    }

    coeffs[0] = F2S(-b0 / a0);
    coeffs[1] = F2S(-b1 / a0);
    coeffs[2] = F2S(-b2 / a0);
    coeffs[3] = F2S(a1 / a0);
    coeffs[4] = F2S(a2 / a0);

    //printf("Flpf t=%f f=%f q=%f alpha %f b0 %f b1 %f b2 %f a1 %f a2 %f r %f theta %f\n", total_samples / (float)AMY_SAMPLE_RATE, f * AMY_SAMPLE_RATE, qFactor, alpha, 
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

// Stick to the faster mult for biquad, hpf etc, since the parameters aren't so sensitive, and parametric_eq was chewing major CPU.
#define FILT_MUL_SS(a, b) SMULR6(a, b)

#define FILTER_SCALEUP_BITS 0  // Apply this gain to input before filtering to avoid underflow in intermediate value.
#define FILTER_BIQUAD_SCALEUP_BITS 0  // Apply this gain to input before filtering to avoid underflow in intermediate value.
#define FILTER_BIQUAD_SCALEDOWN_BITS 0  // Extra headroom for EQ filters to avoid clipping on loud signals.

int8_t dsps_biquad_f32_ansi(const SAMPLE *input, SAMPLE *output, int len, SAMPLE *coef, SAMPLE *w) {
    AMY_PROFILE_START(DSPS_BIQUAD_F32_ANSI)

    // Zeros then poles - Direct Form I
    // We need 2 memories for input, and 2 for output.
    SAMPLE x1 = w[0];
    SAMPLE x2 = w[1];
    SAMPLE y1 = w[2];
    SAMPLE y2 = w[3];
    for (int i = 0 ; i < len ; i++) {
        SAMPLE x0 = SHIFTL(input[i], FILTER_BIQUAD_SCALEUP_BITS);
        // SAMPLE x0 = SHIFTR(input[i], FILTER_BIQUAD_SCALEDOWN_BITS);
        SAMPLE w0 = FILT_MUL_SS(coef[0], x0) + FILT_MUL_SS(coef[1], x1) + FILT_MUL_SS(coef[2], x2);
        SAMPLE y0 = w0 - FILT_MUL_SS(coef[3], y1) - FILT_MUL_SS(coef[4], y2);
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
        output[i] = SHIFTR(y0, FILTER_BIQUAD_SCALEUP_BITS);
        //output[i] = SHIFTL(y0, FILTER_BIQUAD_SCALEDOWN_BITS);
    }
    w[0] = x1;
    w[1] = x2;
    w[2] = y1;
    w[3] = y2;
    AMY_PROFILE_STOP(DSPS_BIQUAD_F32_ANSI)

    return 0;
}


int8_t dsps_biquad_f32_ansi_split_fb(const SAMPLE *input, SAMPLE *output, int len, SAMPLE *coef, SAMPLE *w) {
    AMY_PROFILE_START(DSPS_BIQUAD_F32_ANSI_SPLIT_FB)
    // Rewrite the feeedback coefficients as a1 = -2 + e and a2 = 1 - f
    SAMPLE x1 = w[0];
    SAMPLE x2 = w[1];
    SAMPLE y1 = w[2];
    SAMPLE y2 = w[3];
    SAMPLE e = F2S(2.0f) + coef[3];  // So coef[3] = -2 + e
    SAMPLE f = F2S(1.0f) - coef[4];  // So coef[4] = 1 - f
    //fprintf(stderr, "e=%f (%d) f=%f\n", S2F(e), (e < F2S(0.0625)), S2F(f));
    for (int i = 0 ; i < len ; i++) {
        SAMPLE x0 = SHIFTL(input[i], FILTER_SCALEUP_BITS);
        SAMPLE w0 = FILT_MUL_SS(coef[0], x0) + FILT_MUL_SS(coef[1], x1) + FILT_MUL_SS(coef[2], x2);
        SAMPLE y0 = w0 + SHIFTL(y1, 1) - y2;
        y0 = y0 - FILT_MUL_SS(e, y1) + FILT_MUL_SS(f, y2);
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
    AMY_PROFILE_STOP(DSPS_BIQUAD_F32_ANSI_SPLIT_FB)

    return 0;
}

// 16 bit pseudo floating-point multiply.
// See https://colab.research.google.com/drive/1_uQto5WSVMiSPHQ34cHbCC6qkF614EoN#scrollTo=njPHwSB9VIJi

#define ABS(a) (a > 0) ? (a) : -(a)

static inline int headroom(SAMPLE a) {
    // How many bits bigger can this value get before overflow?
#ifdef AMY_USE_FIXEDPOINT
    return __builtin_clz(ABS(a)) - 1;  // -1 for sign bit.
#else  // !AMY_USE_FIXEDPOINT
    // How many bits can you shift before this max overflows?
    int bits = 0;
    while(a < F2S(128.0) && bits < 32) {
        a = SHIFTL(a, 1);
        ++bits;
    }
    return bits;
#endif  // AMY_USE_FIXEDPOINT
}

#ifdef AMY_USE_FIXEDPOINT

SAMPLE top16SMUL(SAMPLE a, SAMPLE b) {
    // Multiply the top 15 bits of a and b.
    int adropped = MAX(0, 16 - headroom(a));
    if (adropped) {
        a = SHIFTR(a + (1 << (adropped - 1)), adropped);
    }
    int resultdrop = 23 - adropped;
    int bdropped = MIN(resultdrop, MAX(0, 16 - headroom(b)));
    if (bdropped) {
        b = SHIFTR(b + (1 << (bdropped - 1)), bdropped);
    }
    resultdrop -= bdropped;
    SAMPLE result = a * b;
    if (resultdrop) {
        return SHIFTR(result + (1 << (resultdrop - 1)), resultdrop);
    }
    return result;
}

SAMPLE top16SMUL_a_part(SAMPLE a, int *p_adropped) {
    // Just the processing of a, so we can split it out
    int adropped = MAX(0, 16 - headroom(a));
    if (adropped) {
        a = SHIFTR(a + (1 << (adropped - 1)), adropped);
    }
    *p_adropped = adropped;
    return a;
}

SAMPLE top16SMUL_after_a(SAMPLE a_processed, SAMPLE b, int adropped, int bheadroom) {
    // The rest of top16SMUL after a has been preprocessed by top16SML_a_part.
    int resultdrop = 23 - adropped;
    int bdropped = MIN(resultdrop, MAX(0, 16 - bheadroom));
    if (bdropped) {
        b = SHIFTR(b + (1 << (bdropped - 1)), bdropped);
    }
    resultdrop -= bdropped;
    SAMPLE result = a_processed * b;
    if (resultdrop) {
        return SHIFTR(result + (1 << (resultdrop - 1)), resultdrop);
    }
    return result;
}

#else // !AMY_USE_FIXEDPOINT

// Sidestep all this logic for SAMPLE == float.

SAMPLE top16SMUL(SAMPLE a, SAMPLE b) {
    return a * b;
}

SAMPLE top16SMUL_a_part(SAMPLE a, int *p_adropped) {
    *p_adropped = 0;  // dropped_bits registration unused in float.
    return a;
}

SAMPLE top16SMUL_after_a(SAMPLE a_processed, SAMPLE b, int adropped_unused, int bheadroom_unused) {
    return a_processed * b;
}

#endif // AMY_USE_FIXEDPOINT

SAMPLE scan_max(SAMPLE* block, int len) {
    AMY_PROFILE_START(SCAN_MAX)

    // Find the max abs sample value in a block.
    SAMPLE max = 0;
    while (len--) {
        SAMPLE val = *block++;
        if (val > max) max = val;
        else if ((-val) > max) max = -val;
    }
    AMY_PROFILE_STOP(SCAN_MAX)
    return max;
}

// This is the multiply just for dsps_biquad_f32_ansi_split_fb_twice, which is only used for FILTER_LPF24.
#define FILT_MUL_SS_24(a, b) top16SMUL(a, b)

SAMPLE dsps_biquad_f32_ansi_split_fb_twice(const SAMPLE *input, SAMPLE *output, int len, SAMPLE *coef, SAMPLE *w, SAMPLE max_val) {
    // Apply the filter twice
    AMY_PROFILE_START(DSPS_BIQUAD_F32_ANSI_SPLIT_FB_TWICE)
    // Rewrite the feeedback coefficients as a1 = -2 + e and a2 = 1 - f
    SAMPLE x1 = w[0];
    SAMPLE x2 = w[1];
    SAMPLE y1 = w[2];
    SAMPLE y2 = w[3];
    SAMPLE v1 = w[4];
    SAMPLE v2 = w[5];
    SAMPLE state_max = scan_max(w, 6);
    int abits, bbits, cbits, ebits, fbits;
    SAMPLE a = top16SMUL_a_part(coef[0], &abits);
    SAMPLE e = top16SMUL_a_part(F2S(2.0f) + coef[3], &ebits);  // So coef[3] = -2 + e
    SAMPLE f = top16SMUL_a_part(F2S(1.0f) - coef[4], &fbits);  // So coef[4] = 1 - f
    assert(FILTER_SCALEUP_BITS == 0);
    bbits = headroom(max_val);
    cbits = headroom(SHIFTL(MAX(state_max, max_val), 2));
    SAMPLE max_out = 0;
    for (int i = 0 ; i < len ; i++) {
        SAMPLE x0, w0, v0;
        x0 = top16SMUL_after_a(a, input[i], abits, bbits);  // headroom(input[i]);
        w0 = x0 + SHIFTL(x1, 1) + x2;
        v0 = w0 + SHIFTL(v1, 1) - v2;
        v0 = v0
            - top16SMUL_after_a(e, v1, ebits, cbits)  //headroom(v1))
            + top16SMUL_after_a(f, v2, fbits, cbits); //headroom(v2));
        w0 = v0 + SHIFTL(v1, 1) + v2;
        w0 = top16SMUL_after_a(a, w0, abits, cbits);  //headroom(w0));
        SAMPLE y0 = w0 + SHIFTL(y1, 1) - y2;
        y0 = y0
            - top16SMUL_after_a(e, y1, ebits, cbits)  //headroom(y1))
            + top16SMUL_after_a(f, y2, fbits, cbits); //headroom(y2));
        x2 = x1;
        x1 = x0;
        v2 = v1;
        v1 = v0;
        y2 = y1;
        y1 = y0;
        output[i] = y0;
        if (y0 < 0) y0 = -y0;
        if (y0 > max_out) max_out = y0;
    }
    w[0] = x1;
    w[1] = x2;
    w[2] = y1;
    w[3] = y2;
    w[4] = v1;
    w[5] = v2;
    AMY_PROFILE_STOP(DSPS_BIQUAD_F32_ANSI_SPLIT_FB_TWICE)

    return max_out;
}

int8_t dsps_biquad_f32_ansi_commuted(const SAMPLE *input, SAMPLE *output, int len, SAMPLE *coef, SAMPLE *w) {
    AMY_PROFILE_START(DSPS_BIQUAD_F32_ANSI_COMMUTED)
    // poles before zeros, for Direct Form II
    for (int i = 0 ; i < len ; i++) {
        SAMPLE d0 = input[i] - FILT_MUL_SS(coef[3], w[0]) - FILT_MUL_SS(coef[4], w[1]);
        output[i] = FILT_MUL_SS(coef[0], d0) + FILT_MUL_SS(coef[1], w[0]) + FILT_MUL_SS(coef[2], w[1]);
        w[1] = w[0];
        w[0] = d0;
    }
    AMY_PROFILE_STOP(DSPS_BIQUAD_F32_ANSI_COMMUTED)

    return 0;
}

void update_filter(uint16_t osc) {
    // reset the delay for a filter
    // normal mod / adsr will just change the coeffs
    filter_delay[osc][0] = 0; filter_delay[osc][1] = 0;
}


void filters_deinit() {
    for(uint16_t i=0;i<AMY_NCHANS;i++) {
        for(uint16_t j=0;j<3;j++) free(eq_delay[i][j]);
        free(eq_delay[i]);
    }
    for(uint16_t i=0;i<3;i++) free(eq_coeffs[i]);
    for(uint16_t i=0;i<AMY_OSCS;i++) {
        free(coeffs[i]);
        free(filter_delay[i]);
    }
    free(coeffs);
    free(eq_coeffs);
    free(filter_delay);
    free(eq_delay);
}


void filters_init() {
    coeffs = malloc_caps(sizeof(SAMPLE*)*AMY_OSCS, FBL_RAM_CAPS);
    eq_coeffs = malloc_caps(sizeof(SAMPLE*)*3, FBL_RAM_CAPS);
    filter_delay = malloc_caps(sizeof(SAMPLE*)*AMY_OSCS, FBL_RAM_CAPS);
    eq_delay = malloc_caps(sizeof(SAMPLE**)*AMY_NCHANS, FBL_RAM_CAPS);
    for(uint16_t i=0;i<AMY_OSCS;i++) {
        coeffs[i] = malloc_caps(sizeof(SAMPLE)*5, FBL_RAM_CAPS);
        filter_delay[i] = malloc_caps(sizeof(SAMPLE) * FILT_NUM_DELAYS, FBL_RAM_CAPS);
    }
    for(uint16_t i=0;i<3;i++) {
        eq_coeffs[i] = malloc_caps(sizeof(SAMPLE) * 5, FBL_RAM_CAPS);
    }
    for(uint16_t i=0;i<AMY_NCHANS;i++) {
        eq_delay[i] = malloc_caps(sizeof(SAMPLE*) * 3, FBL_RAM_CAPS);
        for(uint16_t j=0;j<3;j++) {
            eq_delay[i][j] = malloc_caps(sizeof(SAMPLE) * FILT_NUM_DELAYS, FBL_RAM_CAPS);

        }
    }

    // update the parametric filters 
    dsps_biquad_gen_lpf_f32(eq_coeffs[0], EQ_CENTER_LOW /(float)AMY_SAMPLE_RATE, 0.707);
    dsps_biquad_gen_bpf_f32(eq_coeffs[1], EQ_CENTER_MED /(float)AMY_SAMPLE_RATE, 1.000);
    dsps_biquad_gen_hpf_f32(eq_coeffs[2], EQ_CENTER_HIGH/(float)AMY_SAMPLE_RATE, 0.707);
    for (int c = 0; c < AMY_NCHANS; ++c) {
        for (int b = 0; b < 3; ++b) {
            for (int d = 0; d < FILT_NUM_DELAYS; ++d) {
                eq_delay[c][b][d] = 0;
            }
        }
    }
}


void parametric_eq_process_old(SAMPLE *block) {
    //AMY_PROFILE_START(PARAMETRIC_EQ_PROCESS)

    SAMPLE output[2][AMY_BLOCK_SIZE];
    for(int c = 0; c < AMY_NCHANS; ++c) {
        SAMPLE *cblock = block + c * AMY_BLOCK_SIZE;
        dsps_biquad_f32_ansi(cblock, output[0], AMY_BLOCK_SIZE, eq_coeffs[0], eq_delay[c][0]);
        dsps_biquad_f32_ansi(cblock, output[1], AMY_BLOCK_SIZE, eq_coeffs[1], eq_delay[c][1]);
        for(int i = 0; i < AMY_BLOCK_SIZE; ++i)
            output[0][i] = FILT_MUL_SS(output[0][i], amy_global.eq[0]) - FILT_MUL_SS(output[1][i], amy_global.eq[1]);
        dsps_biquad_f32_ansi(cblock, output[1], AMY_BLOCK_SIZE, eq_coeffs[2], eq_delay[c][2]);
        for(int i = 0; i < AMY_BLOCK_SIZE; ++i)
            cblock[i] = output[0][i] + FILT_MUL_SS(output[1][i], amy_global.eq[2]);
    }
    //AMY_PROFILE_STOP(PARAMETRIC_EQ_PROCESS)

}


void parametric_eq_process(SAMPLE *block) {
    // Optimized to run all 3 filters interleaved, to avoid extra buffers/buf accesses.
    AMY_PROFILE_START(PARAMETRIC_EQ_PROCESS)

    for(int c = 0; c < AMY_NCHANS; ++c) {
        SAMPLE *cblock = block + c * AMY_BLOCK_SIZE;
        // Zeros then poles - Direct Form I
        // We need 2 memories for input, and 2 for output.
        SAMPLE x1 = eq_delay[c][0][0];
        SAMPLE x2 = eq_delay[c][0][1];
        SAMPLE y01 = eq_delay[c][0][2];
        SAMPLE y02 = eq_delay[c][0][3];
        SAMPLE y11 = eq_delay[c][1][2];
        SAMPLE y12 = eq_delay[c][1][3];
        SAMPLE y21 = eq_delay[c][2][2];
        SAMPLE y22 = eq_delay[c][2][3];
        for (int i = 0 ; i < AMY_BLOCK_SIZE ; i++) {
            SAMPLE x0 = SHIFTL(cblock[i], FILTER_BIQUAD_SCALEUP_BITS);
            SAMPLE w00 = FILT_MUL_SS(eq_coeffs[0][0], x0) + FILT_MUL_SS(eq_coeffs[0][1], x1) + FILT_MUL_SS(eq_coeffs[0][2], x2);
            SAMPLE y00 = w00 - FILT_MUL_SS(eq_coeffs[0][3], y01) - FILT_MUL_SS(eq_coeffs[0][4], y02);
            SAMPLE w10 = FILT_MUL_SS(eq_coeffs[1][0], x0) + FILT_MUL_SS(eq_coeffs[1][1], x1) + FILT_MUL_SS(eq_coeffs[1][2], x2);
            SAMPLE y10 = w10 - FILT_MUL_SS(eq_coeffs[1][3], y11) - FILT_MUL_SS(eq_coeffs[1][4], y12);
            SAMPLE w20 = FILT_MUL_SS(eq_coeffs[2][0], x0) + FILT_MUL_SS(eq_coeffs[2][1], x1) + FILT_MUL_SS(eq_coeffs[2][2], x2);
            SAMPLE y20 = w20 - FILT_MUL_SS(eq_coeffs[2][3], y21) - FILT_MUL_SS(eq_coeffs[2][4], y22);
            x2 = x1;
            x1 = x0;
            y02 = y01;
            y01 = y00;
            y12 = y11;
            y11 = y10;
            y22 = y21;
            y21 = y20;
            cblock[i] = SHIFTR(FILT_MUL_SS(y00, amy_global.eq[0]) - FILT_MUL_SS(y10, amy_global.eq[1]) + FILT_MUL_SS(y20, amy_global.eq[2]), FILTER_BIQUAD_SCALEUP_BITS);
        }
        eq_delay[c][0][0] = x1;
        eq_delay[c][0][1] = x2;
        eq_delay[c][0][2] = y01;
        eq_delay[c][0][3] = y02;
        eq_delay[c][1][2] = y11;
        eq_delay[c][1][3] = y12;
        eq_delay[c][2][2] = y21;
        eq_delay[c][2][3] = y22;
    }
    AMY_PROFILE_STOP(PARAMETRIC_EQ_PROCESS)

}


void hpf_buf(SAMPLE *buf, SAMPLE *state) {
    AMY_PROFILE_START(HPF_BUF)

    // Implement a dc-blocking HPF with a pole at (decay + 0j) and a zero at (1 + 0j).
    SAMPLE pole = F2S(0.99);
    SAMPLE xn1 = state[0];
    SAMPLE yn1 = state[1];
    for (uint16_t i = 0; i < AMY_BLOCK_SIZE; ++i) {
        SAMPLE w = buf[i] - xn1;
        xn1 = buf[i];
        buf[i] = w + FILT_MUL_SS(pole, yn1);
        yn1 = buf[i];
    }
    state[0] = xn1;
    state[1] = yn1;
    AMY_PROFILE_STOP(HPF_BUF)

}

void check_overflow(SAMPLE* block, int osc, char *msg) {
    // Search for overflow in a sample buffer as an unusually large sample-to-sample delta.
#ifdef AMY_DEBUG
    SAMPLE last = block[0];
    SAMPLE max = 0;
    SAMPLE maxdiff = 0;
    int len = AMY_BLOCK_SIZE;
    while (len--) {
        SAMPLE val = *block++;
        SAMPLE diff = val - last;
        if (diff < 0)  diff = -diff;
        if (diff > maxdiff) maxdiff = diff;
        last = val;
        if (val < 0)  val = -val;
        if (val > max)  max = val;
    }
    if (maxdiff > F2S(0.2f))
        fprintf(stderr, "Overflow at timeframe %.3f max=%.3f maxdiff=%.3f osc=%d msg=%s\n",
                (float)(total_samples) / AMY_SAMPLE_RATE,
                S2F(max), S2F(maxdiff), osc, msg);

#endif // AMY_DEBUG
}

void block_norm(SAMPLE* block, int len, int bits) {
    AMY_PROFILE_START(BLOCK_NORM)

    if (bits > 0) {
        while(len--) {
            *block = SHIFTL(*block, bits);
            ++block;
        }
    } else if (bits < 0) {
        bits = -bits;
        while(len--) {
            *block = SHIFTR(*block, bits);
            ++block;
        }
    }
    AMY_PROFILE_STOP(BLOCK_NORM)
}

void block_denorm(SAMPLE* block, int len, int bits) {
    block_norm(block, len, -bits);
}

void filter_process(SAMPLE * block, uint16_t osc, SAMPLE max_val) {

    SAMPLE filtmax = scan_max(synth[osc].filter_delay, 2 * FILT_NUM_DELAYS);
    if (max_val == 0 && filtmax == 0) return;

    AMY_PROFILE_START(FILTER_PROCESS)

    AMY_PROFILE_START(FILTER_PROCESS_STAGE0)

    float ratio = freq_of_logfreq(msynth[osc].filter_logfreq)/(float)AMY_SAMPLE_RATE;
    if(ratio < LOWEST_RATIO) ratio = LOWEST_RATIO;
    if(synth[osc].filter_type==FILTER_LPF || synth[osc].filter_type==FILTER_LPF24)
        dsps_biquad_gen_lpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    else if(synth[osc].filter_type==FILTER_BPF)
        dsps_biquad_gen_bpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    else if(synth[osc].filter_type==FILTER_HPF)
        dsps_biquad_gen_hpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    else {
        fprintf(stderr, "Unrecognized filter type %d\n", synth[osc].filter_type);
        return;
    }
    AMY_PROFILE_STOP(FILTER_PROCESS_STAGE0)

    AMY_PROFILE_START(FILTER_PROCESS_STAGE1)
    //SAMPLE max_val = scan_max(block, AMY_BLOCK_SIZE);
    // Also have to consider the filter state.
#define USE_BLOCK_FLOATING_POINT
#ifdef USE_BLOCK_FLOATING_POINT
    int filtnormbits = synth[osc].last_filt_norm_bits + headroom(filtmax);
#define HEADROOM_BITS 6
#define STATE_HEADROOM_BITS 2
    int normbits = MIN(MAX(0, headroom(max_val) - HEADROOM_BITS), MAX(0, filtnormbits - STATE_HEADROOM_BITS));
    normbits = MIN(normbits, synth[osc].last_filt_norm_bits + 1);  // Increase at most one bit per block.
    normbits = MIN(8, normbits);  // Without this, I get a weird sign flip at the end of TestLFO - intermediate overflow?
#else  // No block floating point
    const int normbits = 0;  // defeat BFP
#endif
    //printf("time %f max_val %f filtmax %f lastfiltnormbits %d filtnormbits %d normbits %d\n", total_samples / (float)AMY_SAMPLE_RATE, S2F(max_val), S2F(filtmax), synth[osc].last_filt_norm_bits, filtnormbits, normbits);
    //block_norm(&synth[osc].hpf_state[0], 2, normbits - synth[osc].last_filt_norm_bits);
    if(synth[osc].filter_type==FILTER_LPF24) {
        // 24 dB/oct by running the same filter twice.
        dsps_biquad_f32_ansi_split_fb_twice(block, block, AMY_BLOCK_SIZE, coeffs[osc], synth[osc].filter_delay, max_val);
    } else {
        block_norm(synth[osc].filter_delay, 2 * FILT_NUM_DELAYS, normbits - synth[osc].last_filt_norm_bits);
        block_norm(block, AMY_BLOCK_SIZE, normbits);
        dsps_biquad_f32_ansi_split_fb(block, block, AMY_BLOCK_SIZE, coeffs[osc], synth[osc].filter_delay);
        block_denorm(block, AMY_BLOCK_SIZE, normbits);
        synth[osc].last_filt_norm_bits = normbits;
    }
    //dsps_biquad_f32_ansi_commuted(block, block, AMY_BLOCK_SIZE, coeffs[osc], filter_delay[osc]);
    //block_denorm(synth[osc].filter_delay, 2 * FILT_NUM_DELAYS, normbits);
    // Final high-pass to remove residual DC offset from sub-fundamental LPF.  (Not needed now source waveforms are zero-mean).
    // hpf_buf(block, &synth[osc].hpf_state[0]); *** NOW NORMBITS IS IN THE WRONG PLACE
    AMY_PROFILE_STOP(FILTER_PROCESS_STAGE1)
    AMY_PROFILE_STOP(FILTER_PROCESS)

}


void reset_filter(uint16_t osc) {
    // Reset all the filter state to zero.
    // The LPF has typically accumulated a large DC offset, so you have to reset both
    // the LPF *and* the dc-blocking HPF at the same time.
    for(int i = 0; i < 2 * FILT_NUM_DELAYS; ++i) synth[osc].filter_delay[i] = 0;
    synth[osc].last_filt_norm_bits = 0;
    synth[osc].hpf_state[0] = 0; synth[osc].hpf_state[1] = 0;
}
