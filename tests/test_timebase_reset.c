// Tests reset=RESET_TIMEBASE: when it takes effect, and what happens to
// events that were already scheduled when it arrives.
//
// The reset used to zero total_blocks / total_samples / sequencer_tick_count
// in the wire parse, on whatever thread called amy_add_message(). The render
// thread could be inside sequencer_check_and_fill(), which computes now_us
// against the old clock and then does an unguarded read-modify-write of
// next_amy_tick_us -- so the catch-up loop saw a stale now_us seconds past a
// freshly re-anchored deadline and replayed thousands of ticks in one block,
// smearing every periodic sequence into a roll. That race can't be pinned by
// a single-threaded test; what this file pins is the observable contract that
// came out of fixing it:
//
//   1. The reset is applied between audio blocks, not at parse time. Until the
//      next block the clock still reads the old timeline. (This is deliberate:
//      masking the reads to fake an instantaneous reset cost more machinery
//      than it was worth.)
//   2. Events already queued with an absolute time keep their RELATIVE timing
//      across the reset -- a delta due 200 ms from now is still due 200 ms
//      from now, not stranded a whole uptime into the future.
//   3. An event that was already due is not lost across a reset. (In practice
//      it is flushed on the parse side before the reset lands, so this one
//      passes with or without the rebase in 2 -- it guards the contract, not
//      the arithmetic.)
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

extern void set_event_for_osc(int base_osc, int rel_osc, struct amy_event *event);

#define MS_PER_BLOCK ((AMY_BLOCK_SIZE * 1000.0f) / AMY_SAMPLE_RATE)

static void render_ms(int ms) {
    int blocks = (int)(ms / MS_PER_BLOCK) + 1;
    for (int i = 0; i < blocks; ++i)  amy_simple_fill_buffer();
}

// osc 0's wave, as the synth readout reports it: AMY_UNSET while the osc is
// still at its default (SINE), the set value once our scheduled delta lands.
static int osc0_wave(void) {
    amy_event e;
    amy_clear_event(&e);
    set_event_for_osc(0, 0, &e);
    return AMY_IS_SET(e.wave) ? (int)e.wave : -1;
}

// Queue a wave change on osc 0 at an absolute time. There is no wire code for
// `time`, so this has to go through the C event API.
static void schedule_wave_at(uint32_t when, int wave) {
    amy_event e = amy_default_event();
    e.time = when;
    e.osc = 0;
    e.wave = (uint8_t)wave;
    amy_add_event(&e);
}

// Render a block at a time until osc 0's wave changes, up to a limit.
// Returns the clock reading when it landed, or -1 if it never did.
static int64_t render_until_wave(int wave, int max_ms) {
    int blocks = (int)(max_ms / MS_PER_BLOCK) + 1;
    for (int i = 0; i < blocks; ++i) {
        amy_simple_fill_buffer();
        if (osc0_wave() == wave)  return (int64_t)amy_sysclock();
    }
    return -1;
}

static void test_reset_applies_at_a_block_boundary(void) {
    printf("the reset lands between blocks, not in the parse\n");
    render_ms(500);
    uint32_t before = amy_sysclock();
    CHECK(before >= 400, "clock has run on (%" PRIu32 " ms)", before);
    amy_add_message((char *)"S16384Z");
    CHECK(amy_sysclock() == before,
          "still the old timeline right after the message (%" PRIu32 " ms)", amy_sysclock());
    amy_simple_fill_buffer();
    // The counters are zeroed at the TOP of the block, so once that block has
    // been rendered the clock reads one block, not zero.
    CHECK(amy_sysclock() <= (uint32_t)(2 * MS_PER_BLOCK),
          "and restarted after one block (%" PRIu32 " ms, was %" PRIu32 ")",
          amy_sysclock(), before);
}

static void test_queued_event_keeps_its_relative_time(void) {
    printf("a queued event keeps its distance from now across the reset\n");
    amy_add_message((char *)"S1Z");          // reset osc 0 back to defaults
    render_ms(600);
    uint32_t now = amy_sysclock();
    CHECK(now >= 500, "clock has run on again (%" PRIu32 " ms)", now);
    schedule_wave_at(now + 200, SAW_DOWN);
    amy_add_message((char *)"S16384Z");
    // On the new timeline it should be due at ~200 ms, NOT at ~now+200 (which
    // is what a reset that left delta times alone would leave behind, stranding
    // it most of a second out) and NOT immediately.
    int64_t landed = render_until_wave(SAW_DOWN, 600);
    CHECK(landed >= 0, "the event survived the reset and played");
    CHECK(landed >= 150 && landed <= 260,
          "it played ~200 ms into the new timeline (%lld ms)", (long long)landed);
}

static void test_already_due_event_is_not_lost(void) {
    printf("an event that was already due is not lost across the reset\n");
    amy_add_message((char *)"S1Z");
    render_ms(300);
    // Due in the past: queue it for a time that has already gone by. The reset
    // message that follows flushes due deltas on the way in, so this plays
    // before the reset is applied at all -- which is the point: it isn't
    // stranded or dropped.
    schedule_wave_at(amy_sysclock() - 50, TRIANGLE);
    amy_add_message((char *)"S16384Z");
    int64_t landed = render_until_wave(TRIANGLE, 200);
    CHECK(landed >= 0 && landed < 50,
          "played immediately on the new timeline (%lld ms)", (long long)landed);
}

static void test_reset_via_the_c_event_api(void) {
    printf("reset=RESET_TIMEBASE works through amy_add_event(), not just the wire\n");
    // RESET_TIMEBASE used to be handled only where a wire string is turned
    // into an event, so a C caller who set reset_osc on an amy_event had it
    // silently dropped: the RESET_OSC delta reached play_delta, which knew
    // nothing about the bit, and the clock just kept running. The only way in
    // was to call amy_reset_sysclock() by hand.
    render_ms(400);
    uint32_t before = amy_sysclock();
    CHECK(before >= 300, "clock has run on (%" PRIu32 " ms)", before);
    amy_event e = amy_default_event();
    e.reset_osc = RESET_TIMEBASE;
    amy_add_event(&e);
    amy_simple_fill_buffer();
    amy_simple_fill_buffer();
    CHECK(amy_sysclock() < before / 2,
          "the clock restarted (%" PRIu32 " ms, was %" PRIu32 ")", amy_sysclock(), before);
}

static void test_scheduled_reset_waits_for_its_time(void) {
    printf("a reset with time= happens at that time, like any other event\n");
    render_ms(400);
    uint32_t now = amy_sysclock();
    amy_event e = amy_default_event();
    e.time = now + 200;
    e.reset_osc = RESET_TIMEBASE;
    amy_add_event(&e);
    render_ms(100);
    CHECK(amy_sysclock() > now, "not reset yet at +100 ms (%" PRIu32 " ms)", amy_sysclock());
    render_ms(150);
    CHECK(amy_sysclock() < 200, "reset by +250 ms (%" PRIu32 " ms)", amy_sysclock());
}

static void test_combined_reset_bits_all_take_effect(void) {
    printf("a reset bit handled in the parse doesn't swallow the ones that aren't\n");
    // The parse handles RESET_AMY / RESET_EVENTS / RESET_SYNTHS itself and
    // then used to unset the WHOLE field, so anything combined with them never
    // reached play_delta -- RESET_EVENTS|RESET_ALL_OSCS reset the queue but
    // left every osc exactly as it was.
    amy_add_message((char *)"v0w2f550Z");
    render_ms(20);
    CHECK(osc0_wave() == SAW_DOWN, "osc 0 set to wave 2");
    char m[32];
    snprintf(m, sizeof(m), "S%dZ", RESET_EVENTS | RESET_ALL_OSCS);
    amy_add_message(m);
    render_ms(20);
    CHECK(osc0_wave() != SAW_DOWN, "RESET_ALL_OSCS still took effect alongside RESET_EVENTS");
}

// AMY calls these; the test binary has to provide them.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    amy_simple_fill_buffer();

    test_reset_applies_at_a_block_boundary();
    test_queued_event_keeps_its_relative_time();
    test_already_due_event_is_not_lost();
    test_reset_via_the_c_event_api();
    test_scheduled_reset_waits_for_its_time();
    test_combined_reset_bits_all_take_effect();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
