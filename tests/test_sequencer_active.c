// The sequencer's per-tick cost should track active work, not the numeric value
// of a public tag. Tagged definitions are stored separately from the small
// anonymous direct-scheduling pool, and active executions occupy a bounded
// pool. Consequently one sequence at tag 0 and one at tag max-1 have the same
// scan cost.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include "amy.h"
#include "sequencer.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

#define MAX_TAGS 4096

static const uint64_t BPS = AMY_SAMPLE_RATE / AMY_BLOCK_SIZE;

static void advance_secs(double secs) {
    uint64_t n = (uint64_t)(BPS * secs);
    for (uint64_t i = 0; i < n; i++) amy_simple_fill_buffer();
}

// A repeating sequence at `tag` that turns `osc` on every 16 ticks.
static void seq_note_on(int32_t tag, int osc) {
    amy_event e = amy_default_event();
    e.osc = osc;
    e.wave = SINE;
    e.velocity = 1.0f;
    e.midi_note = 60;
    e.ticks[TICKS_TICK] = 0;
    e.ticks[TICKS_PERIOD] = 16;
    e.ticks[TICKS_TAG] = (uint32_t)tag;
    amy_add_event(&e);
    sequencer_sequence_control((uint32_t)tag, SEQUENCE_CONTROL_START, 0, 0);
}

// Stop active playback, then clear the future definition.
static void seq_clear(int32_t tag) {
    sequencer_sequence_control((uint32_t)tag, SEQUENCE_CONTROL_STOP, 0, 0);
    amy_event e = amy_default_event();
    e.ticks[TICKS_TICK] = 0;
    e.ticks[TICKS_PERIOD] = 0;
    e.ticks[TICKS_TAG] = (uint32_t)tag;
    amy_add_event(&e);
}

static void all_off(void) {
    for (int osc = 0; osc < 3; osc++) {
        amy_event e = amy_default_event();
        e.osc = osc;
        e.velocity = 0;
        amy_add_event(&e);
    }
    advance_secs(0.2);
}

static int audible(int osc) {
    return synth[osc] != NULL && synth[osc]->status == SYNTH_AUDIBLE;
}

// Tags added out of order all fire, and clearing one leaves the others.
static void test_out_of_order_and_clear(void) {
    printf("tags added out of order all fire, and clear removes only one\n");
    sequencer_reset();

    seq_note_on(500, 0);      // deliberately not ascending, and not dense
    seq_note_on(3, 1);
    seq_note_on(4000, 2);
    advance_secs(1.0);
    CHECK(audible(0) && audible(1) && audible(2),
          "all three fired (tags 500, 3, 4000 added in that order)");

    // Clear the middle one, then silence everything. The two that are
    // still scheduled retrigger themselves; the cleared one has nothing
    // left to turn it back on, which is the whole assertion. (Silencing
    // first and checking for quiet does NOT work — these repeat every 16
    // ticks and turn straight back on.)
    seq_clear(3);
    all_off();
    advance_secs(1.0);
    CHECK(audible(0) && audible(2), "the two still scheduled fired again");
    CHECK(!audible(1), "the cleared one stayed silent");
    seq_clear(500);
    seq_clear(4000);
    all_off();
}

// Anonymous entries (1- or 2-value ticks=, no tag) use a separate pool. They
// should fire once, disappear, and leave no lasting per-tick cost behind.
static void test_anonymous_one_shots(void) {
    printf("anonymous one-shots fire once and leave the list empty\n");
    sequencer_reset();

    // A one-shot at an absolute tick, no tag: wire form "H<tick>".
    char msg[64];
    snprintf(msg, sizeof(msg), "H%" PRIu32 "v0w0n60l1Z", sequencer_ticks() + 8);
    amy_add_message(msg);
    advance_secs(0.5);
    CHECK(audible(0), "the anonymous one-shot fired");
    all_off();
    advance_secs(0.5);
    CHECK(!audible(0), "...and only once");

    extern int32_t first_active;
    CHECK(first_active == -1, "after it fired, nothing is scheduled at all");
}

// The invariant: a lone sequence costs the same wherever it sits.
//
// Measured at a HIGH TEMPO on purpose. At the default ~108 BPM the
// sequencer ticks about 86 times a second, and the scan is then a rounding
// error next to actually rendering the audio — the old sweep over 4096
// entries measured only ~1.5x, which is real but too close to call on a
// loaded machine. Cranking the tempo runs the scan ~28x more often per
// rendered second without changing anything else, which is exactly the
// term under test.
static uint32_t ticks_seen;
static void count_tick(uint32_t t) { (void)t; ticks_seen++; }

static double cost_of_tag(int32_t tag) {
    sequencer_reset();
    seq_note_on(tag, 0);
    advance_secs(0.2);                       // warm
    ticks_seen = 0;
    clock_t c = clock();
    advance_secs(5.0);
    c = clock() - c;
    printf("       (tag %" PRIi32 ": %u ticks)\n", tag, ticks_seen);
    seq_clear(tag);
    all_off();
    return (double)c / CLOCKS_PER_SEC;
}

static void test_cost_is_independent_of_tag(void) {
    printf("a high tag costs no more than a low one\n");

    amy_global.config.amy_external_sequencer_hook = count_tick;
    float was = amy_global.tempo;
    amy_global.tempo = 3000.0f;              // ~2400 ticks/sec
    sequencer_recompute();

    double low = cost_of_tag(0);
    double high = cost_of_tag(MAX_TAGS - 1);

    amy_global.tempo = was;
    sequencer_recompute();
    amy_global.config.amy_external_sequencer_hook = NULL;

    printf("       tag 0: %.3fs   tag %d: %.3fs   ratio %.2fx\n",
           low, MAX_TAGS - 1, high, low > 0 ? high / low : 0.0);
    CHECK(low > 0 && high < low * 2.0,
          "a sequence at tag %d costs about what one at tag 0 costs",
          MAX_TAGS - 1);
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.max_sequencer_tags = MAX_TAGS;
    amy_start(c);

    test_out_of_order_and_clear();
    test_anonymous_one_shots();
    test_cost_is_independent_of_tag();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall sequencer active-list checks passed\n");
    return 0;
}
