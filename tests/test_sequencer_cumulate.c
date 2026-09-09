// Events scheduled on one sequencer tag ACCUMULATE.
//
// A tag used to hold exactly one event: sending a second `ticks=` with the
// same tag replaced the first, so a tag was a slot, and building a pattern
// meant spending one tag per note. Now each send ADDS an entry under that
// tag, with its own tick and period, and the tag names the whole pattern.
//
// Taking a tag back is `ticks="0,0,<tag>"` -- the same cancel spelling
// callers have always used, except it now drops every event on the tag
// rather than the single entry a tag used to hold. There is no way to drop
// one event from a tag; you rebuild the tag instead.
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

#define MAX_TAGS 32

static const uint64_t BPS = AMY_SAMPLE_RATE / AMY_BLOCK_SIZE;

static void advance_secs(double secs) {
    uint64_t n = (uint64_t)(BPS * secs);
    for (uint64_t i = 0; i < n; i++) amy_simple_fill_buffer();
}

// A repeating note-on for `osc` at `tag`, every 16 ticks.
static void seq_note_on(uint32_t tag, int osc) {
    amy_event e = amy_default_event();
    e.osc = osc;
    e.wave = SINE;
    e.velocity = 1.0f;
    e.midi_note = 60;
    e.ticks[TICKS_TICK] = 0;
    e.ticks[TICKS_PERIOD] = 16;
    e.ticks[TICKS_TAG] = tag;
    amy_add_event(&e);
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

static int audible(int osc) {
    return synth[osc] != NULL && synth[osc]->status == SYNTH_AUDIBLE;
}

static int32_t n_scheduled(void) {
    extern int32_t first_active;
    extern struct sequence_info_t *sequences;
    // sequence_info_t is private to sequencer.c, so count through the head
    // link only -- what we need here is "is anything scheduled", plus the
    // firing evidence below.
    (void)sequences;
    return first_active;
}

// Three events on one tag all fire; they did not overwrite each other.
static void test_adds_accumulate(void) {
    printf("several events on one tag all fire\n");
    sequencer_reset();

    seq_note_on(5, 0);
    seq_note_on(5, 1);
    seq_note_on(5, 2);
    advance_secs(1.0);
    CHECK(audible(0) && audible(1) && audible(2),
          "all three events sent to tag 5 fired");

    sequencer_clear_tag(5);
    all_off();
}

// ...and clearing takes the whole tag back, leaving other tags alone.
static void test_clear_takes_the_whole_tag(void) {
    printf("clearing a tag drops every event on it, and only that tag\n");
    sequencer_reset();

    seq_note_on(5, 0);
    seq_note_on(5, 1);
    seq_note_on(9, 2);        // a different tag, must survive
    advance_secs(1.0);
    CHECK(audible(0) && audible(1) && audible(2), "all three fired to start with");

    // Clear tag 5, silence everything, and let the sequencer run again: what
    // is still scheduled retriggers itself, what was cleared cannot.
    sequencer_clear_tag(5);
    all_off();
    advance_secs(1.0);
    CHECK(!audible(0) && !audible(1), "both events on tag 5 are gone");
    CHECK(audible(2), "tag 9 still fires");

    sequencer_clear_tag(9);
    all_off();
    advance_secs(1.0);
    CHECK(n_scheduled() == -1, "nothing is scheduled once both tags are cleared");
}

// The wire spelling of the clear.
static void test_wire_clear(void) {
    printf("\"H0,0,<tag>\" on the wire clears every event on the tag\n");
    sequencer_reset();

    amy_add_message("H0,16,5v0w0n60l1");
    amy_add_message("H0,16,5v1w0n64l1");
    amy_add_message("H0,16,9v2w0n67l1");
    advance_secs(1.0);
    CHECK(audible(0) && audible(1) && audible(2), "two events on tag 5, one on tag 9");

    amy_add_message("H0,0,5");
    all_off();
    advance_secs(1.0);
    CHECK(!audible(0) && !audible(1), "both events on tag 5 are gone");
    CHECK(audible(2), "tag 9 is untouched");

    // Clearing and rebuilding: the clear has to land first, so it is its own
    // message -- ticks= claims the whole rest of a message as its payload.
    amy_add_message("H0,0,9");
    amy_add_message("H0,16,9v0w0n72l1");
    all_off();
    advance_secs(1.0);
    CHECK(!audible(2), "the old tag 9 event is gone");
    CHECK(audible(0), "and its replacement fires");

    amy_add_message("H0,0,9");
    all_off();
    advance_secs(1.0);
    CHECK(n_scheduled() == -1, "clearing the last tag leaves the list empty");
}

// The same thing through the C event API, which is what every caller that
// isn't building wire strings by hand actually uses.
static void test_zero_ticks_event(void) {
    printf("ticks=(0,0,<tag>) on an amy_event clears the whole tag\n");
    sequencer_reset();

    seq_note_on(5, 0);
    seq_note_on(5, 1);
    advance_secs(1.0);
    CHECK(audible(0) && audible(1), "two events on tag 5 fired");

    amy_event e = amy_default_event();
    e.ticks[TICKS_TICK] = 0;
    e.ticks[TICKS_PERIOD] = 0;
    e.ticks[TICKS_TAG] = 5;
    amy_add_event(&e);

    all_off();
    advance_secs(1.0);
    CHECK(!audible(0) && !audible(1), "both are gone");
    CHECK(n_scheduled() == -1, "nothing left scheduled");
}

// An out-of-range tag is rejected by the erase the same way the add rejects
// it, so it can't reach into the anonymous pool that sits past the tag range.
static void test_clear_tag_bounds(void) {
    printf("clearing bounds-checks its tag\n");
    sequencer_reset();

    amy_add_message("H0,16v0w0n60l1");     // anonymous: lands in the pool
    advance_secs(0.5);
    CHECK(audible(0), "the anonymous entry fires");

    sequencer_clear_tag(MAX_TAGS);          // first anonymous slot's index
    sequencer_clear_tag(0x80000000u);       // and one that would index backwards
    all_off();
    advance_secs(0.5);
    CHECK(audible(0), "an out-of-range clear didn't touch it");

    sequencer_reset();
    all_off();
}

// Running out of pool: cumulating means a tag can now exhaust the table,
// where one tag used to mean one slot. It must drop the extra rather than
// scribble, and keep serving what it already holds.
static void test_pool_exhaustion(void) {
    printf("filling the pool drops the overflow and keeps the rest\n");
    sequencer_reset();

    for (int i = 0; i < MAX_TAGS + 8; i++) seq_note_on(5, i % 3);
    advance_secs(1.0);
    CHECK(audible(0) && audible(1) && audible(2), "the entries that fit still fire");

    sequencer_clear_tag(5);
    all_off();
    advance_secs(1.0);
    CHECK(n_scheduled() == -1, "one reset clears all of them");
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.max_sequencer_tags = MAX_TAGS;
    amy_start(c);

    test_adds_accumulate();
    test_clear_takes_the_whole_tag();
    test_wire_clear();
    test_zero_ticks_event();
    test_clear_tag_bounds();
    test_pool_exhaustion();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall sequencer cumulate checks passed\n");
    return 0;
}
