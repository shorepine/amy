// The unison cluster ('C': count, spacing, offset, blend) renders detuned
// copies of an osc's wave inside the osc.  These checks pin the parts of that
// contract that rendering alone would not: one copy at the osc's own pitch is
// sample-identical to no cluster at all, two copies sound like two plain oscs
// at the copies' pitches and weights, the parameters clamp and read back as
// sent, and waves without a LUT renderer ignore them.
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

static void test_two_copies_are_two_oscs(void) {
    printf("a 2-copy sine cluster sounds like two plain sines\n");
    // D = 0.02 octaves: the copies sit at -D and +D around note 60, which
    // two plain oscs reach as fractional notes 60 -/+ 12 * D, each at the
    // cluster's equal weight 1/sqrt(2) and the cluster's phase spread.
    const char *cluster[] = {"v0w0C2,0.04,-0.02,1Z", "v0n60l1Z"};
    const char *pair[] = {"v0w0a0.70710678P0Z", "v1w0a0.70710678P0.5Z", "v0n59.76l1Z", "v1n60.24l1Z"};
    render_scene(cluster, 2, scene_a);
    render_scene(pair, 4, scene_b);
    double ref = rms(scene_b);
    double diff = rms_diff(scene_a, scene_b);
    double db = (diff > 0) ? 20.0 * log10(diff / ref) : -INFINITY;
    CHECK(ref > 0, "the pair renders (rms %.1f, cluster rms %.1f)", ref, rms(scene_a));
    CHECK(db < -40.0, "cluster - pair is %.1f dB below the pair", db);
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

int main(void) {
    alarm(60);
    test_single_copy_is_plain();
    test_two_copies_are_two_oscs();
    test_clamps();
    test_non_lut_waves_ignore();
    test_kind_claimed();
    test_wire_round_trip();
    printf(failures ? "FAILURES: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
