// Regression test for the 49.7-day millisecond-clock rollover.
//
// amy_sysclock() is u32 milliseconds, so it wraps every 2^32 ms = 49.71 days.
// Two things used to break at that instant, and neither is reachable from the
// audio-rendering test suite because you cannot render 50 days of samples --
// so this test fast-forwards amy_global.total_blocks instead.
//
//   1. sequencer_check_and_fill() anchored its 64-bit next_amy_tick_us
//      accumulator to the 32-bit clock. At the rollover now_us collapsed to ~0
//      while next_amy_tick_us stayed at ~4.3e12 us, so the catch-up guard
//      (which only handles falling *behind*) and the tick loop both went quiet
//      and the sequencer stopped forever -- taking tulip.defer() and every
//      seq callback with it.
//
//   2. The delta queue is time-sorted with a plain `>=`. A note_off scheduled
//      a few ms past the rollover has a tiny d->time while its own note_on is
//      still near 2^32, so the release sorted ahead of the attack, fired
//      first, and left the note droning forever.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "amy.h"
#include "sequencer.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

// Blocks per second of synthesized audio.
static const uint64_t BPS = AMY_SAMPLE_RATE / AMY_BLOCK_SIZE;

static uint32_t ticks_seen = 0;
static void count_tick(uint32_t t) { (void)t; ticks_seen++; }

// Park the synth clock at an arbitrary uptime without rendering our way there.
static void set_clock_ms(uint64_t ms) {
    amy_global.total_blocks = (uint32_t)((ms * AMY_SAMPLE_RATE) / (1000ULL * AMY_BLOCK_SIZE));
    amy_global.total_samples = amy_global.total_blocks * AMY_BLOCK_SIZE;
}

// Run the real render loop for `secs` of audio. amy_simple_fill_buffer() does
// execute_deltas -> render -> fill, and the fill is what advances total_blocks,
// so this drives the clock exactly the way the audio callback does. Rendering
// (not delta execution) is also what retires a finished note to SYNTH_OFF.
static uint32_t advance_secs(double secs) {
    ticks_seen = 0;
    uint64_t n = (uint64_t)(BPS * secs);
    for (uint64_t i = 0; i < n; i++) amy_simple_fill_buffer();
    return ticks_seen;
}

static int osc_audible(int osc) {
    return synth[osc] != NULL && synth[osc]->status == SYNTH_AUDIBLE;
}

static void note_on(int osc, float midi_note, uint32_t time) {
    amy_event e = amy_default_event();
    e.time = time; e.osc = osc; e.wave = SINE; e.velocity = 1.0f; e.midi_note = midi_note;
    amy_add_event(&e);
}

static void note_off(int osc, uint32_t time) {
    amy_event e = amy_default_event();
    e.time = time; e.osc = osc; e.velocity = 0;
    amy_add_event(&e);
}

// 2^32 ms -- the instant amy_sysclock() rolls over.
#define WRAP_MS 4294967296ULL

static void test_sequencer_survives_wrap(void) {
    printf("sequencer keeps ticking across the 49.7-day rollover\n");
    amy_global.config.amy_external_sequencer_hook = count_tick;

    double expected_per_10s = 10.0 * 1000000.0 / amy_global.us_per_tick;

    set_clock_ms(WRAP_MS - 30000);       // 30 s out
    sequencer_recompute();
    uint32_t before = advance_secs(10);
    CHECK(before > expected_per_10s * 0.9,
          "before wrap: %u ticks/10s (expected ~%.0f)", before, expected_per_10s);

    advance_secs(25);                     // cross it
    CHECK(amy_sysclock() < 30000, "sysclock rolled over: now %u", amy_sysclock());
    CHECK(amy_sysclock64() > WRAP_MS, "64-bit clock kept counting: %" PRIu64, amy_sysclock64());

    uint32_t after = advance_secs(10);
    CHECK(after > expected_per_10s * 0.9,
          "after wrap:  %u ticks/10s (expected ~%.0f)", after, expected_per_10s);

    uint32_t much_later = advance_secs(60);
    CHECK(much_later > expected_per_10s * 5.9,
          "wrap+1min:   %u ticks/60s (expected ~%.0f)", much_later, expected_per_10s * 6);

    amy_global.config.amy_external_sequencer_hook = NULL;
}

static void test_no_stuck_note_across_wrap(void) {
    printf("a note whose note_off lands past the rollover still stops\n");

    set_clock_ms(WRAP_MS - 500);
    uint32_t t = amy_sysclock();
    note_on(0, 64, t);
    note_off(0, t + 1000);               // this time value wraps past 2^32
    CHECK(t + 1000 < t, "note_off time wrapped as intended (on=%u off=%u)", t, t + 1000);

    advance_secs(0.2);
    CHECK(osc_audible(0), "note is sounding before the wrap");

    advance_secs(2.0);                    // well past both the wrap and the note_off
    CHECK(!osc_audible(0), "note stopped after the wrap (not stuck on)");
}

static void test_scheduling_works_after_wrap(void) {
    printf("scheduling still works once the clock has rolled over\n");

    set_clock_ms(WRAP_MS + 5000);
    note_on(1, 60, amy_sysclock());
    advance_secs(0.2);
    CHECK(osc_audible(1), "immediate note_on sounds");

    note_off(1, amy_sysclock());
    advance_secs(0.5);
    CHECK(!osc_audible(1), "immediate note_off stops it");

    note_on(2, 67, amy_sysclock() + 500);
    advance_secs(0.2);
    CHECK(!osc_audible(2), "note scheduled +500ms has not fired yet");
    advance_secs(0.6);
    CHECK(osc_audible(2), "note scheduled +500ms fired on time");
}

static void test_sustained_note_across_sample_wrap(void) {
    // amy_global.total_samples is u32 and wraps every 2^32 samples (27.05 h at
    // 44.1 kHz). Envelope math is `total_samples - note_on_clock` in u32, which
    // is modular-correct, so this should be a non-event -- pin that down.
    printf("held note is unaffected by the 27-hour total_samples wrap\n");

    uint32_t wrap_blocks = (uint32_t)((1ULL << 32) / AMY_BLOCK_SIZE);
    amy_global.total_blocks = wrap_blocks - (uint32_t)(BPS * 2);
    amy_global.total_samples = amy_global.total_blocks * AMY_BLOCK_SIZE;

    note_on(3, 69, amy_sysclock());
    advance_secs(1.0);
    CHECK(osc_audible(3), "sounding before the sample-counter wrap");
    advance_secs(3.0);
    CHECK(amy_global.total_samples < BPS * AMY_BLOCK_SIZE * 4, "total_samples wrapped");
    CHECK(osc_audible(3), "still sounding after the sample-counter wrap");
    note_off(3, amy_sysclock());
    advance_secs(1.0);
}

// examples.c calls this; the example binaries each define their own. This test
// drives the clock by hand and never sleeps, so nothing here should reach it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);

    test_sequencer_survives_wrap();
    test_no_stuck_note_across_wrap();
    test_scheduling_works_after_wrap();
    test_sustained_note_across_sample_wrap();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall clock-wrap checks passed\n");
    return 0;
}
