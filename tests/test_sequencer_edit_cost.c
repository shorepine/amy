// Editing a tag should cost what is SCHEDULED, not how big the pool is.
//
// Since events accumulate on a tag, an edit is "clear the tag, re-add its
// events", under the lock the render path also takes.
//
// The headline check is an invariant, as in test_sequencer_active: the same
// edit with the same events scheduled costs about the same in a 256-slot
// pool and a 16384-slot pool. The rest checks that the faster clear and
// alloc still keep the pool's bookkeeping right: a full pool refuses, a
// cleared or fired slot is reused, and a clear takes exactly its own tag.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include "amy.h"
#include "sequencer.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

static const uint64_t BPS = AMY_SAMPLE_RATE / AMY_BLOCK_SIZE;

static void advance_secs(double secs) {
    uint64_t n = (uint64_t)(BPS * secs);
    for (uint64_t i = 0; i < n; i++) amy_simple_fill_buffer();
}

static void restart_with_pool(int max_tags) {
    amy_stop();
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.max_sequencer_tags = max_tags;
    amy_start(c);
}

// A repeating event at `tag` that does nothing audible; it is never due
// while the edit loop below runs, because nothing renders there.
static uint8_t add_periodic(uint32_t tag) {
    return sequencer_add_wire(0, 96, tag, true, strdup("v0w0n60l0"));
}

// A one-shot due a few ticks from now.
static uint8_t add_one_shot(uint32_t tag) {
    return sequencer_add_wire(sequencer_ticks() + 4, 0, tag, true, strdup("v0w0n60l0"));
}

// Seconds for `edits` clear-and-re-add cycles of a 4-event pattern on tag 20,
// with 16 other events resident on tags 0..15.
static double edit_cost(int edits) {
    sequencer_reset();
    for (uint32_t t = 0; t < 16; t++) add_periodic(t);
    for (int k = 0; k < 4; k++) add_periodic(20);
    clock_t c = clock();
    for (int i = 0; i < edits; i++) {
        sequencer_clear_tag(20);
        for (int k = 0; k < 4; k++) add_periodic(20);
    }
    c = clock() - c;
    sequencer_reset();
    return (double)c / CLOCKS_PER_SEC;
}

// Best of alternating runs per pool size: load on the machine only ever slows
// a run down, so the fastest is the least disturbed one.
static void test_edit_cost_is_independent_of_pool(void) {
    printf("an edit costs no more in a big pool than in a small one\n");
    const int edits = 100000, runs = 5;
    double small = 0, big = 0;
    for (int r = 0; r < runs; r++) {
        restart_with_pool(256);
        double s = edit_cost(edits);
        restart_with_pool(16384);
        double b = edit_cost(edits);
        if (r == 0 || s < small) small = s;
        if (r == 0 || b < big) big = b;
    }
    printf("       256 slots: %.3fs   16384 slots: %.3fs   ratio %.2fx\n",
           small, big, small > 0 ? big / small : 0.0);
    CHECK(small > 0 && big < small * 2.0,
          "the same edit with the same events scheduled costs about the same");
}

static void test_full_pool(void) {
    printf("a full pool refuses, and a clear makes room at once\n");
    restart_with_pool(64);
    int added = 0;
    for (uint32_t t = 0; t < 64; t++) added += add_periodic(t % 8);
    CHECK(added == 64, "64 events fill a 64-slot pool (%d added)", added);
    CHECK(add_periodic(9) == 0, "the 65th is refused");
    sequencer_clear_tag(3);                  // 8 events on tag 3
    added = 0;
    for (int k = 0; k < 8; k++) added += add_periodic(40);
    CHECK(added == 8, "after clearing a tag of 8, 8 new events fit (%d added)", added);
    CHECK(add_periodic(41) == 0, "and the pool is full again");
    sequencer_reset();
    added = 0;
    for (uint32_t t = 0; t < 64; t++) added += add_periodic(t);
    CHECK(added == 64, "after a reset the whole pool is free (%d added)", added);
    sequencer_reset();
}

static void test_fired_one_shots_free_their_slots(void) {
    printf("one-shots that fire give their slots back\n");
    restart_with_pool(64);
    int added = 0;
    for (uint32_t t = 0; t < 64; t++) added += add_one_shot(t);
    CHECK(added == 64, "64 one-shots fill the pool (%d added)", added);
    advance_secs(0.5);
    added = 0;
    for (uint32_t t = 0; t < 64; t++) added += add_periodic(t);
    CHECK(added == 64, "after they fire, 64 new events fit (%d added)", added);
    sequencer_reset();
}

static int audible(int osc) {
    return synth[osc] != NULL && synth[osc]->status == SYNTH_AUDIBLE;
}

static void note_on_at(uint32_t tag, int osc) {
    char msg[48];
    snprintf(msg, sizeof(msg), "v%dw0n60l1", osc);
    sequencer_add_wire(0, 16, tag, true, strdup(msg));
}

static void all_off(void) {
    for (int osc = 0; osc < 4; osc++) {
        amy_event e = amy_default_event();
        e.osc = osc;
        e.velocity = 0;
        amy_add_event(&e);
    }
    advance_secs(0.2);
}

static void test_clear_takes_only_its_tag(void) {
    printf("a clear takes every event of its tag and nothing else\n");
    restart_with_pool(256);
    note_on_at(7, 0);       // tag 7 carries osc 0 and osc 1, added apart
    note_on_at(3, 2);
    note_on_at(7, 1);
    note_on_at(9, 3);
    advance_secs(1.0);
    CHECK(audible(0) && audible(1) && audible(2) && audible(3), "all four fired");
    sequencer_clear_tag(7);
    all_off();
    advance_secs(1.0);
    CHECK(!audible(0) && !audible(1), "both events of the cleared tag stayed silent");
    CHECK(audible(2) && audible(3), "the tags either side of it fired again");
    sequencer_clear_tag(3);
    sequencer_clear_tag(9);
    sequencer_clear_tag(7);   // clearing an empty tag is a no-op
    all_off();
    advance_secs(0.5);
    CHECK(!audible(2) && !audible(3), "and once cleared they are silent too");
    extern int32_t first_active;
    CHECK(first_active == -1, "nothing is left scheduled");
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);

    test_clear_takes_only_its_tag();
    test_full_pool();
    test_fired_one_shots_free_their_slots();
    test_edit_cost_is_independent_of_pool();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall sequencer edit-cost checks passed\n");
    return 0;
}
