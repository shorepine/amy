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
    custom_osc->note_on(&synth[osc], freq);
}

void custom_note_off(uint16_t osc) {
    assert(custom_osc != NULL);
    custom_osc->note_off(&synth[osc]);
}

void custom_mod_trigger(uint16_t osc) {
    assert(custom_osc != NULL);
    custom_osc->mod_trigger(&synth[osc]);
}

SAMPLE render_custom(SAMPLE* buf, uint16_t osc) {
    assert(custom_osc != NULL);
    return custom_osc->render(buf, &synth[osc]);
}

SAMPLE compute_mod_custom(uint16_t osc) {
    assert(custom_osc != NULL);
    return custom_osc->compute_mod(&synth[osc]);
}
