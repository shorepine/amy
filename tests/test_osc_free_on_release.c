// Tests that releasing a voice's oscs returns their storage.
//
// synth[] blocks are allocated lazily on first touch and, before this
// change, were never freed short of amy_reset_oscs() -- releasing an
// instrument or shrinking a synth kept the arena at its all-time
// high-water mark forever. release_voice_oscs() already discarded the
// *state* at that boundary (it scheduled a RESET_OSC that wiped the osc
// in place), so freeing the block is behaviorally identical -- an
// unallocated osc reads as defaults (see test_synth_readout.c) and the
// next touch re-allocates -- it just gives the memory back. On desktop
// nobody notices; on MCU targets the difference is whether a session's
// footprint tracks the current scene or its history (#1105).
//
// The assertions are per-osc against osc_to_voice ownership rather than
// total counts: voice base oscs relocate across resizes, so identity, not
// arithmetic, is the contract. The steady-state test adds the count-level
// guarantee on top - which this change also makes meaningful, by stopping
// the describe-paths (patch_string store, #1100 snapshot capture) from
// allocating oscs as a side effect of writing a description.
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

static void render_a_bit(void) {
    for (int i = 0; i < 16; ++i) amy_simple_fill_buffer();
}

static void send(const char *m) {
    amy_add_message((char *)m);
    render_a_bit();
}

// Every osc the instrument's voices currently own, via osc_to_voice.
#define MAX_OWNED 64
static int collect_owned(int instr, uint16_t *oscs) {
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    int nv = instrument_get_num_voices(instr, voices);
    int n = 0;
    for (int i = 0; i < AMY_OSCS && n < MAX_OWNED; ++i) {
        if (!AMY_IS_SET(osc_to_voice[i])) continue;
        for (int v = 0; v < nv; ++v) {
            if (osc_to_voice[i] == voices[v]) { oscs[n++] = (uint16_t)i; break; }
        }
    }
    return n;
}

static int all_null(const uint16_t *oscs, int n) {
    for (int i = 0; i < n; ++i)
        if (synth[oscs[i]] != NULL) return 0;
    return 1;
}

static int all_allocated(const uint16_t *oscs, int n) {
    for (int i = 0; i < n; ++i)
        if (synth[oscs[i]] == NULL) return 0;
    return 1;
}

static int count_allocated(void) {
    int n = 0;
    for (int i = 0; i < AMY_OSCS; ++i)
        if (synth[i] != NULL) ++n;
    return n;
}

static void test_release_frees_owned_oscs(void) {
    printf("releasing a synth frees every osc its voices owned\n");
    send("i30iv2uv0w0f220Zv1w0f330Z");
    send("i30n60l1Z");
    uint16_t owned[MAX_OWNED];
    int n = collect_owned(30, owned);
    CHECK(n == 4, "2 voices x 2-osc patch own 4 oscs (got %d)", n);
    CHECK(all_allocated(owned, n), "all owned oscs are allocated while live");
    send("i30n60l0Z");
    send("i30iv0Z");
    CHECK(all_null(owned, n), "all owned oscs freed after release");
}

static void test_shrink_frees_released_voices(void) {
    printf("shrinking a synth frees the released voices' oscs\n");
    send("i31iv4uv0w0f220Zv1w0f330Z");
    uint16_t owned4[MAX_OWNED];
    int n4 = collect_owned(31, owned4);
    CHECK(n4 == 8, "4 voices own 8 oscs (got %d)", n4);
    send("i31iv1Z");
    uint16_t owned1[MAX_OWNED];
    int n1 = collect_owned(31, owned1);
    CHECK(n1 == 2, "1 voice owns 2 oscs (got %d)", n1);
    // Every osc that was owned at 4 voices and is not owned now must be free.
    int leaked = 0;
    for (int i = 0; i < n4; ++i) {
        int still_owned = 0;
        for (int j = 0; j < n1; ++j)
            if (owned4[i] == owned1[j]) { still_owned = 1; break; }
        if (!still_owned && synth[owned4[i]] != NULL) ++leaked;
    }
    CHECK(leaked == 0, "no released voice left a block behind (%d leaked)", leaked);
    send("i31iv0Z");
    CHECK(all_null(owned4, n4) && all_null(owned1, n1),
          "full release freed the rest");
}

static void test_freed_oscs_come_back(void) {
    printf("a freed osc re-allocates on next use and still plays\n");
    send("i32iv2uv0w0f220Zv1w0f330Z");
    uint16_t owned[MAX_OWNED];
    int n = collect_owned(32, owned);
    send("i32iv0Z");
    CHECK(all_null(owned, n), "released");
    send("i32iv2uv0w0f220Zv1w0f330Z");
    send("i32n64l1Z");
    n = collect_owned(32, owned);
    CHECK(n == 4 && all_allocated(owned, n), "re-allocated on reuse and playing");
    send("i32n64l0Z");
    send("i32iv0Z");
    CHECK(all_null(owned, n), "and freed again");
}

static void test_cycles_reach_steady_state(void) {
    printf("configure/release cycles converge instead of accumulating\n");
    send("i33iv2uv0w0f220Zv1w0f330Z");
    send("i33n60l1Z"); send("i33n60l0Z");
    send("i33iv0Z");
    int after_one = count_allocated();
    for (int i = 0; i < 19; ++i) {
        send("i33iv2uv0w0f220Zv1w0f330Z");
        send("i33n60l1Z"); send("i33n60l0Z");
        send("i33iv0Z");
    }
    int after_twenty = count_allocated();
    CHECK(after_twenty == after_one,
          "20 cycles end where 1 cycle ends (%d vs %d)", after_twenty, after_one);
}

static void test_builtin_patch_release(void) {
    printf("a built-in patch synth frees its blocks too\n");
    send("i34iv2K0Z");
    send("i34n60l1Z");
    uint16_t owned[MAX_OWNED];
    int n = collect_owned(34, owned);
    CHECK(n > 0 && all_allocated(owned, n), "built-in patch allocated %d oscs", n);
    send("i34n60l0Z");
    send("i34iv0Z");
    CHECK(all_null(owned, n), "and freed them on release");
}

// --- References held by OTHER oscs -------------------------------------
//
// Freeing an osc NULLs its synth[] slot, but nothing walks the rest of the
// array to clear the references to it: mod_source, algo_source and
// chained_osc are all stored as bare osc numbers. amy_render skips a NULL
// osc at the top of its loop, so the referrer itself is safe -- but the
// recursive descents past that skip (compute_mod_scale -> hold_and_modify,
// render_algo -> render_mod) and the note-on mod trigger in play_delta all
// dereferenced the referenced osc without rechecking, which is a segfault
// on the render thread. render_osc_wave already rechecks before following
// chained_osc, for exactly this reason.
//
// The dangling reference is built the way it arises in the field: a bare
// osc names an osc that no voice owns yet, and the voice allocator -- which
// only skips oscs with osc_to_voice set -- later hands that same osc to a
// voice, whose release then frees it.

// An osc currently owned by instrument `instr`, or AMY_UNSET_VALUE on none.
static uint16_t an_owned_osc(int instr) {
    uint16_t owned[MAX_OWNED];
    int n = collect_owned(instr, owned);
    return n ? owned[0] : (uint16_t)0xFFFF;
}

static void test_render_survives_freed_mod_source(void) {
    printf("rendering an osc whose mod_source was freed does not crash\n");
    send("i36iv1uv0w0f220Zv1w0f330Z");
    uint16_t victim = an_owned_osc(36);
    CHECK(victim != 0xFFFF, "found an osc owned by the synth (%d)", victim);
    char m[64];
    // A bare osc, modulated by an osc the synth owns.
    snprintf(m, sizeof(m), "v0w0f220L%dZ", victim);
    send(m);
    send("v0f220l1Z");
    CHECK(synth[0] != NULL && synth[0]->mod_source[0] == victim,
          "bare osc 0 is modulated by osc %d", victim);
    // Release the synth: the modulator goes away, osc 0 keeps its number.
    send("i36iv0Z");   // <- rendered past a NULL modulator before the fix
    CHECK(synth[victim] == NULL, "the modulator was freed");
    CHECK(synth[0] != NULL, "the osc that named it is still here");
    render_a_bit();
    CHECK(1, "rendered with a dangling mod_source");
    send("v0l0Z");
}

static void test_note_on_survives_freed_mod_source(void) {
    printf("a note-on whose mod_source was freed does not crash\n");
    send("i37iv1uv0w0f220Zv1w0f330Z");
    uint16_t victim = an_owned_osc(37);
    char m[64];
    snprintf(m, sizeof(m), "v2w0f220L%dZ", victim);
    send(m);
    send("i37iv0Z");
    CHECK(synth[victim] == NULL, "the modulator was freed");
    // play_delta's note-on walks mod_source[] to trigger the modulators.
    send("v2f220l1Z");
    CHECK(1, "note-on with a dangling mod_source");
    send("v2l0Z");
}

static void test_render_survives_freed_algo_source(void) {
    printf("rendering an algo osc whose algo_source was freed does not crash\n");
    send("i38iv1uv0w0f220Zv1w0f330Z");
    uint16_t victim = an_owned_osc(38);
    char m[64];
    snprintf(m, sizeof(m), "v3w%do1O%dZ", ALGO, victim);
    send(m);
    send("v3l1Z");
    send("i38iv0Z");
    CHECK(synth[victim] == NULL, "the algo source was freed");
    render_a_bit();
    CHECK(1, "rendered with a dangling algo_source");
    send("v3l0Z");
}

static void test_note_survives_freed_chained_osc(void) {
    printf("a note on an osc whose chained_osc was freed does not crash\n");
    send("i39iv1uv0w0f220Zv1w0f330Z");
    uint16_t victim = an_owned_osc(39);
    char m[64];
    snprintf(m, sizeof(m), "v4w0f220c%dZ", victim);
    send(m);
    send("i39iv0Z");
    CHECK(synth[victim] == NULL, "the chained osc was freed");
    // play_delta walks the chain on note, velocity and note-off.
    send("v4n60Z");
    send("v4f220l1Z");
    render_a_bit();
    send("v4l0Z");
    CHECK(1, "note on/off walked a chain ending in a freed osc");
}

// AMY calls these; the test binary has to provide them.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    render_a_bit();

    test_release_frees_owned_oscs();
    test_shrink_frees_released_voices();
    test_freed_oscs_come_back();
    test_cycles_reach_steady_state();
    test_builtin_patch_release();
    test_render_survives_freed_mod_source();
    test_note_on_survives_freed_mod_source();
    test_render_survives_freed_algo_source();
    test_note_survives_freed_chained_osc();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
