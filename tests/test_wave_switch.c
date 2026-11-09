// Changing wave on a sounding osc must switch the table it reads.
//
// The saw, pulse and triangle renders pick a table from their lutset only
// when synth->lut is NULL, which note-on arranges. A WAVE change mid-note
// that left the old wave's table in place would keep a triangle reading the
// saw's table, and the other way round.
// The check: after the switch, synth->lut points into the new wave's lutset.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include "amy.h"

extern LUT saw_fxpt_lutset[];
extern LUT triangle_fxpt_lutset[];

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

void delay_ms(uint32_t ms) { (void)ms; }

static void restart(void) {
    amy_stop();
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
}

// Whether lut is an entry of a lutset (the sets end with a zero-size entry).
static int lut_in_set(const LUT *lut, const LUT *set) {
    for (const LUT *e = set; e->table_size > 0; ++e)
        if (lut == e) return 1;
    return 0;
}

static const LUT *lut_after_switch(int from, int to) {
    restart();
    char msg[32];
    snprintf(msg, sizeof msg, "v0w%dn60l1Z", from);
    amy_add_message(msg);
    amy_simple_fill_buffer();
    amy_simple_fill_buffer();
    snprintf(msg, sizeof msg, "v0w%dZ", to);
    amy_add_message(msg);
    amy_simple_fill_buffer();
    return synth[0]->lut;
}

int main(void) {
    printf("a mid-note wave change reads the new wave's table\n");
    CHECK(lut_in_set(lut_after_switch(SAW_DOWN, TRIANGLE), triangle_fxpt_lutset),
          "SAW_DOWN -> TRIANGLE reads the triangle set");
    CHECK(lut_in_set(lut_after_switch(TRIANGLE, SAW_DOWN), saw_fxpt_lutset),
          "TRIANGLE -> SAW_DOWN reads the saw set");
    CHECK(lut_in_set(lut_after_switch(TRIANGLE, PULSE), saw_fxpt_lutset),
          "TRIANGLE -> PULSE reads the saw set");
    restart();
    amy_add_message("v0w2n60l1Z");
    amy_simple_fill_buffer();
    const LUT *picked = synth[0]->lut;
    amy_add_message("v0w2s1Z");   // the same wave again, with a bend up
    amy_simple_fill_buffer();
    CHECK(picked != NULL && synth[0]->lut == picked, "a repeated SAW_DOWN keeps the table picked at note-on");
    amy_stop();
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
