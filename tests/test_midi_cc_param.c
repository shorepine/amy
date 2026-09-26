// Tests for MIDI mappings that name an AMY PARAMETER directly instead of
// carrying a wire command template (issue #1175):
//
//     ic<C>,<L>,<N>,<X>,<O>,<P>[,<OSC>][,<P>,<OSC>...]
//
// What is worth pinning down:
//
//  - the value lands in the named param, in amy.send() units (Hz for
//    filter_freq, so the stored value is its logfreq), after the same
//    L/N/X/O transform a template mapping gets;
//  - it reaches EVERY voice of the synth, at the voice-relative OSC, and
//    no other osc;
//  - repeated P,OSC pairs each get the value;
//  - integer params are rounded, not truncated;
//  - a bad payload (a param that can't be driven, an odd P,OSC list) is
//    refused WITHOUT deleting the mapping already on that CC;
//  - the mapping survives a state dump, and the template form still works.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "amy.h"

void delay_ms(uint32_t ms) { (void)ms; }

extern uint16_t *voice_to_base_osc;

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

static void restart(void) {
    amy_stop();
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.features.default_synths = 0;
    amy_start(c);
}

static void render(void) {
    for (int i = 0; i < 4; ++i) amy_simple_fill_buffer();
}

static void wire(const char *s) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    amy_play_message(buf);
    render();
}

// A CC arriving on MIDI channel `ch`, which is synth `ch`.
static void cc(int ch, int code, int value) {
    uint8_t bytes[3] = { (uint8_t)(0xB0 | ((ch - 1) & 0x0F)), (uint8_t)code, (uint8_t)value };
    uint32_t now;
    AMY_UNSET(now);
    midi_msg_handler(bytes, 3, 0, now);
    render();
}

// Absolute osc number of voice-relative `osc` in the synth's voice `v`.
static uint16_t osc_of(int synth_num, int v, int osc) {
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    int n = instrument_get_num_voices(synth_num, voices);
    if (v >= n) return 0xFFFF;
    return voice_to_base_osc[voices[v]] + osc;
}

static float res_at(int v, int osc) { return synth[osc_of(1, v, osc)]->resonance; }

// Two voices of two oscs each, on synth 1 (MIDI channel 1).
static void two_by_two(void) {
    restart();
    wire("i1iv2in2");
    // Touch every osc so each has a synthinfo to read back.
    wire("i1v0R0.7");
    wire("i1v1R0.7");
}

static void test_linear_param_all_voices(void) {
    printf("linear CC -> resonance on osc 0 of every voice\n");
    two_by_two();
    char m[64];
    snprintf(m, sizeof(m), "i1ic74,0,0.5,4.5,0,%d", RESONANCE);
    wire(m);
    cc(1, 74, 127);
    CHECK(fabsf(res_at(0, 0) - 4.5f) < 1e-4, "voice 0 osc 0 at max (got %.4f)", res_at(0, 0));
    CHECK(fabsf(res_at(1, 0) - 4.5f) < 1e-4, "voice 1 osc 0 at max (got %.4f)", res_at(1, 0));
    CHECK(fabsf(res_at(0, 1) - 0.7f) < 1e-4, "osc 1 untouched (got %.4f)", res_at(0, 1));
    cc(1, 74, 0);
    CHECK(fabsf(res_at(1, 0) - 0.5f) < 1e-4, "CC 0 gives min (got %.4f)", res_at(1, 0));
}

static void test_log_filter_freq_at_osc(void) {
    printf("log CC -> filter_freq (Hz) on osc 1\n");
    two_by_two();
    char m[64];
    snprintf(m, sizeof(m), "i1ic75,1,100,6400,0,%d,1", FILTER_FREQ);
    wire(m);
    cc(1, 75, 127);
    float want = logfreq_of_freq(6400.0f);
    for (int v = 0; v < 2; ++v) {
        float got = synth[osc_of(1, v, 1)]->filter_logfreq_coefs[COEF_CONST];
        CHECK(fabsf(got - want) < 1e-3, "voice %d osc 1 at 6400 Hz (logfreq %.4f, want %.4f)", v, got, want);
    }
    float got0 = synth[osc_of(1, 0, 0)]->filter_logfreq_coefs[COEF_CONST];
    CHECK(fabsf(got0 - want) > 0.5f, "osc 0 untouched (logfreq %.4f)", got0);
    // Log mapping: the midpoint is the geometric mean, 800 Hz.
    cc(1, 75, 0);
    float lo = synth[osc_of(1, 0, 1)]->filter_logfreq_coefs[COEF_CONST];
    CHECK(fabsf(lo - logfreq_of_freq(100.0f)) < 1e-3, "CC 0 gives 100 Hz (logfreq %.4f)", lo);
}

static void test_multiple_targets(void) {
    printf("P,OSC,P,OSC: one CC drives both oscs\n");
    two_by_two();
    char m[64];
    snprintf(m, sizeof(m), "i1ic76,0,0,2,0,%d,0,%d,1", RESONANCE, RESONANCE);
    wire(m);
    cc(1, 76, 127);
    for (int v = 0; v < 2; ++v)
        for (int o = 0; o < 2; ++o)
            CHECK(fabsf(res_at(v, o) - 2.0f) < 1e-4, "voice %d osc %d set (got %.4f)", v, o, res_at(v, o));
}

static void test_integer_rounding(void) {
    printf("integer params round\n");
    two_by_two();
    char m[64];
    // 0..6 over 0..127: CC 64 is 3.02 -> 3, CC 116 is 5.48 -> 5, CC 117 is 5.53 -> 6.
    snprintf(m, sizeof(m), "i1ic77,0,0,6,0,%d", FILTER_TYPE);
    wire(m);
    cc(1, 77, 64);
    CHECK(synth[osc_of(1, 1, 0)]->filter_type == 3, "CC 64 -> filter type 3 (got %d)", synth[osc_of(1, 1, 0)]->filter_type);
    cc(1, 77, 117);
    CHECK(synth[osc_of(1, 1, 0)]->filter_type == 6, "CC 117 -> 6, rounded not truncated (got %d)", synth[osc_of(1, 1, 0)]->filter_type);
}

static void test_bad_payload_keeps_mapping(void) {
    printf("a refused payload leaves the existing mapping alone\n");
    two_by_two();
    char m[64];
    snprintf(m, sizeof(m), "i1ic74,0,0,1,0,%d", RESONANCE);
    wire(m);
    CHECK(!amy_param_is_settable(MIDI_NOTE), "MIDI_NOTE is not settable");
    CHECK(!amy_param_is_settable(CHAINED_OSC), "CHAINED_OSC is not settable");
    CHECK(amy_param_is_settable(FILTER_FREQ + 3), "a filter_freq coef is settable");
    snprintf(m, sizeof(m), "i1ic74,0,0,1,0,%d", MIDI_NOTE);
    wire(m);
    snprintf(m, sizeof(m), "i1ic74,0,0,1,0,%d,0,%d", RESONANCE, RESONANCE);  // odd list
    wire(m);
    cc(1, 74, 127);
    CHECK(fabsf(res_at(0, 0) - 1.0f) < 1e-4, "original mapping still drives resonance (got %.4f)", res_at(0, 0));
}

static void test_state_round_trip(void) {
    printf("the direct form survives a state dump\n");
    two_by_two();
    char m[64], want[32];
    snprintf(m, sizeof(m), "i1ic75,1,100,6400,0,%d,1", FILTER_FREQ);
    wire(m);
    char buf[MAX_MESSAGE_LEN];
    bool found = midi_fetch_mapping_command(1, MIDI_MAP_TYPE_CC, 75, buf, sizeof(buf));
    snprintf(want, sizeof(want), ",%d,1Z", FILTER_FREQ);
    CHECK(found && strstr(buf, want) != NULL, "dump carries the param list (%s)", buf);
    // Feed the dump back in, as a state restore would, and check it works.
    restart();
    wire("i1iv2in2");
    wire("i1v1R0.7");
    char restore[MAX_MESSAGE_LEN + 4];
    snprintf(restore, sizeof(restore), "i1%s", buf);
    wire(restore);
    cc(1, 75, 127);
    float got = synth[osc_of(1, 1, 1)]->filter_logfreq_coefs[COEF_CONST];
    CHECK(fabsf(got - logfreq_of_freq(6400.0f)) < 1e-3, "restored mapping drives filter_freq (logfreq %.4f)", got);
}

static void test_template_form_unchanged(void) {
    printf("the wire-template form still works next to it\n");
    two_by_two();
    char m[64];
    snprintf(m, sizeof(m), "i1ic74,0,0,1,0,%d", RESONANCE);
    wire(m);
    wire("i1ic20,0,0,3,0,i%iv1R%v");
    cc(1, 20, 127);
    CHECK(fabsf(res_at(1, 1) - 3.0f) < 1e-4, "template CC sets osc 1 (got %.4f)", res_at(1, 1));
    cc(1, 74, 127);
    CHECK(fabsf(res_at(1, 0) - 1.0f) < 1e-4, "direct CC sets osc 0 (got %.4f)", res_at(1, 0));
}

int main(void) {
    test_linear_param_all_voices();
    test_log_filter_freq_at_osc();
    test_multiple_targets();
    test_integer_rounding();
    test_bad_payload_keeps_mapping();
    test_state_round_trip();
    test_template_form_unchanged();
    amy_stop();
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
