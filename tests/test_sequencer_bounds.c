// Regression test for the sequencer tag bounds check.
//
// `sequences` is a malloc'd array of max_sequences entries, so the last
// valid tag is max_sequences - 1. sequencer_add_event() guarded with
//
//     if (tag > max_sequences)
//
// which let tag == max_sequences through and wrote a whole
// sequence_info_t (a pointer and two uint32_t) one element past the end
// of the allocation. The message it prints on rejection has always said
// "greater than or eq", so the intent was never in doubt.
//
// Not reachable from the audio-rendering suite, which never sends a tag
// near the ceiling -- and not reachable from a well-behaved host either,
// since an allocator that hands out tags below the limit can't produce
// one. It takes exactly one hand-written message:
//
//     amy.send(sequence="0,16,256")     # with max_sequencer_tags 256
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

#define MAX_TAGS 64

// A schedulable event: a period is what makes sequencer_add_event keep it
// rather than returning 0 for "nothing to schedule here".
static uint8_t add_at_tag(int32_t tag) {
    amy_event e = amy_default_event();
    e.osc = 0;
    e.wave = SINE;
    e.velocity = 1.0f;
    e.midi_note = 60;
    e.sequence[SEQUENCE_TICK] = 0;
    e.sequence[SEQUENCE_PERIOD] = 16;
    e.sequence[SEQUENCE_TAG] = (uint32_t)tag;
    return sequencer_add_event(&e);
}

static void test_tag_bounds(void) {
    printf("sequencer tag bounds (max_sequences = %d)\n", MAX_TAGS);

    CHECK(add_at_tag(0) == 1, "tag 0 is accepted");
    CHECK(add_at_tag(MAX_TAGS - 1) == 1,
          "tag max-1 (%d) is accepted -- the last valid slot", MAX_TAGS - 1);

    // The bug: this one used to be written, one element past the end.
    CHECK(add_at_tag(MAX_TAGS) == 0,
          "tag max (%d) is REJECTED, not written past the end", MAX_TAGS);
    CHECK(add_at_tag(MAX_TAGS + 1) == 0, "tag max+1 is rejected");
    CHECK(add_at_tag(1000000) == 0, "a far-out tag is rejected");

    // SEQUENCE_TAG is an unsigned field read into an int32_t, so anything
    // past INT32_MAX arrives negative and would index backwards.
    CHECK(add_at_tag((int32_t)0x80000000u) == 0,
          "a tag that reads negative is rejected");
}

// ...and it still refuses with every legitimate slot occupied, which is
// the state a host that fills its allocation is actually in.
static void test_full_table(void) {
    printf("a full table still refuses the one past the end\n");

    sequencer_reset();
    int filled = 0;
    for (int32_t tag = 0; tag < MAX_TAGS; ++tag)
        filled += add_at_tag(tag);
    CHECK(filled == MAX_TAGS, "every one of the %d valid tags took an event",
          MAX_TAGS);
    CHECK(add_at_tag(MAX_TAGS) == 0, "tag %d is still rejected", MAX_TAGS);
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.max_sequencer_tags = MAX_TAGS;
    amy_start(c);

    test_tag_bounds();
    test_full_table();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall sequencer bounds checks passed\n");
    return 0;
}
