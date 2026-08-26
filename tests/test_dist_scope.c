// The 'G' distortion commands name a stage, not a place: the event they
// arrive on decides whether they shape an osc or a bus.  An event that names
// an osc shapes that osc; one that names none shapes the bus it addresses --
// bus=, else the bus its synth is on, else the default bus.  Nothing the
// rendering suite can hear pins that down, because both scopes sound like
// distortion; what has to be checked is which side of the mixer actually
// changed, and that the other side did not.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

void delay_ms(uint32_t ms) { (void)ms; }

extern uint16_t *voice_to_base_osc;
extern int instrument_get_num_voices(int instrument_number, uint16_t *amy_voices);
extern void set_event_for_osc(int base_osc, int rel_osc, struct amy_event *event);

static void restart(void) {
    amy_stop();
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
}

// Send one wire message and let its deltas land.
static void send(const char *message) {
    amy_add_message((char *)message);
    amy_execute_deltas();
}

static dist_config_t bus_dist(int bus) {
    return amy_global.bus[bus]->dist;
}

// Field by field: dist_config_t has padding between its members, and a
// struct copy is free to leave the padding in the copy as it found it.
static int same_dist(dist_config_t a, dist_config_t b) {
    return a.stages == b.stages && a.drive == b.drive && a.bits == b.bits
        && a.rate == b.rate && a.mix == b.mix;
}

// An osc that was never allocated is at its defaults, which for distortion
// means no stage enabled.
static uint8_t osc_dist_stages(int osc) {
    return synth[osc] == NULL ? 0 : synth[osc]->dist_stages;
}

static void test_no_osc_means_bus(void) {
    printf("bus=1 with no osc shapes bus 1, and nothing else\n");
    restart();
    dist_config_t bus0_before = bus_dist(0);

    send("y1GC1Z");
    CHECK(bus_dist(1).stages & DIST_CLIP, "bus 1 has the clip stage enabled");
    CHECK(same_dist(bus0_before, bus_dist(0)), "bus 0's distortion config is untouched");
    CHECK(osc_dist_stages(0) == 0, "osc 0 is not distorting");
}

static void test_explicit_osc_means_osc(void) {
    printf("osc=0 shapes the osc, and leaves the bus stage off\n");
    restart();

    send("v0GC1Z");
    CHECK(osc_dist_stages(0) & DIST_CLIP, "osc 0 has the clip stage enabled");
    CHECK(bus_dist(0).stages == 0, "bus 0 is not distorting");
}

static void test_bus_with_osc_is_routing(void) {
    printf("bus= alongside an osc is osc routing, as it always was\n");
    restart();

    send("v1y2GC1Z");
    CHECK(synth[1] != NULL && synth[1]->bus == 2, "osc 1 is routed to bus 2");
    CHECK(osc_dist_stages(1) & DIST_CLIP, "osc 1 has the clip stage enabled");
    CHECK(bus_dist(2).stages == 0, "bus 2 is not distorting");
}

static void test_synth_resolves_to_its_bus(void) {
    printf("synth= with no osc shapes the bus that synth is on\n");
    restart();

    // A one-voice, two-osc synth parked on bus 1.
    send("i1iv1in2y1Z");
    send("i1GC1Z");
    CHECK(bus_dist(1).stages & DIST_CLIP, "the synth's bus has the clip stage enabled");
    CHECK(bus_dist(0).stages == 0, "the default bus is untouched");
    for (int osc = 0; osc < 2; ++osc)
        if (osc_dist_stages(osc)) {
            CHECK(false, "voice osc %d is not distorting", osc);
            return;
        }
    CHECK(true, "neither of the voice's oscs is distorting");
}

static void test_bus_takes_only_the_constant_coef(void) {
    printf("a bus reads only the constant term of GD/GM\n");
    restart();

    // The modulation coefs need per-note sources a bus sum hasn't got, so the
    // bus stage takes the drive it was handed and ignores the rest of the list.
    send("y1GC1GD4,0,2GM0.5,1Z");
    CHECK(bus_dist(1).drive == 4.0f, "bus drive is the constant term (%.3f)", bus_dist(1).drive);
    CHECK(bus_dist(1).mix == 0.5f, "bus mix is the constant term (%.3f)", bus_dist(1).mix);
}

static void test_bus_state_round_trips(void) {
    printf("a distorting bus reads back as bus-scope commands\n");
    restart();

    send("y1GC1GH8,4GD4GM1Z");
    amy_event e = amy_default_event();
    set_event_for_bus_fx(&e, 1, &amy_global);
    CHECK(AMY_IS_UNSET(e.osc), "the dumped event names no osc, so it is bus-scope");
    CHECK(AMY_IS_SET(e.dist_clip) && e.dist_clip == 1, "clip comes back enabled");
    CHECK(AMY_IS_SET(e.dist_crush) && e.dist_crush == 1, "crush comes back enabled");
    CHECK(e.dist_bits == 8 && e.dist_rate == 4, "the crusher's bits,rate come back");
    CHECK(e.dist_drive_coefs[COEF_CONST] == 4.0f, "drive comes back as the constant coef");

    // And feeding the dump back in reproduces the same bus, from a clean start.
    char wire[MAX_MESSAGE_LEN];
    sprint_event(&e, wire, sizeof(wire), /* wirecode= */ true);
    dist_config_t before = bus_dist(1);
    restart();
    send(wire);
    CHECK(same_dist(before, bus_dist(1)), "replaying the dump restores the same bus stage (%s)", wire);
}

static void test_undistorted_bus_says_nothing(void) {
    printf("a bus with no stage enabled adds nothing to its dump\n");
    restart();

    amy_event e = amy_default_event();
    set_event_for_bus_fx(&e, 0, &amy_global);
    CHECK(AMY_IS_UNSET(e.dist_clip) && AMY_IS_UNSET(e.dist_fold) && AMY_IS_UNSET(e.dist_crush),
          "no stage enables are emitted");
    CHECK(AMY_IS_UNSET(e.dist_drive_coefs[COEF_CONST]) && AMY_IS_UNSET(e.dist_mix_coefs[COEF_CONST]),
          "no drive or mix is emitted");
}

// The dumped event is the inverse of the wire that built the osc, so the
// wire it prints, fed to a clean engine, has to render the same samples.
#define ROUND_TRIP_BLOCKS 8

static void render_blocks(int16_t *out, int n_blocks) {
    for (int b = 0; b < n_blocks; ++b) {
        int16_t *buf = amy_simple_fill_buffer();
        memcpy(out + b * AMY_BLOCK_SIZE * AMY_NCHANS, buf,
               AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
    }
}

static void test_osc_state_round_trips(void) {
    printf("a distorting osc reads back as osc-scope commands\n");
    restart();

    // Drive 3 is deliberately not a power of two: it goes through log2 on the
    // way in and exp2 on the way out, so the read-back is only as exact as
    // that pair.  The wire prints to 3 dp, which is what the replay parses,
    // so the sample-level comparison is the one that has to hold exactly.
    send("v0w3GC1GF1GH6,5GD3,,2GM0.7Z");
    amy_event e = amy_default_event();
    set_event_for_osc(0, 0, &e);
    CHECK(AMY_IS_SET(e.dist_clip) && e.dist_clip == 1, "clip comes back enabled");
    CHECK(AMY_IS_SET(e.dist_fold) && e.dist_fold == 1, "fold comes back enabled");
    CHECK(AMY_IS_SET(e.dist_crush) && e.dist_crush == 1, "crush comes back enabled");
    CHECK(e.dist_bits == 6 && e.dist_rate == 5, "the crusher's bits,rate come back");
    CHECK(AMY_IS_SET(e.dist_drive_coefs[COEF_CONST])
          && fabsf(e.dist_drive_coefs[COEF_CONST] - 3.0f) < 1e-3f,
          "drive comes back linear (%.6f)", e.dist_drive_coefs[COEF_CONST]);
    CHECK(e.dist_drive_coefs[COEF_VEL] == 2.0f, "the drive's velocity coef comes back as sent");
    CHECK(e.dist_mix_coefs[COEF_CONST] == 0.7f, "mix comes back");

    // Play it, then play the dump from a clean start, and compare samples.
    static int16_t original[ROUND_TRIP_BLOCKS * AMY_BLOCK_SIZE * AMY_NCHANS];
    static int16_t replayed[ROUND_TRIP_BLOCKS * AMY_BLOCK_SIZE * AMY_NCHANS];
    amy_reset_sysclock();
    send("v0n60l1Z");
    render_blocks(original, ROUND_TRIP_BLOCKS);

    e.osc = 0;  // The dump names its osc, so the G commands stay osc-scope.
    char wire[MAX_MESSAGE_LEN];
    sprint_event(&e, wire, sizeof(wire), /* wirecode= */ true);
    restart();
    amy_reset_sysclock();
    send(wire);
    send("v0n60l1Z");
    render_blocks(replayed, ROUND_TRIP_BLOCKS);
    CHECK(memcmp(original, replayed, sizeof(original)) == 0,
          "replaying the dump renders the same samples (%s)", wire);
}

static void test_undistorted_osc_says_nothing(void) {
    printf("an osc with no stage enabled adds nothing to its dump\n");
    restart();

    send("v0w3Z");
    amy_event e = amy_default_event();
    set_event_for_osc(0, 0, &e);
    CHECK(AMY_IS_UNSET(e.dist_clip) && AMY_IS_UNSET(e.dist_fold) && AMY_IS_UNSET(e.dist_crush),
          "no stage enables are emitted");
    CHECK(AMY_IS_UNSET(e.dist_bits) && AMY_IS_UNSET(e.dist_rate), "no bits or rate are emitted");
    CHECK(AMY_IS_UNSET(e.dist_drive_coefs[COEF_CONST]) && AMY_IS_UNSET(e.dist_mix_coefs[COEF_CONST]),
          "no drive or mix is emitted");
    e.osc = 0;
    char wire[MAX_MESSAGE_LEN];
    sprint_event(&e, wire, sizeof(wire), /* wirecode= */ true);
    CHECK(strchr(wire, 'G') == NULL, "the wire form carries no G command (%s)", wire);
}

static void test_osc_dist_survives_growth(void) {
    printf("growing a synth keeps its voices' distortion\n");
    restart();

    // A hand-configured synth: growth clones voice 0 through the osc readout.
    send("i1iv1in1Z");
    send("i1v0GC1GD3Z");
    send("i1iv3Z");
    CHECK(instrument_get_num_voices(1, NULL) == 3, "synth grew to 3 voices");
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    int num_voices = instrument_get_num_voices(1, voices);
    bool all_clip = true, all_drive = true;
    for (int v = 0; v < num_voices; ++v) {
        uint16_t osc = voice_to_base_osc[voices[v]];
        if (!(osc_dist_stages(osc) & DIST_CLIP))  all_clip = false;
        if (synth[osc] == NULL
            || fabsf(synth[osc]->dist_logdrive_coefs[COEF_CONST] - log2f(3.0f)) > 1e-3f)
            all_drive = false;
    }
    CHECK(all_clip, "every voice has the clip stage enabled");
    CHECK(all_drive, "every voice has drive 3");
}

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);

    test_no_osc_means_bus();
    test_explicit_osc_means_osc();
    test_bus_with_osc_is_routing();
    test_synth_resolves_to_its_bus();
    test_bus_takes_only_the_constant_coef();
    test_bus_state_round_trips();
    test_undistorted_bus_says_nothing();
    test_osc_state_round_trips();
    test_undistorted_osc_says_nothing();
    test_osc_dist_survives_growth();

    amy_stop();
    printf(failures ? "FAILURES: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
