// log2_lut() normalizes its argument into [1, 2) by shifting, so it requires a
// strictly positive one: shifting zero left leaves zero, forever. That is a
// deliberate contract -- it runs per-osc per-block, and a guard inside it would
// be paid on every sample -- so every caller must floor its argument. Two did
// not:
//
//   map_60dB_to_01f() guarded `lin == 0`, which misses a negative amplitude, a
//   NaN, and (in fixed-point builds) any amplitude too small to survive the
//   conversion to a SAMPLE. Any of those wedged the render thread in an
//   infinite loop: no crash, no message, just silence and a pegged core.
//
//   The DX7 attack branch in envelope.c fed log2_lut a level that reaches zero
//   once a breakpoint value passes ~2.37, which a hand-written patch can ask
//   for.
//
// Found by TestFuzzWireParser, which hung at its 92nd random message on x86 --
// an amp coefficient landed on the wrong side of the underflow there while the
// same message on arm64 did not.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

extern float map_60dB_to_01f(float lin);
extern float map_01_to_60dBf(float log);

static void render_a_bit(void) {
    for (int i = 0; i < 16; ++i) amy_simple_fill_buffer();
}

void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    // Every check here hangs rather than fails if a guard goes missing, so
    // bound the run: the alarm turns a wedged engine into a visible failure
    // instead of a CI job that sits at "in progress" for six hours.
    alarm(30);

    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    render_a_bit();

    printf("map_60dB_to_01f survives everything that isn't safely positive\n");
    CHECK(map_60dB_to_01f(0.0f) <= -10.0f, "0 -> %f", map_60dB_to_01f(0.0f));
    CHECK(map_60dB_to_01f(-0.5f) <= -10.0f, "-0.5 -> %f", map_60dB_to_01f(-0.5f));
    CHECK(map_60dB_to_01f(1e-9f) <= -10.0f, "1e-9 (underflows) -> %f", map_60dB_to_01f(1e-9f));
    CHECK(map_60dB_to_01f(nanf("")) <= -10.0f, "NaN -> %f", map_60dB_to_01f(nanf("")));

    printf("and still maps the range it documents\n");
    float one = map_60dB_to_01f(1.0f), milli = map_60dB_to_01f(0.001f);
    CHECK(one > 0.99f && one < 1.01f, "1.0 -> %f (want 1)", one);
    CHECK(milli > -0.01f && milli < 0.01f, "0.001 -> %f (want 0)", milli);
    CHECK(map_01_to_60dBf(map_60dB_to_01f(0.5f)) > 0.45f &&
          map_01_to_60dBf(map_60dB_to_01f(0.5f)) < 0.55f,
          "0.5 round-trips (%f)", map_01_to_60dBf(map_60dB_to_01f(0.5f)));

    printf("an oscillator with a junk amplitude renders instead of wedging\n");
    // The shape the fuzzer found: a negative ratio (logratio NaN) and an
    // out-of-range amp, then a note-on to make the envelopes run.
    amy_add_message((char *)"v0w0I-1a0.0000001l1n60Z");
    render_a_bit();
    CHECK(1, "negative ratio + tiny amp rendered");
    amy_add_message((char *)"S8192Z");
    render_a_bit();

    printf("a DX7 attack past the mapping's ceiling renders too\n");
    // Breakpoint values above ~2.37 drive MAP_ATTACK_LEVEL_S to zero.
    amy_add_message((char *)"v1w0T2A0,3.0,100,4.0,200,0Zl1n60Z");
    render_a_bit();
    CHECK(1, "oversized DX7 breakpoints rendered");

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
