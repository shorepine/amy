// Lookup-table versions of log2/exp2.

#include "amy.h"
#include "log2_exp2_fxpt_lutable.h"

static inline SAMPLE lut_val(SAMPLE frac, const LUTSAMPLE *table, const int log2_tab_size) {
    int index = INT_OF_S(frac, log2_tab_size);
    SAMPLE index_frac_part = S_FRAC_OF_S(frac, log2_tab_size);
    // Tables are constructed to extend one value beyond the log2_tab_size bits max.
    return L2S(table[index]) + MUL0_SS(L2S(table[index + 1] - table[index]), index_frac_part);
}


AMY_IRAM_ATTR SAMPLE log2_lut(SAMPLE x) {
    // assert(x > 0);
    int scale = 0;
    while (x < F2S(1.0f)) {
        x = SHIFTL(x, 1);
        --scale;
    }
    while (x >= F2S(2.0f)) {
        x = SHIFTR(x, 1);
        ++scale;
    }
    // lut_val is negated because table is stored as negative (to reach 1.0).
    return AMY_I2S(scale, 0) - lut_val(x - F2S(1.0f), log2_fxpt_lutable, 8 /* =log_2(256) */);
}


AMY_IRAM_ATTR SAMPLE exp2_lut(SAMPLE x) {
    int offset = INT_OF_S(x, 0);
    SAMPLE x_frac = S_FRAC_OF_S(x, 0);
    // lut_val is negated because table is stored as negative (to reach 1.0).
    SAMPLE unnorm = F2S(1.0f)
        - lut_val(x_frac, exp2_fxpt_lutable, 8 /* =log_2(256) */);
    if (offset > 0)
        return SHIFTL(unnorm, offset);
    else
        return SHIFTR(unnorm, -offset);
}


AMY_IRAM_ATTR SAMPLE sin_lut(SAMPLE x) {
    // sin(x / 2 pi)
    SAMPLE x_frac = SHIFTL(S_FRAC_OF_S(x, 0), 2);  // Fractional part of x * 4, so 0 <= x_frac < 4.
    int quadrant = INT_OF_S(x_frac, 0);
    SAMPLE x_frac2 = S_FRAC_OF_S(x, 2);  // Now just 0 <= x_frac < 1
    int sign = 1;
    if (quadrant & 1)
        x_frac2 = F2S(1.f) - x_frac2;
    if (quadrant & 2)
        sign = -1;
    //printf("x=%f quadrant=%d x_frac=%f x_frac2=%f\n", S2F(x), quadrant, S2F(x_frac), S2F(x_frac2));
    return -sign * lut_val(x_frac2, qsin_fxpt_lutable, 8);
}

AMY_IRAM_ATTR SAMPLE cos_lut(SAMPLE x) {
    return sin_lut(x + F2S(0.25f));
}
