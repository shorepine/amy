// The unison cluster ('C': count, spacing, offset, blend, groups, interval1,
// interval2, interval3, random, frame_spread) renders detuned copies of an
// osc's wave
// inside the osc.  These checks pin the parts of that contract that
// rendering alone would not: one copy at the osc's own pitch is
// sample-identical to no cluster at all, one group is sample-identical to the
// four-value form, two copies and two groups sound like plain oscs at the
// copies' pitches, weights and start phases (integer-ratio groups offset by
// THETA), copies at weight 0 drop out, random start phases differ between
// note-ons, the parameters clamp and read back as sent, waves without a LUT
// renderer ignore them, and an upper group leaves group 0's table alone.
//
// Not checked: the OOM path.  malloc_caps cannot be made to fail for one osc
// from a test (ram_caps_oscs would fail every osc allocation with it).
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

void delay_ms(uint32_t ms) { (void)ms; }

extern void set_event_for_osc(int base_osc, int rel_osc, struct amy_event *event);

#define BLOCKS 40
#define SCENE_LEN (BLOCKS * AMY_BLOCK_SIZE * AMY_NCHANS)

static void start(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    amy_reset_sysclock();
}

// Render one scene from a clean engine into out[SCENE_LEN].
static void render_scene(const char *const *msgs, int n_msgs, int16_t *out) {
    start();
    for (int i = 0; i < n_msgs; ++i) amy_add_message((char *)msgs[i]);
    for (int b = 0; b < BLOCKS; ++b) {
        int16_t *buf = amy_simple_fill_buffer();
        memcpy(out + b * AMY_BLOCK_SIZE * AMY_NCHANS, buf, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
    }
    amy_stop();
}

static double rms(const int16_t *x) {
    double sumsq = 0;
    for (int i = 0; i < SCENE_LEN; ++i) sumsq += (double)x[i] * (double)x[i];
    return sqrt(sumsq / SCENE_LEN);
}

static double rms_diff(const int16_t *a, const int16_t *b) {
    double sumsq = 0;
    for (int i = 0; i < SCENE_LEN; ++i) {
        double d = (double)a[i] - (double)b[i];
        sumsq += d * d;
    }
    return sqrt(sumsq / SCENE_LEN);
}

static int16_t scene_a[SCENE_LEN];
static int16_t scene_b[SCENE_LEN];

// Same note, once with no unison command and once with the given one sent
// to the osc first; returns 1 when the two renders are identical.
static int same_with_and_without(int wave, int preset, const char *unison, double *level) {
    char setup_plain[64], setup_unison[96];
    if (preset >= 0) {
        snprintf(setup_plain, sizeof(setup_plain), "v0w%dp%dZ", wave, preset);
        snprintf(setup_unison, sizeof(setup_unison), "v0w%dp%d%sZ", wave, preset, unison);
    } else {
        snprintf(setup_plain, sizeof(setup_plain), "v0w%dZ", wave);
        snprintf(setup_unison, sizeof(setup_unison), "v0w%d%sZ", wave, unison);
    }
    const char *plain[] = {setup_plain, "v0n60l1Z"};
    const char *with_unison[] = {setup_unison, "v0n60l1Z"};
    render_scene(plain, 2, scene_a);
    render_scene(with_unison, 2, scene_b);
    *level = rms(scene_a);
    return memcmp(scene_a, scene_b, sizeof(scene_a)) == 0;
}

static void test_single_copy_is_plain(void) {
    printf("one copy at the osc's pitch renders exactly as no cluster\n");
    const struct { int wave; const char *name; } waves[] = {
        {SINE, "sine"}, {SAW_DOWN, "saw"}, {PULSE, "pulse"}, {TRIANGLE, "triangle"}, {WAVETABLE, "wavetable"},
    };
    for (size_t i = 0; i < sizeof(waves) / sizeof(waves[0]); ++i) {
        double level;
        int same = same_with_and_without(waves[i].wave, -1, "C1,0,0,1", &level);
        CHECK(level > 0, "%s renders (rms %.1f)", waves[i].name, level);
        CHECK(same, "%s with C1,0,0,1 == %s with no C", waves[i].name, waves[i].name);
    }
}

// Render the cluster scene and the plain-osc scene and check that their
// difference is more than 40 dB below the plain oscs.
static void check_cluster_matches_oscs(const char *const *cluster, int n_cluster,
                                       const char *const *oscs, int n_oscs, const char *what) {
    render_scene(cluster, n_cluster, scene_a);
    render_scene(oscs, n_oscs, scene_b);
    double ref = rms(scene_b);
    double diff = rms_diff(scene_a, scene_b);
    double db = (diff > 0) ? 20.0 * log10(diff / ref) : -INFINITY;
    CHECK(ref > 0, "the %s render (rms %.1f, cluster rms %.1f)", what, ref, rms(scene_a));
    CHECK(db < -40.0, "cluster - %s is %.1f dB below the %s", what, db, what);
}

static void test_two_copies_are_two_oscs(void) {
    printf("a 2-copy sine cluster sounds like two plain sines\n");
    // D = 0.02 octaves: the copies sit at -D and +D around note 60, which
    // two plain oscs reach as fractional notes 60 -/+ 12 * D, each at the
    // cluster's equal weight 1/sqrt(2) and the cluster's phase spread.
    const char *cluster[] = {"v0w0C2,0.04,-0.02,1Z", "v0n60l1Z"};
    const char *pair[] = {"v0w0a0.70710678P0Z", "v1w0a0.70710678P0.5Z", "v0n59.76l1Z", "v1n60.24l1Z"};
    check_cluster_matches_oscs(cluster, 2, pair, 4, "pair");
}

static void test_one_group_is_four_values(void) {
    printf("one group given in the seven-value form renders exactly as the four-value form\n");
    const struct { int wave; const char *name; } waves[] = {
        {SINE, "sine"}, {SAW_DOWN, "saw"}, {PULSE, "pulse"}, {TRIANGLE, "triangle"}, {WAVETABLE, "wavetable"},
    };
    for (size_t i = 0; i < sizeof(waves) / sizeof(waves[0]); ++i) {
        char four[64], seven[64];
        snprintf(four, sizeof(four), "v0w%dC4,0.01,-0.015,0.8Z", waves[i].wave);
        snprintf(seven, sizeof(seven), "v0w%dC4,0.01,-0.015,0.8,1,0,0Z", waves[i].wave);
        const char *msgs_four[] = {four, "v0n60l1Z"};
        const char *msgs_seven[] = {seven, "v0n60l1Z"};
        render_scene(msgs_four, 2, scene_a);
        render_scene(msgs_seven, 2, scene_b);
        CHECK(rms(scene_a) > 0, "%s renders (rms %.1f)", waves[i].name, rms(scene_a));
        CHECK(memcmp(scene_a, scene_b, sizeof(scene_a)) == 0,
              "%s with C4,0.01,-0.015,0.8,1,0,0 == %s with C4,0.01,-0.015,0.8", waves[i].name, waves[i].name);
    }
}

static void test_two_groups_are_two_oscs(void) {
    printf("a 2-copy sine cluster in two groups at an integer ratio sounds like two plain sines offset by THETA\n");
    // One copy per group, no spread: notes 60 and 72, equal weights 1/sqrt(2).
    // The octave copy starts at twice the root's phase plus THETA = 0.21132487.
    const char *cluster[] = {"v0w0C2,0,0,1,2,1Z", "v0n60l1Z"};
    const char *pair[] = {"v0w0a0.70710678P0Z", "v1w0a0.70710678P0.21132487Z", "v0n60l1Z", "v1n72l1Z"};
    check_cluster_matches_oscs(cluster, 2, pair, 4, "pair");
    // An octave down starts at half the root's phase plus THETA / 2, THETA
    // cycles of its second harmonic.
    const char *sub_cluster[] = {"v0w0C2,0,0,1,2,-1Z", "v0n60l1Z"};
    const char *sub_pair[] = {"v0w0a0.70710678P0Z", "v1w0a0.70710678P0.10566244Z", "v0n60l1Z", "v1n48l1Z"};
    check_cluster_matches_oscs(sub_cluster, 2, sub_pair, 4, "sub pair");
    // A twelfth (ratio 3, log2(3) octaves, 19.01955 semitones) starts at
    // three times the root's phase plus THETA.
    const char *twelfth_cluster[] = {"v0w0C2,0,0,1,2,1.5849625Z", "v0n60l1Z"};
    const char *twelfth_pair[] = {"v0w0a0.70710678P0Z", "v1w0a0.70710678P0.21132487Z", "v0n60l1Z", "v1n79.01955l1Z"};
    check_cluster_matches_oscs(twelfth_cluster, 2, twelfth_pair, 4, "twelfth pair");
}

static void test_two_spread_groups_are_four_oscs(void) {
    printf("a 4-copy sine cluster in two spread groups sounds like four plain sines\n");
    // D = 10/1200 octaves: group 0 holds copies 0, 1 at -D, +D, group 1
    // copies 2, 3 at 1 - D, 1 + D, i.e. notes 59.9, 60.1, 71.9, 72.1, each at
    // weight 1/2.  The root copies start at 0 and 1/2, the octave copies at
    // twice the root copy of their index plus THETA: THETA and 1 + THETA.
    const float D = 10.0f / 1200.0f;
    char setup[96];
    snprintf(setup, sizeof(setup), "v0w0C4,%.9f,%.9f,1,2,1Z", 2 * D, -D);
    const char *cluster[] = {setup, "v0n60l1Z"};
    const char *quad[] = {"v0w0a0.5P0Z", "v1w0a0.5P0.5Z", "v2w0a0.5P0.21132487Z", "v3w0a0.5P0.21132487Z",
                          "v0n59.9l1Z", "v1n60.1l1Z", "v2n71.9l1Z", "v3n72.1l1Z"};
    check_cluster_matches_oscs(cluster, 2, quad, 8, "four oscs");
}

// Render osc 0 from a clean engine into out[SCENE_LEN]: a note-on, half the
// blocks, the same note-on again, the other half.
static void render_retriggered(const char *setup, int16_t *out) {
    start();
    amy_add_message((char *)setup);
    for (int b = 0; b < BLOCKS; ++b) {
        if (b == 0 || b == BLOCKS / 2) amy_add_message("v0n60l1Z");
        int16_t *buf = amy_simple_fill_buffer();
        memcpy(out + b * AMY_BLOCK_SIZE * AMY_NCHANS, buf, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
    }
    amy_stop();
}

// Level of the second note's render against the first, after each note's
// first block; -INFINITY when they are identical.
static double retrigger_diff_db(const int16_t *x, double *level) {
    const int half = BLOCKS / 2, block = AMY_BLOCK_SIZE * AMY_NCHANS;
    double sumsq = 0, diffsq = 0;
    for (int i = block; i < half * block; ++i) {
        double a = x[i], b = x[i + half * block];
        sumsq += a * a;
        diffsq += (a - b) * (a - b);
    }
    *level = sqrt(sumsq / ((half - 1) * block));
    return (diffsq > 0) ? 10.0 * log10(diffsq / sumsq) : -INFINITY;
}

static void test_random_phases(void) {
    printf("random start phases differ at every note-on, the fixed rule does not\n");
    // Copy 0 restarts at P0 either way; only copies 1..3 can differ.
    double level;
    render_retriggered("v0w0P0C4,0.01,-0.015,1Z", scene_a);
    double fixed_db = retrigger_diff_db(scene_a, &level);
    CHECK(fixed_db == -INFINITY, "random 0: the second note renders as the first (%.1f dB)", fixed_db);
    render_retriggered("v0w0P0C4,0.01,-0.015,1,1,0,0,0,1Z", scene_a);
    double random_db = retrigger_diff_db(scene_a, &level);
    CHECK(level > 0, "random 1 renders (rms %.1f)", level);
    CHECK(random_db > -40.0, "random 1: the second note differs from the first by %.1f dB", random_db);
}

// Render osc 0 from a clean engine into out[SCENE_LEN]: setup and a note-on,
// half the blocks, then change, then the other half.
static void render_changed_mid_note(const char *setup, const char *change, int16_t *out) {
    start();
    amy_add_message((char *)setup);
    amy_add_message("v0n60l1Z");
    for (int b = 0; b < BLOCKS; ++b) {
        if (b == BLOCKS / 2) amy_add_message((char *)change);
        int16_t *buf = amy_simple_fill_buffer();
        memcpy(out + b * AMY_BLOCK_SIZE * AMY_NCHANS, buf, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
    }
    amy_stop();
}

// A: setup, changed to final mid-note.  B: the final settings from the
// note-on, sent again at the same block.  Copy 0 plays the same pitch in
// both, so at the change both respread from the same phase and must match
// from then on.  Without the respread, A's copies 1..3 would carry on from
// the spread they started with.
static void check_change_respreads(const char *setup, const char *final_setup, const char *final,
                                   const char *what) {
    render_changed_mid_note(setup, final, scene_a);
    render_changed_mid_note(final_setup, final, scene_b);
    const int start_at = (BLOCKS / 2) * AMY_BLOCK_SIZE * AMY_NCHANS;
    double sumsq = 0, diffsq = 0;
    for (int i = start_at; i < SCENE_LEN; ++i) {
        double a = scene_a[i], b = scene_b[i];
        sumsq += b * b;
        diffsq += (a - b) * (a - b);
    }
    double db = (diffsq > 0) ? 10.0 * log10(diffsq / sumsq) : -INFINITY;
    CHECK(sumsq > 0, "%s: the fresh note renders after the change (rms %.1f)", what,
          sqrt(sumsq / (SCENE_LEN - start_at)));
    CHECK(db < -40.0, "%s: changed mid-note - fresh note is %.1f dB below the fresh note", what, db);
}

static void test_geometry_change_respreads(void) {
    printf("a groups or interval change mid-note respreads the copies\n");
    check_change_respreads("v0w0C4,0.01,-0.015,1Z", "v0w0C4,0.01,-0.015,1,2,1Z",
                           "v0C4,0.01,-0.015,1,2,1Z", "groups 1 -> 2");
    check_change_respreads("v0w0C4,0.01,-0.015,1,2,1Z", "v0w0C4,0.01,-0.015,1,2,2Z",
                           "v0C4,0.01,-0.015,1,2,2Z", "interval1 1 -> 2");
}

static void test_zero_weight_skipped(void) {
    printf("copies at weight 0 are left out\n");
    // Blend 0 over -D, 0, +D: the outer copies weigh 0 and the centre copy 1.
    // The centre copy is copy 1, which starts a third of a cycle on.
    const float D = 10.0f / 1200.0f;
    char setup[96];
    snprintf(setup, sizeof(setup), "v0w0C3,%.9f,%.9f,0Z", D, -D);
    const char *cluster[] = {setup, "v0n60l1Z"};
    const char *single[] = {"v0w0a1P0.33333333Z", "v0n60l1Z"};
    check_cluster_matches_oscs(cluster, 2, single, 2, "single osc");
}

static amy_event read_back(int osc) {
    amy_event e = amy_default_event();
    set_event_for_osc(0, osc, &e);
    return e;
}

static void test_clamps(void) {
    printf("count, spacing, offset and blend clamp, and read back clamped\n");
    start();
    amy_add_message("v0w0C0,0,0.01Z");
    amy_add_message("v1w0C9Z");
    amy_add_message("v2w0C2,0,0,2Z");
    amy_add_message("v3w0C2,9,-9Z");
    amy_execute_deltas();
    amy_event e0 = read_back(0), e1 = read_back(1), e2 = read_back(2), e3 = read_back(3);
    CHECK(e0.unison_count == 1, "C0 reads back count %d", e0.unison_count);
    CHECK(fabsf(e0.unison_offset - 0.01f) < 1e-6f, "C0,0,0.01 keeps offset %f", e0.unison_offset);
    CHECK(e1.unison_count == 8, "C9 reads back count %d", e1.unison_count);
    CHECK(e2.unison_count == 2, "C2,0,0,2 reads back count %d", e2.unison_count);
    CHECK(e2.unison_blend == 1.0f, "C2,0,0,2 reads back blend %f", e2.unison_blend);
    CHECK(e3.unison_spacing == AMY_UNISON_MAX_OCTAVES, "C2,9,-9 reads back spacing %f", e3.unison_spacing);
    CHECK(e3.unison_offset == -AMY_UNISON_MAX_OCTAVES, "C2,9,-9 reads back offset %f", e3.unison_offset);
    amy_stop();
}

static void test_group_clamps(void) {
    printf("groups and intervals clamp, and read back clamped\n");
    start();
    amy_add_message("v0w0C2,0,0,1,0Z");
    amy_add_message("v1w0C2,0,0,1,5Z");
    amy_add_message("v2w0C2,0,0,1,2,5Z");
    amy_add_message("v3w0C2,0,0,1,2,-5Z");
    amy_add_message("v4w0C2,0.01,0,1,3Z");
    amy_add_message("v5w0C2,0,0,1,4,0,0,5Z");
    amy_add_message("v6w0C2,0,0,1,4,0,0,-5Z");
    amy_add_message("v7w0C2,0,0,1,1,0,0,0,2Z");
    amy_execute_deltas();
    amy_event e0 = read_back(0), e1 = read_back(1), e2 = read_back(2), e3 = read_back(3), e4 = read_back(4);
    amy_event e5 = read_back(5), e6 = read_back(6), e7 = read_back(7);
    CHECK(e0.unison_groups == 1, "groups 0 reads back %d", e0.unison_groups);
    CHECK(e1.unison_groups == 4, "groups 5 reads back %d", e1.unison_groups);
    CHECK(e2.unison_interval1 == AMY_UNISON_MAX_OCTAVES, "interval1 5 reads back %f", e2.unison_interval1);
    CHECK(e3.unison_interval1 == -AMY_UNISON_MAX_OCTAVES, "interval1 -5 reads back %f", e3.unison_interval1);
    CHECK(e4.unison_groups == 3, "groups 3 with count 2 reads back %d", e4.unison_groups);
    CHECK(e5.unison_interval3 == AMY_UNISON_MAX_OCTAVES, "interval3 5 reads back %f", e5.unison_interval3);
    CHECK(e6.unison_interval3 == -AMY_UNISON_MAX_OCTAVES, "interval3 -5 reads back %f", e6.unison_interval3);
    CHECK(e7.unison_random == 1, "random 2 reads back %d", e7.unison_random);
    amy_stop();
    const char *more_groups[] = {"v0w2C2,0.01,0,1,3,0.5,1Z", "v0n60l1Z"};
    render_scene(more_groups, 2, scene_a);
    CHECK(rms(scene_a) > 0, "groups 3 with count 2 renders (rms %.1f)", rms(scene_a));
}

static void test_copy_offset_clamp(void) {
    printf("a copy's offset is bounded at render time, the fields are not changed\n");
    // Eight copies at spacing 4 would reach 28 octaves up; each copy stops
    // at AMY_UNISON_MAX_OCTAVES.
    char setup[64];
    snprintf(setup, sizeof(setup), "v0w%dC8,4,0,1,1Z", SAW_DOWN);
    const char *msgs[] = {setup, "v0n48l1Z"};
    render_scene(msgs, 2, scene_a);
    double level = rms(scene_a);
    CHECK(isfinite(level) && level > 0, "count 8 at spacing 4 renders (rms %.1f)", level);
    start();
    amy_add_message(setup);
    amy_execute_deltas();
    amy_event e = read_back(0);
    CHECK(e.unison_count == 8, "count reads back %d", e.unison_count);
    CHECK(e.unison_spacing == 4.0f, "spacing reads back %f", e.unison_spacing);
    CHECK(e.unison_offset == 0.0f, "offset reads back %f", e.unison_offset);
    amy_stop();
}

static void test_non_lut_waves_ignore(void) {
    printf("waves without a LUT renderer ignore the cluster\n");
    double level;
    int same = same_with_and_without(PCM, 0, "C4,0.01,-0.015,1", &level);
    CHECK(level > 0, "pcm renders (rms %.1f)", level);
    CHECK(same, "pcm with C4,0.01,-0.015,1 == pcm with no C");
    same = same_with_and_without(KS, -1, "C4,0.01,-0.015,1", &level);
    CHECK(level > 0, "ks renders (rms %.1f)", level);
    CHECK(same, "ks with C4,0.01,-0.015,1 == ks with no C");
}

static void test_kind_claimed(void) {
    printf("the cluster renders whichever of wave and unison arrives last\n");
    // A default-wave osc (sine, no w sent): the unison delta claims the kind.
    const char *plain_default[] = {"v0n60l1Z"};
    const char *cluster_default[] = {"v0C2,0.04,-0.02,1Z", "v0n60l1Z"};
    render_scene(plain_default, 1, scene_a);
    render_scene(cluster_default, 2, scene_b);
    CHECK(memcmp(scene_a, scene_b, sizeof(scene_a)) != 0, "default-wave osc with C2 differs from the plain osc");
    // Unison first, on a wave without a cluster (noise), then the wave: the
    // WAVE delta claims the kind.
    const char *plain_saw[] = {"v0w5Z", "v0w2Z", "v0n60l1Z"};
    const char *cluster_then_saw[] = {"v0w5Z", "v0C2,0.04,-0.02,1Z", "v0w2Z", "v0n60l1Z"};
    render_scene(plain_saw, 3, scene_a);
    render_scene(cluster_then_saw, 4, scene_b);
    CHECK(memcmp(scene_a, scene_b, sizeof(scene_a)) != 0, "C2 sent before w2 differs from the plain saw");
}

static void test_wire_round_trip(void) {
    printf("the cluster reads back and prints as sent\n");
    start();
    amy_add_message("v0w2C4,0.01,-0.015,0.8Z");
    amy_execute_deltas();
    amy_event e = read_back(0);
    CHECK(e.unison_count == 4, "count %d", e.unison_count);
    CHECK(fabsf(e.unison_spacing - 0.01f) < 1e-6f, "spacing %f", e.unison_spacing);
    CHECK(fabsf(e.unison_offset + 0.015f) < 1e-6f, "offset %f", e.unison_offset);
    CHECK(fabsf(e.unison_blend - 0.8f) < 1e-6f, "blend %f", e.unison_blend);
    char wire[MAX_MESSAGE_LEN];
    sprint_event(&e, wire, sizeof(wire), /* wirecode= */ true);
    // snprintcodefloat prints 0.01f as 0.010: the float is not exactly 1/100.
    CHECK(strstr(wire, "C4,0.010,-0.015,0.8") != NULL, "wire form \"%s\"", wire);
    amy_stop();
}

static void test_groups_wire_round_trip(void) {
    printf("groups and intervals read back and print as sent\n");
    start();
    amy_add_message("v0w2C4,0.01,-0.015,0.8,2,0.5,1,-1,1Z");
    amy_execute_deltas();
    amy_event e = read_back(0);
    CHECK(e.unison_groups == 2, "groups %d", e.unison_groups);
    CHECK(fabsf(e.unison_interval1 - 0.5f) < 1e-6f, "interval1 %f", e.unison_interval1);
    CHECK(fabsf(e.unison_interval2 - 1.0f) < 1e-6f, "interval2 %f", e.unison_interval2);
    CHECK(fabsf(e.unison_interval3 + 1.0f) < 1e-6f, "interval3 %f", e.unison_interval3);
    CHECK(e.unison_random == 1, "random %d", e.unison_random);
    char wire[MAX_MESSAGE_LEN];
    sprint_event(&e, wire, sizeof(wire), /* wirecode= */ true);
    CHECK(strstr(wire, "C4,0.010,-0.015,0.8,2,0.5,1,-1,1") != NULL, "wire form \"%s\"", wire);
    amy_stop();
}

// The table osc 0 renders copy 0 from, after a note at MIDI 36 with the given
// unison command.
static const LUT *copy0_table(const char *unison) {
    char setup[64];
    snprintf(setup, sizeof(setup), "v0w%d%sZ", SAW_DOWN, unison);
    start();
    amy_add_message(setup);
    amy_add_message("v0n36l1Z");
    for (int b = 0; b < 4; ++b) amy_simple_fill_buffer();
    const LUT *lut = synth[0]->lut;
    amy_stop();
    return lut;
}

static void test_table_repicked_mid_note(void) {
    printf("a spacing change mid-note re-picks group 0's table\n");
    const LUT *narrow = copy0_table("C4,0.1,0,1");
    const LUT *wide = copy0_table("C4,1,0,1");
    char setup[64];
    snprintf(setup, sizeof(setup), "v0w%dC4,0.1,0,1Z", SAW_DOWN);
    start();
    amy_add_message(setup);
    amy_add_message("v0n36l1Z");
    for (int b = 0; b < 4; ++b) amy_simple_fill_buffer();
    amy_add_message("v0C4,1Z");
    for (int b = 0; b < 4; ++b) amy_simple_fill_buffer();
    const LUT *changed = synth[0]->lut;
    amy_stop();
    CHECK(narrow != wide, "spacing 0.1 (size %d) and 1.0 (size %d) pick different tables",
          narrow ? narrow->table_size : 0, wide ? wide->table_size : 0);
    CHECK(changed == wide, "after spacing 0.1 -> 1.0 mid-note copy 0's table (size %d) is the fresh spacing 1.0 one",
          changed ? changed->table_size : 0);
}

static void test_table_per_group(void) {
    printf("an upper group leaves group 0's table alone\n");
    const LUT *one_group = copy0_table("C2,0,0,1,1,2");
    const LUT *two_groups = copy0_table("C2,0,0,1,2,2");
    CHECK(one_group != NULL && one_group == two_groups,
          "copy 0's table with groups 2 (size %d) == with groups 1 (size %d)",
          two_groups ? two_groups->table_size : 0, one_group ? one_group->table_size : 0);
}

// frame_spread (the tenth value) fans a WAVETABLE cluster's copies over the
// table: two copies at 0.4 read a fifth of the table either side of the osc's
// own position, which two plain wavetable oscs reach through duty.
static void test_frame_spread_two_copies_are_two_oscs(void) {
    printf("a 2-copy wavetable cluster at frame_spread 0.4 sounds like two plain wavetable oscs at duty 0.3 and 0.7\n");
    char cluster_setup[64], a_setup[64], b_setup[64];
    snprintf(cluster_setup, sizeof(cluster_setup), "v0w%dd0.5C2,0.04,-0.02,1,1,0,0,0,0,0.4Z", WAVETABLE);
    snprintf(a_setup, sizeof(a_setup), "v0w%dd0.3a0.70710678P0Z", WAVETABLE);
    snprintf(b_setup, sizeof(b_setup), "v1w%dd0.7a0.70710678P0.5Z", WAVETABLE);
    const char *cluster[] = {cluster_setup, "v0n60l1Z"};
    const char *pair[] = {a_setup, b_setup, "v0n59.76l1Z", "v1n60.24l1Z"};
    check_cluster_matches_oscs(cluster, 2, pair, 4, "wavetable pair");
    // Two groups an octave apart fan the same way: the octave copies start
    // at twice their root copy's phase plus THETA, both at 0.21132487 here,
    // every weight 1/2.
    char g_setup[64], g0[64], g1[64], g2[64], g3[64];
    snprintf(g_setup, sizeof(g_setup), "v0w%dd0.5C4,0.04,-0.02,1,2,1,0,0,0,0.4Z", WAVETABLE);
    snprintf(g0, sizeof(g0), "v0w%dd0.3a0.5P0Z", WAVETABLE);
    snprintf(g1, sizeof(g1), "v1w%dd0.7a0.5P0.5Z", WAVETABLE);
    snprintf(g2, sizeof(g2), "v2w%dd0.3a0.5P0.21132487Z", WAVETABLE);
    snprintf(g3, sizeof(g3), "v3w%dd0.7a0.5P0.21132487Z", WAVETABLE);
    const char *groups[] = {g_setup, "v0n60l1Z"};
    const char *four[] = {g0, g1, g2, g3, "v0n59.76l1Z", "v1n60.24l1Z", "v2n71.76l1Z", "v3n72.24l1Z"};
    check_cluster_matches_oscs(groups, 2, four, 8, "octave groups");
}

static void test_frame_spread_elsewhere_inert(void) {
    printf("frame_spread changes nothing on a saw cluster, on one wavetable copy, or past the table's ends\n");
    const char *saw_plain[] = {"v0w2C2,0.04,-0.02,1Z", "v0n60l1Z"};
    const char *saw_spread[] = {"v0w2C2,0.04,-0.02,1,1,0,0,0,0,0.5Z", "v0n60l1Z"};
    render_scene(saw_plain, 2, scene_a);
    render_scene(saw_spread, 2, scene_b);
    CHECK(rms(scene_a) > 0, "the saw cluster renders (rms %.1f)", rms(scene_a));
    CHECK(memcmp(scene_a, scene_b, sizeof(scene_a)) == 0, "saw cluster with frame_spread 0.5 == without");
    double level;
    int same = same_with_and_without(WAVETABLE, -1, "C1,0,0,1,1,0,0,0,0,0.5", &level);
    CHECK(level > 0, "one wavetable copy renders (rms %.1f)", level);
    CHECK(same, "one wavetable copy with frame_spread 0.5 == no C");
    char end_setup[64];
    snprintf(end_setup, sizeof(end_setup), "v0w%dd1C4,0.01,-0.015,1,1,0,0,0,0,1Z", WAVETABLE);
    const char *at_end[] = {end_setup, "v0n60l1Z"};
    render_scene(at_end, 2, scene_a);
    CHECK(rms(scene_a) > 0, "four copies at the last frame with frame_spread 1 render (rms %.1f)", rms(scene_a));
}

// Near an end the fan shrinks to the room on that side: two copies at duty
// 0.1 with frame_spread 0.5 would reach 0.15 below the table's start, so
// they read 0.1 either side instead, which plain oscs reach at duty 0 and 0.2.
static void test_frame_spread_shrinks_at_the_ends(void) {
    printf("a 2-copy wavetable cluster at duty 0.1 with frame_spread 0.5 sounds like two plain wavetable oscs at duty 0 and 0.2\n");
    char cluster_setup[64], a_setup[64], b_setup[64];
    snprintf(cluster_setup, sizeof(cluster_setup), "v0w%dd0.1C2,0.04,-0.02,1,1,0,0,0,0,0.5Z", WAVETABLE);
    snprintf(a_setup, sizeof(a_setup), "v0w%dd0a0.70710678P0Z", WAVETABLE);
    snprintf(b_setup, sizeof(b_setup), "v1w%dd0.2a0.70710678P0.5Z", WAVETABLE);
    const char *cluster[] = {cluster_setup, "v0n60l1Z"};
    const char *pair[] = {a_setup, b_setup, "v0n59.76l1Z", "v1n60.24l1Z"};
    check_cluster_matches_oscs(cluster, 2, pair, 4, "wavetable pair at the start");
    // The same room from the other end: duty 0.9 reads 0.8 and 1.
    snprintf(cluster_setup, sizeof(cluster_setup), "v0w%dd0.9C2,0.04,-0.02,1,1,0,0,0,0,0.5Z", WAVETABLE);
    snprintf(a_setup, sizeof(a_setup), "v0w%dd0.8a0.70710678P0Z", WAVETABLE);
    snprintf(b_setup, sizeof(b_setup), "v1w%dd1a0.70710678P0.5Z", WAVETABLE);
    check_cluster_matches_oscs(cluster, 2, pair, 4, "wavetable pair at the end");
    // At the very end there is no room: four copies at duty 1 all read the
    // last frame, as four plain oscs there do (weights 1/2, phases j/4).
    char end_setup[64], e0[64], e1[64], e2[64], e3[64];
    snprintf(end_setup, sizeof(end_setup), "v0w%dd1C4,0.01,-0.015,1,1,0,0,0,0,1Z", WAVETABLE);
    snprintf(e0, sizeof(e0), "v0w%dd1a0.5P0Z", WAVETABLE);
    snprintf(e1, sizeof(e1), "v1w%dd1a0.5P0.25Z", WAVETABLE);
    snprintf(e2, sizeof(e2), "v2w%dd1a0.5P0.5Z", WAVETABLE);
    snprintf(e3, sizeof(e3), "v3w%dd1a0.5P0.75Z", WAVETABLE);
    const char *at_end[] = {end_setup, "v0n60l1Z"};
    const char *four[] = {e0, e1, e2, e3, "v0n59.82l1Z", "v1n59.94l1Z", "v2n60.06l1Z", "v3n60.18l1Z"};
    check_cluster_matches_oscs(at_end, 2, four, 8, "four oscs at the last frame");
}

static void test_frame_spread_clamps(void) {
    printf("frame_spread clamps to 0..1 and reads back\n");
    start();
    char m0[48], m1[48], m2[48];
    snprintf(m0, sizeof(m0), "v0w%dC2,0,0,1,1,0,0,0,0,5Z", WAVETABLE);
    snprintf(m1, sizeof(m1), "v1w%dC2,0,0,1,1,0,0,0,0,-1Z", WAVETABLE);
    snprintf(m2, sizeof(m2), "v2w%dC2,0,0,1,1,0,0,0,0,0.25Z", WAVETABLE);
    amy_add_message(m0);
    amy_add_message(m1);
    amy_add_message(m2);
    amy_execute_deltas();
    amy_event e0 = read_back(0), e1 = read_back(1), e2 = read_back(2);
    CHECK(e0.unison_frame_spread == 1.0f, "frame_spread 5 reads back %f", e0.unison_frame_spread);
    CHECK(e1.unison_frame_spread == 0.0f, "frame_spread -1 reads back %f", e1.unison_frame_spread);
    CHECK(e2.unison_frame_spread == 0.25f, "frame_spread 0.25 reads back %f", e2.unison_frame_spread);
    char wire[MAX_MESSAGE_LEN];
    sprint_event(&e2, wire, sizeof(wire), /* wirecode= */ true);
    CHECK(strstr(wire, "C2,0,0,1,1,0,0,0,0,0.25") != NULL, "wire form \"%s\"", wire);
    amy_stop();
}

int main(void) {
    alarm(60);
    test_single_copy_is_plain();
    test_two_copies_are_two_oscs();
    test_clamps();
    test_non_lut_waves_ignore();
    test_kind_claimed();
    test_wire_round_trip();
    test_one_group_is_four_values();
    test_two_groups_are_two_oscs();
    test_two_spread_groups_are_four_oscs();
    test_zero_weight_skipped();
    test_random_phases();
    test_geometry_change_respreads();
    test_group_clamps();
    test_copy_offset_clamp();
    test_groups_wire_round_trip();
    test_table_per_group();
    test_table_repicked_mid_note();
    test_frame_spread_two_copies_are_two_oscs();
    test_frame_spread_elsewhere_inert();
    test_frame_spread_shrinks_at_the_ends();
    test_frame_spread_clamps();
    printf(failures ? "FAILURES: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
