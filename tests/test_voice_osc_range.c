// An osc number in a synth-directed command is voice-relative: base_osc is
// added to reach the real osc. Nothing used to check the result, so a number
// past the end of the voice addressed -- or pointed at -- whatever happened to
// live there, which is another voice of the same synth, or another synth
// entirely. amy.send(synth=1, osc=0, mod_source=1) on a one-osc voice FM'd
// itself from a neighbour's oscillator.
//
// These pin what is refused, what is kept, and (the subtle one) what must NOT
// be refused: a patch string can re-shape its own voice as it runs, so the
// bound has to be read live rather than captured. The drum kits open with
// `if3iv1in38Z`, which turns a 1-osc voice into a 38-osc one before the
// per-drum commands that follow; binding to the count known beforehand
// rejected the rest of the patch and silenced every kit.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

extern uint16_t *voice_to_base_osc;
extern int instrument_get_num_voices(int instrument_number, uint16_t *amy_voices);
extern int instrument_get_oscs_per_voice(int instrument_number);

static void render_a_bit(void) {
    for (int i = 0; i < 8; ++i) amy_simple_fill_buffer();
}

static void send(const char *m) {
    amy_add_message((char *)m);
    render_a_bit();
}

// Base osc of an instrument's first voice, or -1 if it has none.
static int base_of(uint8_t instr) {
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    if (instrument_get_num_voices(instr, voices) < 1)  return -1;
    uint16_t base = voice_to_base_osc[voices[0]];
    return AMY_IS_UNSET(base) ? -1 : (int)base;
}

// Wipe everything so each case starts from a known board.
static void fresh(void) {
    send("S8192Z");
}

static void test_reference_within_the_voice_is_kept(void) {
    printf("a reference inside the voice is kept\n");
    fresh();
    send("i1iv1in2Z");
    int base = base_of(1);
    CHECK(base >= 0, "synth 1 has a voice (base osc %d)", base);
    send("i1v0L1c1Z");
    CHECK(synth[base]->mod_source[0] == base + 1,
          "mod_source=1 became osc %d (base+1)", (int)synth[base]->mod_source[0]);
    CHECK(synth[base]->chained_osc == base + 1,
          "chained_osc=1 became osc %d (base+1)", (int)synth[base]->chained_osc);
}

static void test_reference_outside_the_voice_is_refused(void) {
    printf("a reference past the end of the voice is refused\n");
    fresh();
    send("i1iv1in1Z");
    int base = base_of(1);
    send("i1v0L1c1Z");
    CHECK(AMY_IS_UNSET(synth[base]->mod_source[0]),
          "mod_source=1 on a 1-osc voice left mod_source unset");
    CHECK(AMY_IS_UNSET(synth[base]->chained_osc),
          "chained_osc=1 on a 1-osc voice left chained_osc unset");
}

static void test_refused_reference_does_not_touch_the_neighbour(void) {
    printf("the osc it would have reached is left alone\n");
    fresh();
    // Two 1-osc synths. Whatever sits at synth 1's base+1 belongs to someone
    // else -- that is the osc the unchecked code used to write into.
    send("i1iv1in1Z");
    send("i2iv1in1Z");
    int base = base_of(1);
    uint16_t neighbour_wave = synth[base + 1] ? synth[base + 1]->wave : (uint16_t)0;
    // An addressed osc past the voice, and a chained_osc past it.
    send("i1v1w2Zi1v0c1Z");
    CHECK(synth[base + 1] == NULL || synth[base + 1]->wave == neighbour_wave,
          "osc base+1 untouched (%s)",
          synth[base + 1] == NULL ? "still unallocated"
                                  : (synth[base + 1]->wave == neighbour_wave ? "wave unchanged"
                                                                             : "OVERWRITTEN"));
    CHECK(AMY_IS_UNSET(synth[base]->chained_osc), "and nothing got chained to it");
}

static void test_algo_source_is_checked_per_operator(void) {
    printf("algo_source is checked one operator at a time\n");
    fresh();
    send("i1iv1in2Z");
    int base = base_of(1);
    send("i1v0w8o1O1,5,,,,Z");
    CHECK(synth[base]->algo_source[0] == base + 1,
          "operator 0's in-range source became osc %d", (int)synth[base]->algo_source[0]);
    CHECK(AMY_IS_UNSET(synth[base]->algo_source[1]),
          "operator 1's out-of-range source was left alone");
}

static void test_reset_osc_number_versus_reset_mask(void) {
    printf("reset takes an osc number or a mask, and only the number is voice-relative\n");
    fresh();
    send("i1iv1in2Z");
    int base = base_of(1);
    send("i1v0w2Zi1v1w2Z");
    CHECK(synth[base]->wave == 2 && synth[base + 1]->wave == 2, "both oscs set to wave 2");
    send("i1S1Z");                 // reset osc 1 *of this voice*
    CHECK(synth[base]->wave == 2, "reset=1 left osc 0 alone");
    CHECK(synth[base + 1]->wave != 2, "reset=1 cleared the voice's osc 1");
    // An osc number past the voice is refused like any other reference.
    fresh();
    send("i1iv1in1Z");
    base = base_of(1);
    send("i1v0w2Z");
    send("i1S3Z");
    CHECK(synth[base]->wave == 2, "reset=3 on a 1-osc voice changed nothing");
    // A mask is not an osc number and must not be offset or range-checked.
    send("i1S8192Z");              // RESET_ALL_OSCS
    CHECK(instrument_get_num_voices(1, NULL) == 0, "reset=RESET_ALL_OSCS still cleared the synth");
}

static void test_absolute_osc_numbers_are_unbounded(void) {
    printf("an event with no synth is absolute, and not bounded by any voice\n");
    fresh();
    send("v5w2L9Z");
    CHECK(synth[5] != NULL && synth[5]->wave == 2, "osc 5 took wave 2");
    CHECK(synth[5] != NULL && synth[5]->mod_source[0] == 9, "and mod_source 9, unchecked");
}

static void test_a_patch_may_reshape_its_own_voice(void) {
    printf("a patch string that re-shapes its voice is not fought by the bound\n");
    fresh();
    // Drum kits open with `if3iv1in38Z`: patch_oscs says 1 osc until that first
    // command runs. A bound captured before the string ran rejected every
    // command after it.
    send("i10iv1K384Z");
    int oscs = instrument_get_oscs_per_voice(10);
    CHECK(oscs > 1, "the kit voice grew past its declared size (%d oscs)", oscs);
    int base = base_of(10);
    CHECK(base >= 0, "kit voice allocated at base osc %d", base);
    int configured = 0;
    for (int i = 0; i < oscs; ++i)
        if (synth[base + i] != NULL)  ++configured;
    CHECK(configured == oscs, "all %d of its oscs were configured (%d were)", oscs, configured);
}

// AMY calls these; the test binary has to provide them.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    render_a_bit();

    test_reference_within_the_voice_is_kept();
    test_reference_outside_the_voice_is_refused();
    test_refused_reference_does_not_touch_the_neighbour();
    test_algo_source_is_checked_per_operator();
    test_reset_osc_number_versus_reset_mask();
    test_absolute_osc_numbers_are_unbounded();
    test_a_patch_may_reshape_its_own_voice();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
