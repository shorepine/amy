// Wavetable pitch ladders (pcm_wavetable_ladder_build / _free,
// pcm_wavetable_lutset): band-limited copies of a preset's table that the
// oscillator picks by pitch through choose_from_lutset, as the saw does.
// These checks pin the contract: a rung is the frame's series cut at the
// rung's limit; a note the base table does not fold at renders exactly as
// without a ladder; a note above that renders exactly as a plain osc on the
// rung alone; the groups of a cluster pick their own rungs from their
// sharpest copy; copies fanned by frame_spread read their frames from that
// rung; a rebuild follows a rewritten table in place; freeing, unloading and
// amy_stop drop the rungs; a switch to another wave stops reading them.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

void delay_ms(uint32_t ms) { (void)ms; }

#define BLOCKS 40
#define SCENE_LEN (BLOCKS * AMY_BLOCK_SIZE * AMY_NCHANS)
#define CYCLE 256
#define FRAMES 64
#define TABLE_LEN (FRAMES * CYCLE)
#define TABLE_PRESET 100
#define HARMONICS 127

static int16_t scene_a[SCENE_LEN];
static int16_t scene_b[SCENE_LEN];

// The rungs' harmonic limits as the set reports them (read_limits).
static int limits[AMY_WAVETABLE_LADDER_RUNGS];

// A synthetic table: every frame the same sum of sines, amplitude 12000 / k
// for k = 1..127, so the series cut at any limit is known exactly.
static float series(int n, int limit) {
    float y = 0;
    for (int k = 1; k <= limit; ++k)
        y += (12000.0f / (float)k) * sinf(2.0f * (float)M_PI * (float)k * (float)n / (float)CYCLE);
    return y;
}

static void load_table(void) {
    int16_t *buf = pcm_load(TABLE_PRESET, TABLE_LEN, 44100, 1, 69.0f, 0, 0);
    for (int f = 0; f < FRAMES; ++f)
        for (int n = 0; n < CYCLE; ++n)
            buf[f * CYCLE + n] = (int16_t)lroundf(series(n, HARMONICS));
}

// The same table with frame f scaled by (f + 1) / FRAMES, so frames far apart
// differ in level and every rung keeps its frame's scale.
static void load_scaled_table(void) {
    int16_t *buf = pcm_load(TABLE_PRESET, TABLE_LEN, 44100, 1, 69.0f, 0, 0);
    for (int f = 0; f < FRAMES; ++f)
        for (int n = 0; n < CYCLE; ++n)
            buf[f * CYCLE + n] = (int16_t)lroundf(series(n, HARMONICS) * (float)(f + 1) / (float)FRAMES);
}

// A copy of rung r as its own preset, so a plain osc can play it alone.
static void load_rung_as_preset(uint16_t preset, int r) {
    const int16_t *rung = pcm_wavetable_lutset(TABLE_PRESET)[1 + r].table;
    int16_t *buf = pcm_load(preset, TABLE_LEN, 44100, 1, 69.0f, 0, 0);
    memcpy(buf, rung, TABLE_LEN * sizeof(int16_t));
}

static void start(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    amy_reset_sysclock();
}

static void render_blocks(int16_t *out) {
    for (int b = 0; b < BLOCKS; ++b) {
        int16_t *buf = amy_simple_fill_buffer();
        memcpy(out + b * AMY_BLOCK_SIZE * AMY_NCHANS, buf, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
    }
}

static void read_limits(void) {
    start();
    load_table();
    pcm_wavetable_ladder_build(TABLE_PRESET);
    const LUT *set = pcm_wavetable_lutset(TABLE_PRESET);
    for (int r = 0; r < AMY_WAVETABLE_LADDER_RUNGS; ++r)
        limits[r] = (set != NULL) ? set[1 + r].highest_harmonic : 0;
    amy_stop();
}

// Render one scene on a clean engine with the synthetic table loaded, with or
// without its ladder.
static void render_scene(const char *const *msgs, int n_msgs, int ladder, int16_t *out) {
    start();
    load_table();
    if (ladder) pcm_wavetable_ladder_build(TABLE_PRESET);
    for (int i = 0; i < n_msgs; ++i) amy_add_message((char *)msgs[i]);
    render_blocks(out);
    amy_stop();
}

// Render the note on a plain osc reading one rung alone (built on a fresh
// engine, copied to preset 101, the ladder freed so nothing can pick a rung).
static void render_on_rung_alone(const char *note_msg, int r, int16_t *out) {
    start();
    load_table();
    pcm_wavetable_ladder_build(TABLE_PRESET);
    load_rung_as_preset(101, r);
    pcm_wavetable_ladder_free(TABLE_PRESET);
    char setup[48];
    snprintf(setup, sizeof(setup), "v0w%dp101d0.3Z", WAVETABLE);
    amy_add_message(setup);
    amy_add_message((char *)note_msg);
    render_blocks(out);
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

// The pick rule, choose_from_lutset's: a table with highest harmonic H serves
// pitch f0 while 2 * H * f0 / sr < 0.9. The notes below are chosen so the
// expectations hold at 44.1 and 48 kHz: 131 Hz reads the base (127 * cps
// 0.35..0.38), 262 Hz rung 63 (0.75 and 0.37), 523 Hz rung 31 (0.75, 0.37).
static float midi_hz(float note) { return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f); }
static int passes(int limit, float hz) { return 2.0f * (float)limit * hz / (float)AMY_SAMPLE_RATE < 0.9f; }

// The largest difference between one cycle of a rung and the series cut at
// limit, scaled.
static int worst_lsb(const int16_t *rung, int limit, float scale) {
    int worst = 0;
    for (int n = 0; n < CYCLE; ++n) {
        int d = abs((int)rung[n] - (int)lroundf(scale * series(n, limit)));
        if (d > worst) worst = d;
    }
    return worst;
}

static void test_rung_is_the_series_cut(void) {
    printf("a rung is the frame's series cut at its limit\n");
    start();
    load_table();
    int built = pcm_wavetable_ladder_build(TABLE_PRESET);
    const LUT *set = pcm_wavetable_lutset(TABLE_PRESET);
    CHECK(built && set != NULL, "the ladder builds");
    for (int r = 0; set != NULL && r < AMY_WAVETABLE_LADDER_RUNGS; ++r) {
        int worst = worst_lsb(set[1 + r].table, limits[r], 1.0f);
        CHECK(worst <= 3, "rung %d (limit %d) within %d LSB of the series", r, limits[r], worst);
    }
    CHECK(set != NULL && set[0].highest_harmonic == 127
          && set[0].table == pcm_get_sample_ram_for_preset(TABLE_PRESET, NULL),
          "the set starts with the base table at 127");
    CHECK(limits[0] == 63 && limits[1] == 31 && limits[2] == 15 && limits[3] == 7,
          "the rungs follow at 63, 31, 15 and 7");
    int n = 0;
    while (set != NULL && set[n].table_size > 0) n++;
    CHECK(n == 1 + AMY_WAVETABLE_LADDER_RUNGS, "the set holds the base and %d rungs, then the terminator", n - 1);
    amy_stop();
}

static void test_low_note_unchanged(void) {
    printf("a note the base table does not fold at renders exactly as without a ladder\n");
    char setup[48];
    snprintf(setup, sizeof(setup), "v0w%dp%dd0.3Z", WAVETABLE, TABLE_PRESET);
    const char *scene[] = {setup, "v0n48l1Z"};
    render_scene(scene, 2, 0, scene_a);
    render_scene(scene, 2, 1, scene_b);
    CHECK(passes(127, midi_hz(48)), "note 48 is below the base's limit at %d Hz", AMY_SAMPLE_RATE);
    CHECK(rms(scene_a) > 0, "the note renders (rms %.1f)", rms(scene_a));
    CHECK(memcmp(scene_a, scene_b, sizeof(scene_a)) == 0, "note 48 with the ladder == without");
}

static void test_high_note_reads_its_rung(void) {
    printf("a note above the base's limit renders exactly as a plain osc on its rung alone\n");
    const struct { int note; int rung; } cases[] = { {60, 0}, {72, 1} };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char setup[48], note[24];
        int limit = limits[cases[i].rung];
        int finer = cases[i].rung == 0 ? 127 : limits[cases[i].rung - 1];
        CHECK(passes(limit, midi_hz((float)cases[i].note)) && !passes(finer, midi_hz((float)cases[i].note)),
              "note %d wants rung %d at %d Hz", cases[i].note, limit, AMY_SAMPLE_RATE);
        snprintf(setup, sizeof(setup), "v0w%dp%dd0.3Z", WAVETABLE, TABLE_PRESET);
        snprintf(note, sizeof(note), "v0n%dl1Z", cases[i].note);
        const char *with_ladder[] = {setup, note};
        render_scene(with_ladder, 2, 1, scene_a);
        render_on_rung_alone(note, cases[i].rung, scene_b);
        CHECK(rms(scene_a) > 0, "note %d renders (rms %.1f)", cases[i].note, rms(scene_a));
        CHECK(memcmp(scene_a, scene_b, sizeof(scene_a)) == 0, "note %d with the ladder == plain osc on rung %d",
              cases[i].note, limit);
    }
}

// Two groups an octave apart: group 0 at note 60 (rung 63), group 1 at note
// 72 (rung 31), each group picked from its sharpest copy. The octave copies
// start at twice their root copy's phase plus THETA (0.21132487), every
// weight 1/2, as the unison tests pin.
static void test_cluster_groups_pick_their_own_rung(void) {
    printf("the groups of a cluster pick their own rungs\n");
    char setup[72];
    snprintf(setup, sizeof(setup), "v0w%dp%dd0.3C4,0.04,-0.02,1,2,1Z", WAVETABLE, TABLE_PRESET);
    const char *cluster[] = {setup, "v0n60l1Z"};
    render_scene(cluster, 2, 1, scene_a);
    start();
    load_table();
    pcm_wavetable_ladder_build(TABLE_PRESET);
    load_rung_as_preset(101, 0);
    load_rung_as_preset(102, 1);
    pcm_wavetable_ladder_free(TABLE_PRESET);
    char g0[64], g1[64], g2[64], g3[64];
    snprintf(g0, sizeof(g0), "v0w%dp101d0.3a0.5P0Z", WAVETABLE);
    snprintf(g1, sizeof(g1), "v1w%dp101d0.3a0.5P0.5Z", WAVETABLE);
    snprintf(g2, sizeof(g2), "v2w%dp102d0.3a0.5P0.21132487Z", WAVETABLE);
    snprintf(g3, sizeof(g3), "v3w%dp102d0.3a0.5P0.21132487Z", WAVETABLE);
    amy_add_message(g0); amy_add_message(g1); amy_add_message(g2); amy_add_message(g3);
    amy_add_message("v0n59.76l1Z"); amy_add_message("v1n60.24l1Z");
    amy_add_message("v2n71.76l1Z"); amy_add_message("v3n72.24l1Z");
    render_blocks(scene_b);
    amy_stop();
    double ref = rms(scene_b), diff = rms_diff(scene_a, scene_b);
    double db = (diff > 0) ? 20.0 * log10(diff / ref) : -INFINITY;
    CHECK(ref > 0, "the four oscs render (rms %.1f, cluster rms %.1f)", ref, rms(scene_a));
    CHECK(db < -40.0, "cluster - four oscs is %.1f dB below the four oscs", db);
}

// Two copies at note 72 -/+ 0.24 fanned by frame_spread 0.4 a fifth of the
// scaled table either side of duty 0.5, both reading rung 31, as two plain
// oscs at duty 0.3 and 0.7 on that rung alone, weights 1/sqrt(2), phases 0
// and 1/2.
static void test_frame_spread_copies_read_their_rung(void) {
    printf("a 2-copy cluster at frame_spread 0.4 reads its fanned frames from its rung\n");
    CHECK(passes(31, midi_hz(71.76f)) && !passes(63, midi_hz(71.76f))
          && passes(31, midi_hz(72.24f)) && !passes(63, midi_hz(72.24f)),
          "notes 71.76 and 72.24 want rung 31 at %d Hz", AMY_SAMPLE_RATE);
    char setup[72];
    snprintf(setup, sizeof(setup), "v0w%dp%dd0.5C2,0.04,-0.02,1,1,0,0,0,0,0.4Z", WAVETABLE, TABLE_PRESET);
    start();
    load_scaled_table();
    pcm_wavetable_ladder_build(TABLE_PRESET);
    amy_add_message(setup);
    amy_add_message("v0n72l1Z");
    render_blocks(scene_a);
    amy_stop();
    start();
    load_scaled_table();
    pcm_wavetable_ladder_build(TABLE_PRESET);
    load_rung_as_preset(101, 1);
    pcm_wavetable_ladder_free(TABLE_PRESET);
    char c0[64], c1[64];
    snprintf(c0, sizeof(c0), "v0w%dp101d0.3a0.70710678P0Z", WAVETABLE);
    snprintf(c1, sizeof(c1), "v1w%dp101d0.7a0.70710678P0.5Z", WAVETABLE);
    amy_add_message(c0); amy_add_message(c1);
    amy_add_message("v0n71.76l1Z"); amy_add_message("v1n72.24l1Z");
    render_blocks(scene_b);
    amy_stop();
    double ref = rms(scene_b), diff = rms_diff(scene_a, scene_b);
    double db = (diff > 0) ? 20.0 * log10(diff / ref) : -INFINITY;
    CHECK(ref > 0, "the two oscs render (rms %.1f, cluster rms %.1f)", ref, rms(scene_a));
    CHECK(db < -40.0, "cluster - two oscs is %.1f dB below the two oscs", db);
}

static void test_rebuild_free_unload_and_refusals(void) {
    printf("a rebuild writes in place; free and unload drop the rungs; streamed, multichannel and short presets are refused\n");
    start();
    load_table();
    CHECK(pcm_wavetable_lutset(TABLE_PRESET) == NULL, "no set before a build");
    pcm_wavetable_ladder_build(TABLE_PRESET);
    const LUT *set = pcm_wavetable_lutset(TABLE_PRESET);
    const int16_t *before[AMY_WAVETABLE_LADDER_RUNGS];
    for (int r = 0; r < AMY_WAVETABLE_LADDER_RUNGS; ++r) before[r] = set[1 + r].table;
    // Rewrite the table at half level, then rebuild.
    int16_t *base = (int16_t *)pcm_get_sample_ram_for_preset(TABLE_PRESET, NULL);
    for (int i = 0; i < TABLE_LEN; ++i) base[i] = (int16_t)lroundf(0.5f * series(i % CYCLE, HARMONICS));
    pcm_wavetable_ladder_build(TABLE_PRESET);
    set = pcm_wavetable_lutset(TABLE_PRESET);
    int same = 1;
    for (int r = 0; r < AMY_WAVETABLE_LADDER_RUNGS; ++r) same &= (set[1 + r].table == before[r]);
    CHECK(same, "a rebuild writes the rungs in place");
    int worst = worst_lsb(set[2].table, limits[1], 0.5f);
    CHECK(worst <= 3, "the rebuilt rung %d follows the rewritten table (%d LSB)", limits[1], worst);
    pcm_wavetable_ladder_free(TABLE_PRESET);
    CHECK(pcm_wavetable_lutset(TABLE_PRESET) == NULL, "free drops the set");
    pcm_wavetable_ladder_build(TABLE_PRESET);
    pcm_unload_preset(TABLE_PRESET);
    CHECK(pcm_wavetable_lutset(TABLE_PRESET) == NULL, "unload drops the set");
    int16_t *stereo = pcm_load(102, 2 * CYCLE, 44100, 2, 69.0f, 0, 0);
    memset(stereo, 0, 2 * CYCLE * 2 * sizeof(int16_t));
    int16_t *tiny = pcm_load(103, CYCLE, 44100, 1, 69.0f, 0, 0);
    memset(tiny, 0, CYCLE * sizeof(int16_t));
    CHECK(pcm_wavetable_ladder_build(102) == 0 && pcm_wavetable_ladder_build(103) == 0
          && pcm_wavetable_ladder_build(999) == 0,
          "a stereo preset, one shorter than two cycles and a missing one are refused");
    amy_stop();
}

static void test_rom_wavetable(void) {
    printf("a ROM wavetable takes a ladder too\n");
    start();
    int built = pcm_wavetable_ladder_build(pcm_wavetable_base);
    const LUT *set = pcm_wavetable_lutset(pcm_wavetable_base);
    CHECK(built && set != NULL && set[0].table == pcm_get_sample_ram_for_preset(pcm_wavetable_base, NULL)
          && set[1].highest_harmonic == limits[0],
          "the first ROM wavetable builds and its set starts with the ROM table");
    pcm_wavetable_ladder_free(pcm_wavetable_base);
    CHECK(pcm_wavetable_lutset(pcm_wavetable_base) == NULL, "free drops it");
    pcm_wavetable_ladder_build(pcm_wavetable_base);
    amy_stop();
    start();
    CHECK(pcm_wavetable_lutset(pcm_wavetable_base) == NULL, "amy_stop drops it");
    amy_stop();
}

// A wavetable note on rung 31 keeps no pointer into the rung set between
// blocks; switched to a saw mid-note, the saw picks its own table.
static void test_wave_switch_leaves_the_set(void) {
    printf("a switch to another wave mid-note stops reading the rungs\n");
    start();
    load_table();
    pcm_wavetable_ladder_build(TABLE_PRESET);
    const LUT *set = pcm_wavetable_lutset(TABLE_PRESET);
    char setup[48], saw[24];
    snprintf(setup, sizeof(setup), "v0w%dp%dd0.3Z", WAVETABLE, TABLE_PRESET);
    snprintf(saw, sizeof(saw), "v0w%dZ", SAW_DOWN);
    amy_add_message(setup);
    amy_add_message("v0n72l1Z");
    amy_simple_fill_buffer();
    amy_simple_fill_buffer();
    CHECK(synth[0]->lut == NULL, "the wavetable note keeps no pointer into the set between blocks");
    amy_add_message(saw);
    amy_simple_fill_buffer();
    CHECK(synth[0]->lut != NULL && (synth[0]->lut < set || synth[0]->lut > &set[AMY_WAVETABLE_LADDER_RUNGS + 1]),
          "after the switch the saw reads a table outside the set");
    amy_stop();
}

int main(void) {
    read_limits();
    test_rung_is_the_series_cut();
    test_low_note_unchanged();
    test_high_note_reads_its_rung();
    test_cluster_groups_pick_their_own_rung();
    test_frame_spread_copies_read_their_rung();
    test_rebuild_free_unload_and_refusals();
    test_rom_wavetable();
    test_wave_switch_leaves_the_set();
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
