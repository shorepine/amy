// custom.c

#include "amy.h"
#include <assert.h>

struct custom_oscillator* custom_osc;

void amy_set_custom(struct custom_oscillator* custom) {
    assert(custom_osc == NULL);
    custom_osc = custom;
}

void custom_init() {
    if (custom_osc != NULL) {
        custom_osc->init();
    }
}

void custom_note_on(uint16_t osc, float freq) {
    assert(custom_osc != NULL);
    custom_osc->note_on(osc, freq);
}

void custom_note_off(uint16_t osc) {
    assert(custom_osc != NULL);
    custom_osc->note_off(osc);
}

void custom_mod_trigger(uint16_t osc) {
    assert(custom_osc != NULL);
    custom_osc->mod_trigger(osc);
}

SAMPLE render_custom(SAMPLE* buf, uint16_t osc) {
    assert(custom_osc != NULL);
    return custom_osc->render(buf, osc);
}

SAMPLE compute_mod_custom(uint16_t osc) {
    assert(custom_osc != NULL);
    return custom_osc->compute_mod(osc);
}
