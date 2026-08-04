// Tests for the runtime bus count (amy_config.max_buses) and bus range checks.
//
// AMY_NUM_BUSES used to be a compile-time 4; it's now amy_config.max_buses,
// clamped into 1..AMY_MAX_BUSES at amy_start. Two things are worth pinning
// down, neither of which the audio-rendering suite can see:
//
//  - the configured count is honored, and out-of-range requests are clamped
//    rather than allocating (or indexing) past AMY_MAX_BUSES;
//  - a bus number outside the configured range is refused. Bus numbers come
//    in unchecked from the API and the wire protocol ('y9' is an atoi), and
//    every one of them subscripts amy_global.bus[] and fbl[], so before the
//    range check "y9" on a 4-bus build wrote off the end of both.
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

static void restart_with_buses(int max_buses) {
    amy_stop();
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.max_buses = max_buses;
    amy_start(c);
}

static void render_a_bit(void) {
    for (int i = 0; i < 64; ++i) amy_simple_fill_buffer();
}

// Park an osc on a bus and see where it actually landed.
static int osc_bus_after_send(int osc, int bus) {
    amy_event e = amy_default_event();
    e.osc = osc;
    e.bus = bus;
    e.wave = SINE;
    e.velocity = 1.0f;
    e.midi_note = 60;
    amy_add_event(&e);
    amy_execute_deltas();
    return synth[osc] == NULL ? -1 : synth[osc]->bus;
}

static void test_config_is_honored(void) {
    printf("max_buses is honored and clamped (AMY_MAX_BUSES = %d)\n", AMY_MAX_BUSES);

    restart_with_buses(AMY_DEFAULT_NUM_BUSES);
    CHECK(AMY_NUM_BUSES == AMY_DEFAULT_NUM_BUSES,
          "the default is %d buses", AMY_DEFAULT_NUM_BUSES);

    restart_with_buses(AMY_MAX_BUSES);
    CHECK(AMY_NUM_BUSES == AMY_MAX_BUSES, "max_buses = AMY_MAX_BUSES is taken as-is");
    for (int bus = 0; bus < AMY_MAX_BUSES; ++bus)
        CHECK(amy_global.bus[bus] != NULL, "bus %d state is allocated", bus);

    restart_with_buses(1);
    CHECK(AMY_NUM_BUSES == 1, "max_buses = 1 is taken as-is");

    // Nonsense in, no out-of-bounds allocation out.
    restart_with_buses(AMY_MAX_BUSES + 7);
    CHECK(AMY_NUM_BUSES == AMY_MAX_BUSES, "over-max is clamped to AMY_MAX_BUSES");
    restart_with_buses(0);
    CHECK(AMY_NUM_BUSES == AMY_DEFAULT_NUM_BUSES,
          "0 (e.g. a hand-rolled config that never set it) falls back to the default");
}

static void test_bus_range_is_enforced(void) {
    restart_with_buses(2);
    printf("bus numbers are range-checked (max_buses = %d)\n", AMY_NUM_BUSES);

    CHECK(osc_bus_after_send(0, 0) == 0, "osc on bus 0 stays on bus 0");
    CHECK(osc_bus_after_send(1, 1) == 1, "osc on bus 1 stays on bus 1");
    // Bus 2 exists in the AMY_MAX_BUSES-wide tables but was never allocated.
    CHECK(osc_bus_after_send(2, 2) == AMY_DEFAULT_BUS,
          "osc on out-of-range bus 2 falls back to bus %d", AMY_DEFAULT_BUS);
    CHECK(osc_bus_after_send(3, AMY_MAX_BUSES + 4) == AMY_DEFAULT_BUS,
          "osc on a bus past AMY_MAX_BUSES falls back to bus %d", AMY_DEFAULT_BUS);

    // Same over the wire, which is how a bad bus number usually arrives.
    amy_add_message("v4y9w0n64l1Z");
    amy_execute_deltas();
    CHECK(synth[4] != NULL && synth[4]->bus == AMY_DEFAULT_BUS,
          "wire 'y9' falls back to bus %d", AMY_DEFAULT_BUS);

    // Bus-directed FX for a bus that doesn't exist must not write through a
    // null bus_state pointer. Rendering afterwards proves nothing tripped.
    amy_add_message("y5h1Z");   // reverb on bus 5
    amy_add_message("y5M0.5Z"); // echo on bus 5
    amy_execute_deltas();
    render_a_bit();
    CHECK(amy_global.highest_bus < AMY_NUM_BUSES,
          "highest_bus (%d) stays inside the configured range", amy_global.highest_bus);

    // An instrument's bus is stored and read back later to index the bus
    // tables (set_event_for_bus_fx), so it gets checked on the way in too --
    // both when the instrument is created and when its bus is changed.
    amy_event e = amy_default_event();
    e.synth = 1;
    e.num_voices = 2;
    e.patch_number = 0;
    e.bus = 6;                    // new synth 1, on a bus that doesn't exist
    amy_add_event(&e);
    amy_execute_deltas();
    CHECK(instrument_get_bus(1) == AMY_DEFAULT_BUS,
          "a synth created on out-of-range bus 6 gets bus %d", AMY_DEFAULT_BUS);
    e = amy_default_event();
    e.synth = 1;
    e.bus = 7;                    // ...and move it to another one
    amy_add_event(&e);
    amy_execute_deltas();
    CHECK(instrument_get_bus(1) == AMY_DEFAULT_BUS,
          "moving that synth to out-of-range bus 7 leaves it on bus %d", AMY_DEFAULT_BUS);
    render_a_bit();
}

// Every configured bus has to survive a real render with its FX switched on:
// this is what would fall over if the per-bus tables and the allocation loops
// ever disagreed about how many buses there are.
static void test_all_buses_render(void) {
    restart_with_buses(AMY_MAX_BUSES);
    printf("all %d buses render with FX on\n", AMY_NUM_BUSES);

    for (int bus = 0; bus < AMY_NUM_BUSES; ++bus) {
        char msg[128];
        snprintf(msg, sizeof(msg), "v%dy%dw0n%dl1Z", bus, bus, 60 + bus);
        amy_add_message(msg);
        snprintf(msg, sizeof(msg), "y%dh0.5k0.5M0.3Z", bus);  // reverb, chorus, echo
        amy_add_message(msg);
    }
    amy_execute_deltas();
    render_a_bit();

    CHECK(amy_global.highest_bus == AMY_NUM_BUSES - 1,
          "highest_bus reached the top bus (%d)", AMY_NUM_BUSES - 1);
    for (int bus = 0; bus < AMY_NUM_BUSES; ++bus)
        CHECK(amy_global.bus[bus]->reverb.level > 0, "bus %d kept its reverb", bus);
    CHECK(amy_get_oom_count() == 0, "no allocation failures");
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);

    test_config_is_honored();
    test_bus_range_is_enforced();
    test_all_buses_render();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall bus config checks passed\n");
    return 0;
}
