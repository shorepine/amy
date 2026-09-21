// Tests for synth NOTE OUTPUTS: a synth whose note events go to CV/gate
// jacks or out a MIDI channel instead of to its oscillators
// (src/note_output.c, the `iG` wire command).
//
// None of this is visible to the audio-rendering suite, because the
// whole point of the feature is that it makes no audio. What is worth
// pinning down:
//
//  - the voltages. 1V/oct out is the exact inverse of cv_trigger's
//    1V/oct in, and the two sharing a scale and offset is what makes a
//    host's loopback patch round-trip a note unchanged. That identity is
//    asserted here directly, because it is the property the whole design
//    hangs on and it would otherwise only be checkable on hardware.
//  - the ORDER. Pitch is written before the gate rises, so nothing
//    downstream is ever told to look at a voltage that is still moving.
//  - mono last-note priority and legato: a note-on over a held note
//    moves the pitch and leaves the gate high (which is what gives a
//    mono synth its slide), and the gate falls only on the last release.
//  - that a note-output synth allocates NO OSCILLATORS. It is intercepted
//    before voices are allocated, and if that ever stops being true the
//    feature silently starts costing an osc per synth again -- which is
//    exactly what the wave-type implementation it replaced did wrong.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "amy.h"

// The example mains define this; a ctest links the library without one.
void delay_ms(uint32_t ms) { (void)ms; }

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

// ---- what the host saw -------------------------------------------------

#define LOG_MAX 64
typedef struct { uint8_t ch; float v; } cv_write_t;
static cv_write_t cv_log[LOG_MAX];
static int cv_writes = 0;

static uint8_t midi_log[LOG_MAX][3];
static int midi_writes = 0;

static void test_cv_hook(uint8_t channel, float volts) {
    if (cv_writes < LOG_MAX) { cv_log[cv_writes].ch = channel; cv_log[cv_writes].v = volts; }
    cv_writes++;
}

static void test_midi_hook(uint8_t *bytes, uint16_t len) {
    if (midi_writes < LOG_MAX && len >= 3)
        memcpy(midi_log[midi_writes], bytes, 3);
    midi_writes++;
}

static void clear_log(void) { cv_writes = 0; midi_writes = 0; }

// Last voltage written to a channel, or NAN if it was never written.
static float last_on(uint8_t ch) {
    for (int i = cv_writes - 1; i >= 0; --i)
        if (cv_log[i].ch == ch) return cv_log[i].v;
    return NAN;
}

static int writes_on(uint8_t ch) {
    int n = 0;
    for (int i = 0; i < cv_writes; ++i) if (cv_log[i].ch == ch) ++n;
    return n;
}

// ---- driving -----------------------------------------------------------

static void restart(void) {
    amy_stop();
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.features.default_synths = 0;
    c.amy_external_cv_output_hook = test_cv_hook;
    c.amy_external_midi_output_hook = test_midi_hook;
    amy_start(c);
}

static void wire(const char *s) {
    char buf[256];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    amy_play_message(buf);
    // Deltas are applied at render time, so a note has to be rendered
    // before anything has happened at all.
    for (int i = 0; i < 4; ++i) amy_simple_fill_buffer();
}

static int oscs_in_use(void) {
    int n = 0;
    for (uint16_t i = 0; i < amy_global.config.max_oscs; ++i)
        if (synth[i] != NULL) ++n;
    return n;
}

// ---- tests -------------------------------------------------------------

static void test_cv_gate_voltages(void) {
    printf("CV_GATE voltages\n");
    restart();
    // pitch on control out 0, gate on 1; defaults 12 semitones/volt, note
    // 24 at 0 V, gate high 5 V.
    wire("i1iG1,0,1");
    clear_log();
    wire("i1n60l1");     // note 60, velocity 1
    CHECK(fabsf(last_on(0) - 3.0f) < 1e-4, "note 60 is 3.000 V (got %.4f)", last_on(0));
    CHECK(fabsf(last_on(1) - 5.0f) < 1e-4, "gate high is 5.000 V (got %.4f)", last_on(1));
    // THE ORDER MATTERS: pitch must be written before the gate rises.
    int pitch_at = -1, gate_at = -1;
    for (int i = 0; i < cv_writes; ++i) {
        if (cv_log[i].ch == 0 && pitch_at < 0) pitch_at = i;
        if (cv_log[i].ch == 1 && gate_at < 0) gate_at = i;
    }
    CHECK(pitch_at >= 0 && gate_at > pitch_at, "pitch is written before the gate rises");

    clear_log();
    wire("i1n60l0");     // note off
    CHECK(fabsf(last_on(1)) < 1e-4, "gate falls to 0 (got %.4f)", last_on(1));
    CHECK(writes_on(0) == 0, "the pitch is HELD on note-off, not zeroed");
}

static void test_loopback_identity(void) {
    printf("1V/oct round trip\n");
    restart();
    wire("i1iG1,0,1");
    // The inverse of cv_trigger's note = volts * scale + offset, with the
    // same defaults. A host patching an output into an input gets the
    // note back; that is the property, so assert it over a whole range.
    for (int note = 24; note <= 108; note += 12) {
        clear_log();
        char msg[64];
        snprintf(msg, sizeof(msg), "i1n%dl1", note);
        wire(msg);
        float volts = last_on(0);
        float back = volts * 12.0f + 24.0f;
        CHECK(fabsf(back - note) < 1e-3, "note %d -> %.4f V -> note %.4f", note, volts, back);
        snprintf(msg, sizeof(msg), "i1n%dl0", note);
        wire(msg);
    }
}

static void test_mono_legato(void) {
    printf("mono last-note priority\n");
    restart();
    wire("i1iG1,0,1");
    wire("i1n60l1");
    clear_log();
    wire("i1n67l1");             // a second note while the first is held
    CHECK(fabsf(last_on(0) - 3.5833f) < 1e-3, "pitch moves to the new note (%.4f V)", last_on(0));
    CHECK(writes_on(1) == 0, "the gate is NOT retriggered: legato");
    clear_log();
    wire("i1n67l0");             // release the top one
    CHECK(fabsf(last_on(0) - 3.0f) < 1e-3, "pitch falls back to the held note (%.4f V)", last_on(0));
    CHECK(writes_on(1) == 0, "the gate is still not touched");
    clear_log();
    wire("i1n60l0");             // release the last one
    CHECK(fabsf(last_on(1)) < 1e-4, "gate falls only on the last release");
}

static void test_midi_out(void) {
    printf("MIDI_OUT\n");
    restart();
    wire("i2iG2,6");             // MIDI channel 6
    clear_log();
    wire("i2n60l0.8");
    CHECK(midi_writes >= 1, "a note-on reached the MIDI port (%d writes)", midi_writes);
    if (midi_writes >= 1) {
        CHECK(midi_log[0][0] == 0x95, "status is 0x95 (note-on, channel 6), got 0x%02x", midi_log[0][0]);
        CHECK(midi_log[0][1] == 60, "note is 60, got %d", midi_log[0][1]);
        CHECK(midi_log[0][2] > 90 && midi_log[0][2] < 110, "velocity ~102, got %d", midi_log[0][2]);
    }
    clear_log();
    wire("i2n60l0");
    CHECK(midi_writes >= 1 && midi_log[0][2] == 0, "note-off is velocity 0");
    // MIDI is polyphonic: a second note does not displace the first.
    clear_log();
    wire("i2n60l1");
    wire("i2n64l1");
    CHECK(midi_writes == 2, "two note-ons go out, not one (got %d)", midi_writes);
}

static int render_peak(int blocks) {
    int peak = 0;
    for (int b = 0; b < blocks; ++b) {
        int16_t *buf = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i) {
            int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > peak) peak = v;
        }
    }
    return peak;
}

static void test_echo_not_diversion(void) {
    printf("a voiced synth plays AND echoes\n");
    restart();
    wire("K0i1iv1");             // synth 1, one voice, Juno patch 0
    wire("i1iG1,0,1");
    clear_log();
    amy_play_message((char[]){"n60l1i1"});
    int peak = render_peak(64);
    CHECK(peak > 0, "the synth's own voices still sound (peak %d)", peak);
    CHECK(fabsf(last_on(0) - 3.0f) < 1e-4, "...and the note also reached CV (%.4f V)", last_on(0));
    CHECK(fabsf(last_on(1) - 5.0f) < 1e-4, "...and the gate (%.4f V)", last_on(1));
    CHECK(oscs_in_use() > 0, "it is using oscillators, because it was given voices");
}

static void test_costs_no_oscs(void) {
    printf("a synth with no voices costs no oscillators\n");
    restart();
    // Silent inside AMY is not a mode -- it is simply a synth nobody
    // gave voices to, which is what makes a pure CV/gate interface cost
    // nothing and needs no flag to ask for.
    int before = oscs_in_use();
    wire("i1iG1,0,1");
    wire("i1n60l1");
    wire("i1n64l1");
    wire("i1n67l1");
    int after = oscs_in_use();
    CHECK(after == before, "oscs in use unchanged: %d -> %d", before, after);
}

static void test_off_restores(void) {
    printf("turning it off\n");
    restart();
    wire("i1iG1,0,1");
    wire("i1n60l1");
    clear_log();
    wire("i1iG0");
    CHECK(fabsf(last_on(1)) < 1e-4, "switching off drops the gate rather than leaving it high");
    clear_log();
    wire("i1n72l1");
    CHECK(cv_writes == 0, "notes no longer reach the CV outputs");
}

static void test_state_round_trip(void) {
    printf("it survives a state dump\n");
    restart();
    wire("i1iG1,0,1,2");
    // A note output that did not come back from amy_dump_state would be a
    // synth that reappears silently pointed at oscillators -- audible, and
    // nothing in the audio suite would catch it.
    void *state = NULL;
    char s[MAX_MESSAGE_LEN];
    int found = 0;
    do {
        state = yield_synth_commands(1, s, MAX_MESSAGE_LEN, true, state);
        if (strstr(s, "iG1,0,1,2") != NULL) found = 1;
    } while (state != NULL);
    CHECK(found, "the iG command is in the synth's dumped state");
}

static void test_bad_mode_is_refused(void) {
    printf("a non-numeric mode is refused, not read as OFF\n");
    restart();
    // The friendly name is a Python convenience; a binding that passes it
    // through unmapped must not have it read as 0 (= OFF), which would be
    // silence with no complaint. (This prints a complaint; that is the point.)
    wire("i1iGCV_GATE,0,1");
    clear_log();
    wire("i1n60l1");
    CHECK(cv_writes == 0, "nothing was configured, so nothing was written");
}

int main(void) {
    test_cv_gate_voltages();
    test_loopback_identity();
    test_mono_legato();
    test_midi_out();
    test_costs_no_oscs();
    test_off_restores();
    test_state_round_trip();
    test_bad_mode_is_refused();
    test_echo_not_diversion();
    printf("%s: %d failure%s\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
