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
#include <stdint.h>
#include "amy.h"

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

    amy_stop();
    printf(failures ? "FAILURES: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
