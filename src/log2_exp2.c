// Lookup-table versions of log2/exp2.

#include "amy.h"
#include "log2_exp2_fxpt_lutable.h"


static inline SAMPLE lut_val(SAMPLE frac, const LUTSAMPLE *table, const int log2_tab_size) {
    int index = INT_OF_S(frac, log2_tab_size);
    SAMPLE index_frac_part = S_FRAC_OF_S(frac, log2_tab_size);
    // Tables are constructed to extend one value beyond the log2_tab_size bits max.
    return L2S(table[index]) + MUL0_SS(L2S(table[index + 1] - table[index]), index_frac_part);
}


SAMPLE log2_lut(SAMPLE x) {
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


SAMPLE exp2_lut(SAMPLE x) {
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
