// Tests for midi_cc_output (iC, issue #1175): a synth's parameter changes
// echoed out of the MIDI port as control changes.
//
//     iC<C>,<L>,<N>,<X>,<O>,<P>[,<OSC>][,<P>,<OSC>...]
//
// It is the converse of midi_cc's direct-parameter form, so the property
// the whole thing hangs on is that the two transforms are inverses: a
// value midi_cc would produce from CC v goes back out as CC v, for every
// v, linear and log.  That is asserted here over the whole 0..127 range.
//
// Also pinned down:
//  - the channel: the synth's MIDI note_output channel if it has one, else
//    the synth number, else (a synth outside 1..16) nothing at all;
//  - only a change at the watched osc counts, and an event naming no osc
//    reaches every osc, so it counts too;
//  - bus params, which never reach the per-voice path, still go out;
//  - a CC is sent on change only;
//  - a change that arrived over MIDI is not echoed back unless the note
//    output says to forward MIDI input -- otherwise ic + iC on one CC is a
//    feedback loop;
//  - clearing, the state dump round trip, and that an output mapping never
//    answers an incoming CC.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "amy.h"

void delay_ms(uint32_t ms) { (void)ms; }

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

// ---- what the host saw -------------------------------------------------

#define LOG_MAX 256
static uint8_t midi_log[LOG_MAX][3];
static int midi_writes = 0;

static void test_midi_hook(uint8_t *bytes, uint16_t len) {
    if (midi_writes < LOG_MAX && len >= 3) memcpy(midi_log[midi_writes], bytes, 3);
    midi_writes++;
}

static void clear_log(void) { midi_writes = 0; }

// ---- driving -----------------------------------------------------------

static void restart(void) {
    amy_stop();
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.features.default_synths = 0;
    c.amy_external_midi_output_hook = test_midi_hook;
    amy_start(c);
}

static void render(void) {
    for (int i = 0; i < 4; ++i) amy_simple_fill_buffer();
}

static void wire(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    amy_play_message(buf);
    render();
}

// A CC arriving on MIDI channel `ch`.
static void cc_in(int ch, int code, int value) {
    uint8_t bytes[3] = { (uint8_t)(0xB0 | ((ch - 1) & 0x0F)), (uint8_t)code, (uint8_t)value };
    uint32_t now;
    AMY_UNSET(now);
    midi_msg_handler(bytes, 3, 0, now);
    render();
}

static bool last_is(uint8_t status, uint8_t code, uint8_t value) {
    if (midi_writes < 1 || midi_writes > LOG_MAX) return false;
    uint8_t *m = midi_log[midi_writes - 1];
    return m[0] == status && m[1] == code && m[2] == value;
}

static void last_str(char *s) {
    if (midi_writes < 1 || midi_writes > LOG_MAX) { strcpy(s, "(none)"); return; }
    uint8_t *m = midi_log[midi_writes - 1];
    sprintf(s, "%02X %d %d", m[0], m[1], m[2]);
}

static void synth_2x2(int n) { wire("i%div2in2", n); }

// ---- tests -------------------------------------------------------------

static void test_basic(void) {
    printf("a watched change goes out as a CC\n");
    restart();
    synth_2x2(1);
    wire("i1iC74,0,0,4,0,%d", RESONANCE);
    clear_log();
    char s[32];
    wire("i1R1");  // 1/4 of 0..4 -> 31.75 -> 32
    last_str(s);
    CHECK(midi_writes == 1 && last_is(0xB0, 74, 32), "resonance 1 -> CC 74 = 32 on channel 1 (%s)", s);
    wire("i1R4");
    last_str(s);
    CHECK(last_is(0xB0, 74, 127), "resonance 4 -> 127 (%s)", s);
    wire("i1R9");
    last_str(s);
    CHECK(last_is(0xB0, 74, 127) && midi_writes == 2, "out of range clamps, and a clamped repeat isn't resent (%d sent)", midi_writes);
}

static void test_dedupe(void) {
    printf("sent on change only\n");
    restart();
    synth_2x2(1);
    wire("i1iC74,0,0,4,0,%d", RESONANCE);
    clear_log();
    wire("i1R2");
    wire("i1R2");
    wire("i1R2.001");  // the same 7-bit value
    CHECK(midi_writes == 1, "three changes to one CC value send once (%d sent)", midi_writes);
    wire("i1R3");
    CHECK(midi_writes == 2, "a new value sends (%d sent)", midi_writes);
}

static void test_round_trip(void) {
    printf("the reverse transform inverts midi_cc's, for every value\n");
    // Linear, and a log map with an offset, both as midi_cc would see them.
    struct { int is_log; float n, x, o; int param; const char *code; } maps[] = {
        { 0, 0.5f, 8.0f, 0.0f, RESONANCE, "R" },
        { 1, 100.0f, 6400.0f, 0.0f, FILTER_FREQ, "F" },
        { 1, 0.0f, 1.0f, 0.1f, RESONANCE, "R" },
    };
    for (size_t k = 0; k < sizeof(maps) / sizeof(maps[0]); ++k) {
        restart();
        synth_2x2(1);
        wire("i1iC20,%d,%g,%g,%g,%d", maps[k].is_log, maps[k].n, maps[k].x, maps[k].o, maps[k].param);
        int bad = 0, first_bad = -1;
        for (int v = 0; v < 128; ++v) {
            float n = maps[k].n, x = maps[k].x, o = maps[k].o, val;
            if (maps[k].is_log)
                val = (n + o) * expf(logf((x + o) / (n + o)) * v / 127.0f) - o;
            else
                val = n + (x - n) * v / 127.0f;
            clear_log();
            wire("i1%s%.6f", maps[k].code, val);
            // Every step is a new value, so every step must send, exactly v.
            if (!(midi_writes == 1 && last_is(0xB0, 20, (uint8_t)v))) {
                ++bad;
                if (first_bad < 0) first_bad = v;
            }
        }
        CHECK(bad == 0, "%s N=%g X=%g O=%g: all 128 values round-trip (%d wrong, first at %d)",
              maps[k].is_log ? "log" : "linear", maps[k].n, maps[k].x, maps[k].o, bad, first_bad);
    }
}

static void test_osc_matching(void) {
    printf("only a change at the watched osc counts\n");
    restart();
    synth_2x2(1);
    wire("i1iC74,0,0,4,0,%d,1", RESONANCE);
    clear_log();
    wire("i1v0R2");
    CHECK(midi_writes == 0, "osc 0 changed: nothing sent (%d)", midi_writes);
    wire("i1v1R2");
    CHECK(midi_writes == 1 && last_is(0xB0, 74, 64), "osc 1 changed: sent (%d)", midi_writes);
    wire("i1R4");
    CHECK(midi_writes == 2 && last_is(0xB0, 74, 127), "no osc named reaches osc 1 too: sent (%d)", midi_writes);
}

static void test_multiple_targets(void) {
    printf("P,OSC pairs: any of them changing sends the CC\n");
    restart();
    synth_2x2(1);
    wire("i1iC74,0,0,4,0,%d,0,%d,1", RESONANCE, RESONANCE);
    clear_log();
    wire("i1v0R1");
    wire("i1v1R2");
    CHECK(midi_writes == 2 && last_is(0xB0, 74, 64), "each osc's change goes out (%d sent)", midi_writes);
    clear_log();
    wire("i1R3");
    CHECK(midi_writes == 1, "one event touching both sends one CC (%d)", midi_writes);
}

static void test_bus_param(void) {
    printf("bus params go out too\n");
    restart();
    synth_2x2(1);
    wire("i1iC7,0,0,2,0,%d", VOLUME);
    wire("i1iC91,0,0,1,0,%d", BUS_DIST_MIX);
    wire("i1iC92,0,0,1,0,%d", DIST_MIX);
    clear_log();
    wire("i1V1");
    CHECK(midi_writes == 1 && last_is(0xB0, 7, 64), "volume 1 of 0..2 -> CC 7 = 64 (%d sent)", midi_writes);
    clear_log();
    wire("i1GM0.5");  // no osc: the bus's distortion mix
    CHECK(midi_writes == 1 && last_is(0xB0, 91, 64), "no osc: dist_mix is the bus's (%d sent)", midi_writes);
    clear_log();
    wire("i1v0GM1");  // an osc named: the osc's
    CHECK(midi_writes == 1 && last_is(0xB0, 92, 127), "osc named: dist_mix is the osc's (%d sent)", midi_writes);
}

static void test_channel(void) {
    printf("the channel\n");
    restart();
    synth_2x2(3);
    wire("i3iC74,0,0,4,0,%d", RESONANCE);
    clear_log();
    wire("i3R4");
    CHECK(midi_writes == 1 && last_is(0xB2, 74, 127), "no note output: synth 3 sends on channel 3");
    wire("i3iG%d,9", NOTE_OUTPUT_MIDI_OUT);
    wire("i3R0");
    CHECK(last_is(0xB8, 74, 0), "a MIDI note output on channel 9 takes the CCs too");

    synth_2x2(20);
    wire("i20iC74,0,0,4,0,%d", RESONANCE);
    clear_log();
    wire("i20R4");
    CHECK(midi_writes == 0, "synth 20 with no note output has no channel: nothing sent (%d)", midi_writes);
    wire("i20iG%d,6", NOTE_OUTPUT_MIDI_OUT);
    wire("i20R0");
    CHECK(midi_writes == 1 && last_is(0xB5, 74, 0), "...until a note output names one (%d sent)", midi_writes);
}

static void test_no_feedback(void) {
    printf("a change that came in over MIDI isn't echoed by default\n");
    restart();
    synth_2x2(1);
    wire("i1ic74,0,0,4,0,%d", RESONANCE);
    wire("i1iC74,0,0,4,0,%d", RESONANCE);
    clear_log();
    cc_in(1, 74, 100);
    CHECK(midi_writes == 0, "ic + iC on one CC: no echo (%d sent)", midi_writes);
    wire("i1iG%d,1,1", NOTE_OUTPUT_MIDI_OUT);  // forward_midi_in
    cc_in(1, 74, 90);
    CHECK(midi_writes == 1 && last_is(0xB0, 74, 90), "note output with forward_midi_in: echoed (%d sent)", midi_writes);
}

static void test_output_is_not_input(void) {
    printf("an output mapping never answers an incoming CC\n");
    restart();
    synth_2x2(1);
    wire("i1v0R0.7");
    wire("i1iC74,0,0,4,0,%d", RESONANCE);
    clear_log();
    cc_in(1, 74, 127);
    extern uint16_t *voice_to_base_osc;
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    instrument_get_num_voices(1, voices);
    float r = synth[voice_to_base_osc[voices[0]]]->resonance;
    CHECK(fabsf(r - 0.7f) < 1e-4 && midi_writes == 0, "resonance unchanged (%.3f), nothing sent (%d)", r, midi_writes);
}

static void test_template_refused(void) {
    printf("iC needs a parameter, not a wire command\n");
    restart();
    synth_2x2(1);
    wire("i1iC74,0,0,1,0,i1R%%v");
    char buf[MAX_MESSAGE_LEN];
    CHECK(!midi_fetch_mapping_command(1, MIDI_MAP_TYPE_CC_OUT, 74, buf, sizeof(buf)), "not stored");
}

static void test_clear(void) {
    printf("clearing\n");
    restart();
    synth_2x2(1);
    wire("i1iC74,0,0,4,0,%d", RESONANCE);
    wire("i1iC75,0,0,4,0,%d,1", RESONANCE);
    wire("i1iC74");
    clear_log();
    wire("i1v0R4");
    CHECK(midi_writes == 0, "iC74 alone clears CC 74 (%d sent)", midi_writes);
    wire("i1v1R4");
    CHECK(midi_writes == 1, "...and leaves CC 75 (%d sent)", midi_writes);
    wire("i1iC255");
    clear_log();
    wire("i1v1R1");
    CHECK(midi_writes == 0, "iC255 clears them all (%d sent)", midi_writes);
}

static void test_state_round_trip(void) {
    printf("an output mapping survives a state dump\n");
    restart();
    synth_2x2(1);
    wire("i1iC75,1,100,6400,0,%d,1", FILTER_FREQ);
    // It is in the synth's command stream...
    char buf[MAX_MESSAGE_LEN], found[MAX_MESSAGE_LEN] = "";
    void *state = NULL;
    do {
        state = yield_synth_commands(1, buf, sizeof(buf), true, state);
        if (strncmp(buf, "iC75,", 5) == 0) strcpy(found, buf);
    } while (state);
    char want[32];
    snprintf(want, sizeof(want), ",%d,1Z", FILTER_FREQ);
    CHECK(found[0] && strstr(found, want), "yield_synth_commands emits it (%s)", found[0] ? found : "(missing)");
    // ...and feeding it back in works.
    restart();
    synth_2x2(1);
    wire("i1%s", found);
    clear_log();
    wire("i1v1F6400");
    CHECK(midi_writes == 1 && last_is(0xB0, 75, 127), "restored mapping sends (%d sent)", midi_writes);
}

int main(void) {
    test_basic();
    test_dedupe();
    test_round_trip();
    test_osc_matching();
    test_multiple_targets();
    test_bus_param();
    test_channel();
    test_no_feedback();
    test_output_is_not_input();
    test_template_refused();
    test_clear();
    test_state_round_trip();
    amy_stop();
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
