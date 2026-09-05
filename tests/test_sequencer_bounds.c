// Regression test for the sequencer tag bounds check.
//
// User-addressable tags once indexed `sequences[0 .. max_sequences-1]`, with
// the anonymous pool immediately after it. An earlier
// version of the sequencer guarded with `tag > max_sequences` (and read
// the tag into an int32_t), which let tag == max_sequences write one
// entry past the user range — in those days one element past the whole
// allocation, a heap overflow. Tagged definitions and anonymous direct events
// now use separate storage, and sequencer_add_wire() still checks
// `tag >= (uint32_t)max_sequences` unsigned, which also disposes of the
// negative-reindex case: a tag past INT32_MAX stays a huge unsigned
// value and fails the same compare, so it can never index backwards.
//
// Not reachable from the audio-rendering suite, which never sends a tag
// near the ceiling. It takes one hand-written message:
//
//     amy.send(ticks="0,16,256")     # with max_sequencer_tags 256
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

static const uint64_t BPS = AMY_SAMPLE_RATE / AMY_BLOCK_SIZE;

static void advance_secs(double secs) {
    uint64_t n = (uint64_t)(BPS * secs);
    for (uint64_t i = 0; i < n; i++) amy_simple_fill_buffer();
}

// A repeating (every 16 ticks) note-on for `osc`, scheduled at `tag`.
static void seq_note_on_at_tag(uint32_t tag, int osc) {
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

static void seq_clear(uint32_t tag) {
    amy_event e = amy_default_event();
    e.ticks[TICKS_TICK] = 0;
    e.ticks[TICKS_PERIOD] = 0;
    e.ticks[TICKS_TAG] = tag;
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

static int accepted(uint32_t tag) {
    sequencer_reset();
    seq_note_on_at_tag(tag, 0);
    int scheduled = sequencer_sequence_control(
        tag, SEQUENCE_CONTROL_START, 0, 0);
    advance_secs(0.5);
    int fired = audible(0);
    sequencer_sequence_control(tag, SEQUENCE_CONTROL_STOP, 0, 0);
    seq_clear(tag);
    all_off();
    sequencer_reset();
    CHECK(fired == scheduled, "fired (%d) agrees with scheduled (%d) for tag %" PRIu32,
          fired, scheduled, tag);
    return fired && scheduled;
}

static void test_tag_bounds(void) {
    printf("sequencer tag bounds (max_sequencer_tags = %d)\n", MAX_TAGS);

    CHECK(accepted(0), "tag 0 is accepted");
    CHECK(accepted(MAX_TAGS - 1),
          "tag max-1 (%d) is accepted -- the last valid slot", MAX_TAGS - 1);

    // The historical bug: this one used to be written past the user range.
    CHECK(!accepted(MAX_TAGS),
          "tag max (%d) is REJECTED, not written past the user range", MAX_TAGS);
    CHECK(!accepted(MAX_TAGS + 1), "tag max+1 is rejected");
    CHECK(!accepted(1000000), "a far-out tag is rejected");
    // Past INT32_MAX: with a signed read this indexed backwards.
    CHECK(!accepted(0x80000000u), "a tag past INT32_MAX is rejected");
}

// An out-of-range user tag must not affect the separate anonymous pool.
static void test_no_anon_clobber(void) {
    printf("an out-of-range tag can't clobber an anonymous entry\n");
    sequencer_reset();

    // Anonymous repeating entry (no tag): wire form "H<tick>,<period>".
    amy_add_message("H0,16v1w0n64l1Z");
    // Now aim a user tag exactly at the anonymous region.
    seq_note_on_at_tag(MAX_TAGS, 2);
    advance_secs(0.5);
    CHECK(audible(1), "the anonymous entry still fires");
    CHECK(!audible(2), "the out-of-range user entry does not");
    sequencer_reset();
    all_off();
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    c.max_sequencer_tags = MAX_TAGS;
    amy_start(c);

    test_tag_bounds();
    test_no_anon_clobber();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall sequencer bounds checks passed\n");
    return 0;
}
