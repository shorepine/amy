// Two-level sequencer patterns: wire/C authoring, one-shot and loop playback,
// quantized activation, immutable committed versions and lane priority.
//
// The existing H sequencer is intentionally exercised in the same process:
// the new J/zQ paths must not change its modulo, tag or wire behavior.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "amy.h"
#include "sequencer.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

typedef struct mark_t {
    char name[24];
    uint32_t tick;
} mark_t;

static mark_t marks[128];
static int mark_count = 0;

static void mark_hook(const char *code) {
    if (mark_count >= (int)(sizeof(marks) / sizeof(marks[0]))) return;
    snprintf(marks[mark_count].name, sizeof(marks[mark_count].name), "%s",
             code);
    marks[mark_count].tick = sequencer_ticks();
    mark_count++;
}

static void clear_marks(void) {
    mark_count = 0;
    bzero(marks, sizeof(marks));
}

static void clock_to(uint32_t target) {
    while (sequencer_ticks() < target) sequencer_midi_clock_tick();
}

static uint32_t next_boundary(uint32_t now, uint32_t quantum) {
    uint32_t remainder = now % quantum;
    return now + (remainder == 0 ? quantum : quantum - remainder);
}

static int mark_at(const char *name, uint32_t tick) {
    for (int i = 0; i < mark_count; ++i) {
        if (!strcmp(marks[i].name, name) && marks[i].tick == tick) return 1;
    }
    return 0;
}

static int marks_named(const char *name) {
    int count = 0;
    for (int i = 0; i < mark_count; ++i)
        if (!strcmp(marks[i].name, name)) count++;
    return count;
}

static void test_existing_h_is_unchanged(void) {
    printf("existing H wire semantics remain unchanged\n");
    sequencer_reset();
    clear_marks();
    uint32_t now = sequencer_ticks();
    uint32_t first = now + (4 - now % 4);
    if (first == now) first += 4;

    amy_add_message("H0,4,0zProotZ");
    clock_to(first + 4);
    CHECK(mark_at("root", first), "H period event fires at old global modulo");
    CHECK(mark_at("root", first + 4), "H period event keeps looping");
    amy_add_message("H0,0,0Z");
}

static void test_existing_c_ticks_are_unchanged(void) {
    printf("existing C amy_event tick scheduling remains unchanged\n");
    sequencer_reset();
    amy_add_message("S0Z");
    amy_execute_deltas();
    uint32_t target = sequencer_ticks() + 4;
    amy_event event = amy_default_event();
    event.osc = 0;
    event.wave = TRIANGLE;
    event.ticks[TICKS_TICK] = target;
    event.ticks[TICKS_PERIOD] = 0;
    event.ticks[TICKS_TAG] = 12;
    amy_add_event(&event);
    clock_to(target - 2);
    amy_execute_deltas();
    CHECK(synth[0] == NULL || synth[0]->wave != TRIANGLE,
          "C tick event does not fire early");
    clock_to(target);
    amy_execute_deltas();
    CHECK(synth[0] != NULL && synth[0]->wave == TRIANGLE,
          "C tick event fires at its original absolute tick");
}

static void test_wire_one_shot_and_loop(void) {
    printf("wire-authored pattern supports one-shot and loop modes\n");
    sequencer_reset();
    clear_marks();

    amy_add_message("zQB0,8,2,0Z");
    amy_add_message("J0,0,8,0zPwire0Z");
    amy_add_message("J0,2,8,1zPwire2Z");
    amy_add_message("zQC0Z");

    uint32_t one_start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQT0,0,4Z");
    CHECK(!marks_named("wire0"),
          "an asynchronous trigger never fires inside the API call");
    clock_to(one_start - 2);
    CHECK(!marks_named("wire0"), "quantized one-shot waits for its boundary");
    clock_to(one_start + 10);
    CHECK(mark_at("wire0", one_start), "one-shot emits local tick zero");
    CHECK(mark_at("wire2", one_start + 2), "one-shot emits relative tick two");
    CHECK(marks_named("wire0") == 1 && marks_named("wire2") == 1,
          "one-shot does not wrap");

    clear_marks();
    uint32_t loop_start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQT0,1,4,41Z");
    clock_to(loop_start + 10);
    CHECK(mark_at("wire0", loop_start)
          && mark_at("wire0", loop_start + 8),
          "loop wraps at pattern length");
    CHECK(mark_at("wire2", loop_start + 2)
          && mark_at("wire2", loop_start + 10),
          "loop preserves relative offsets");

    uint32_t stop = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQS41,4Z");
    clock_to(stop + 8);
    CHECK(!mark_at("wire0", stop), "quantized stop suppresses boundary event");
}

static void test_c_event_api(void) {
    printf("C event API uses the same tick/period/tag pattern model\n");
    sequencer_reset();
    amy_event event = amy_default_event();
    event.osc = 0;
    event.wave = SINE;
    event.midi_note = 60;
    event.velocity = 1;
    event.ticks[TICKS_TICK] = 0;
    event.ticks[TICKS_PERIOD] = 4;
    event.ticks[TICKS_TAG] = 0;

    CHECK(amy_pattern_begin(1, 4, 3, 0), "C API begins staging pattern");
    CHECK(amy_pattern_add_event(1, &event), "C API stores amy_event");
    CHECK(amy_pattern_commit(1), "C API commits atomically");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(1, AMY_PATTERN_ONE_SHOT, 4,
                              AMY_PATTERN_UNTAGGED),
          "C API arms quantized one-shot");
    clock_to(start);
    amy_execute_deltas();
    CHECK(synth[0] != NULL && synth[0]->status == SYNTH_AUDIBLE,
          "C-authored event reaches normal AMY event playback");
    amy_add_message("v0l0Z");
    amy_execute_deltas();
}

static void test_committed_version_survives_replacement(void) {
    printf("running instance retains committed definition version\n");
    sequencer_reset();
    clear_marks();

    CHECK(amy_pattern_begin(2, 8, 4, 0), "begin old definition");
    CHECK(amy_pattern_add_wire(2, 0, 8, 0, true, "zPold0Z"),
          "store old tick zero");
    CHECK(amy_pattern_add_wire(2, 6, 8, 1, true, "zPold6Z"),
          "store old tail");
    CHECK(amy_pattern_commit(2), "commit old definition");
    uint32_t old_start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(2, AMY_PATTERN_ONE_SHOT, 4,
                              AMY_PATTERN_UNTAGGED),
          "trigger old definition");

    CHECK(amy_pattern_begin(2, 8, 4, 0), "begin replacement");
    CHECK(amy_pattern_add_wire(2, 0, 8, 0, true, "zPnew0Z"),
          "store replacement");
    CHECK(amy_pattern_commit(2), "commit replacement while old is pending");
    clock_to(old_start + 6);
    CHECK(mark_at("old0", old_start) && mark_at("old6", old_start + 6),
          "old one-shot finishes after definition replacement");
    CHECK(!marks_named("new0"), "replacement did not leak into old instance");

    uint32_t new_start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(2, AMY_PATTERN_ONE_SHOT, 4,
                              AMY_PATTERN_UNTAGGED),
          "trigger replacement");
    clock_to(new_start);
    CHECK(mark_at("new0", new_start), "next instance uses replacement");
}

static void test_lane_priority_suppresses_onsets_only(void) {
    printf("higher-priority pattern owns its lane for its exact duration\n");
    sequencer_reset();
    clear_marks();

    CHECK(amy_pattern_begin(3, 4, 7, 0), "begin background");
    CHECK(amy_pattern_add_wire(3, 0, 4, 0, true, "zPbg0Z"),
          "background tick zero");
    CHECK(amy_pattern_add_wire(3, 2, 4, 1, true, "zPbg2Z"),
          "background tick two");
    CHECK(amy_pattern_commit(3), "commit background");
    uint32_t background_start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(3, AMY_PATTERN_LOOP, 4, 77),
          "start background loop");
    clock_to(background_start + 2);
    CHECK(mark_at("bg0", background_start)
          && mark_at("bg2", background_start + 2),
          "background sounds before overlay");

    CHECK(amy_pattern_begin(4, 4, 7, 1), "begin lane overlay");
    CHECK(amy_pattern_add_wire(4, 0, 4, 0, true, "zPfill0Z"),
          "overlay tick zero");
    CHECK(amy_pattern_add_wire(4, 2, 4, 1, true, "zPfill2Z"),
          "overlay tick two");
    CHECK(amy_pattern_commit(4), "commit lane overlay");
    uint32_t fill_start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(4, AMY_PATTERN_ONE_SHOT, 4,
                              AMY_PATTERN_UNTAGGED),
          "arm overlay");
    clock_to(fill_start + 4);
    CHECK(mark_at("fill0", fill_start)
          && mark_at("fill2", fill_start + 2),
          "overlay events sound");
    CHECK(!mark_at("bg0", fill_start) && !mark_at("bg2", fill_start + 2),
          "lower-priority lane onsets are suppressed");
    CHECK(mark_at("bg0", fill_start + 4),
          "background resumes on first tick after overlay duration");
    amy_pattern_stop(77, 0);
    clock_to(sequencer_ticks() + 2);
}

static void test_third_level_is_rejected(void) {
    printf("pattern payloads cannot create a third sequencer level\n");
    CHECK(amy_pattern_begin(5, 4, 0, 0), "begin leaf-only definition");
    CHECK(!amy_pattern_add_wire(5, 0, 4, 0, true, "H0,4v0l1Z"),
          "root H payload rejected");
    CHECK(!amy_pattern_add_wire(5, 0, 4, 0, true, "J0,0,4,0v0l1Z"),
          "nested J payload rejected");
    CHECK(!amy_pattern_add_wire(5, 0, 4, 0, true, "zQT0,0,0Z"),
          "pattern trigger payload rejected");
    CHECK(!amy_pattern_add_wire(5, 0, 4, 0, true, "v0zQT0,0,0Z"),
          "pattern trigger after an ordinary field is also rejected");
    CHECK(!amy_pattern_add_wire(5, 0, 4, 0, true, "v0l1"),
          "unterminated pattern payload rejected");
    amy_pattern_clear(5);
}

static void test_root_can_trigger_local_tick_zero(void) {
    printf("a root sequence event can trigger pattern tick zero atomically\n");
    sequencer_reset();
    clear_marks();
    CHECK(amy_pattern_begin(6, 4, 1, 0), "begin root-triggered pattern");
    CHECK(amy_pattern_add_wire(6, 0, 4, 0, true, "zProot-childZ"),
          "store local tick zero");
    CHECK(amy_pattern_commit(6), "commit root-triggered pattern");

    uint32_t start = next_boundary(sequencer_ticks(), 4);
    char wire[64];
    snprintf(wire, sizeof(wire), "H%" PRIu32 ",0,21zQT6,0,4Z", start);
    amy_add_message(wire);
    clock_to(start);
    CHECK(mark_at("root-child", start),
          "root trigger and child tick zero share one sequencer tick");
}

static void test_pattern_event_tag_semantics(void) {
    printf("pattern tags and anonymous events match root sequencer semantics\n");
    sequencer_reset();
    clear_marks();
    CHECK(amy_pattern_begin(7, 4, 0, 0), "begin tag-semantics pattern");
    CHECK(amy_pattern_add_wire(7, 0, 4, 3, true, "zPclearedZ"),
          "store tagged event");
    CHECK(!amy_pattern_add_wire(7, 0, 0, 3, true, "zPignoredZ"),
          "zero/zero with a tag clears it");
    CHECK(!amy_pattern_add_wire(7, 0, 0, 0, false, "zPignoredZ"),
          "anonymous zero/zero remains a no-op");
    CHECK(amy_pattern_add_wire(7, 1, 4, 0, false, "zPanon-aZ"),
          "first anonymous event is stored");
    CHECK(amy_pattern_add_wire(7, 1, 4, 0, false, "zPanon-bZ"),
          "second anonymous event does not replace the first");
    CHECK(amy_pattern_commit(7), "commit tag-semantics pattern");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(7, AMY_PATTERN_ONE_SHOT, 4,
                              AMY_PATTERN_UNTAGGED),
          "trigger tag-semantics pattern");
    clock_to(start + 1);
    CHECK(!marks_named("cleared"), "cleared tag never fires");
    CHECK(mark_at("anon-a", start + 1) && mark_at("anon-b", start + 1),
          "anonymous entries coexist at the same local tick");
}

static void test_reset_stops_instances_but_keeps_definitions(void) {
    printf("RESET_SEQUENCER clears playback but preserves stored definitions\n");
    sequencer_reset();
    clear_marks();
    CHECK(amy_pattern_begin(8, 4, 0, 0), "begin reset-survival pattern");
    CHECK(amy_pattern_add_wire(8, 0, 4, 0, true, "zPsurvivorZ"),
          "store reset-survival event");
    CHECK(amy_pattern_commit(8), "commit reset-survival pattern");
    uint32_t first_start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(8, AMY_PATTERN_LOOP, 4, 88),
          "start loop before reset");
    clock_to(first_start);
    CHECK(mark_at("survivor", first_start), "loop sounded before reset");

    clear_marks();
    sequencer_reset();
    clock_to(first_start + 4);
    CHECK(!marks_named("survivor"), "reset stopped the running instance");

    uint32_t second_start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(8, AMY_PATTERN_ONE_SHOT, 4,
                              AMY_PATTERN_UNTAGGED),
          "stored definition is still triggerable");
    clock_to(second_start);
    CHECK(mark_at("survivor", second_start),
          "preserved definition plays after reset");
}

static void test_timebase_reset_preserves_local_phase(void) {
    printf("RESET_TIMEBASE preserves pattern phase and pending distances\n");
    sequencer_reset();
    clear_marks();
    CHECK(amy_pattern_begin(9, 8, 0, 0), "begin phase pattern");
    CHECK(amy_pattern_add_wire(9, 3, 8, 0, true, "zPphase-threeZ"),
          "store local phase marker");
    CHECK(amy_pattern_commit(9), "commit phase pattern");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    CHECK(amy_pattern_trigger(9, AMY_PATTERN_LOOP, 4, 90),
          "start loop before timebase reset");
    clock_to(start + 2);
    clear_marks();

    amy_add_message("S16384Z");
    amy_simple_fill_buffer();
    CHECK(sequencer_ticks() == 0, "shared tick counter restarted at zero");
    sequencer_midi_clock_tick();
    CHECK(mark_at("phase-three", 1),
          "active loop continued at the same local phase");
    sequencer_reset();
}

static void test_pattern_activation_wraps_with_tick_clock(void) {
    printf("quantized pattern activation survives uint32 tick rollover\n");
    sequencer_reset();
    clear_marks();
    CHECK(amy_pattern_begin(10, 4, 0, 0), "begin rollover pattern");
    CHECK(amy_pattern_add_wire(10, 0, 4, 0, true, "zPwrappedZ"),
          "store rollover tick zero");
    CHECK(amy_pattern_commit(10), "commit rollover pattern");
    amy_global.sequencer_tick_count = UINT32_MAX - 2;
    CHECK(amy_pattern_trigger(10, AMY_PATTERN_ONE_SHOT, 4,
                              AMY_PATTERN_UNTAGGED),
          "arm activation across rollover");
    sequencer_midi_clock_tick();
    sequencer_midi_clock_tick();
    CHECK(mark_at("wrapped", 0), "local tick zero fired after wrap");
    sequencer_reset();
    amy_global.sequencer_tick_count = 0;
}

static void test_configured_bounds_are_enforced(void) {
    printf("configured pattern, tag and instance bounds are enforced\n");
    sequencer_reset();
    CHECK(amy_pattern_begin(31, 8, 0, 0), "last configured pattern is valid");
    CHECK(!amy_pattern_begin(32, 8, 0, 0),
          "first pattern past the configured range is refused");
    CHECK(amy_pattern_add_wire(31, 0, 8, 63, true, "zPlast-tagZ"),
          "last configured event tag is valid");
    CHECK(!amy_pattern_add_wire(31, 0, 8, 64, true, "zPbad-tagZ"),
          "first event tag past the configured range is refused");
    CHECK(amy_pattern_commit(31), "commit bounds pattern");
    for (int i = 0; i < 32; ++i) {
        CHECK(amy_pattern_trigger(31, AMY_PATTERN_ONE_SHOT, 64,
                                  AMY_PATTERN_UNTAGGED),
              "instance slot %d is available", i);
    }
    CHECK(!amy_pattern_trigger(31, AMY_PATTERN_ONE_SHOT, 64,
                               AMY_PATTERN_UNTAGGED),
          "one instance beyond the configured pool is refused");
    sequencer_reset();
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.audio = AMY_AUDIO_IS_NONE;
    config.amy_external_exec_hook = mark_hook;
    amy_start(config);

    test_existing_h_is_unchanged();
    test_existing_c_ticks_are_unchanged();
    test_wire_one_shot_and_loop();
    test_c_event_api();
    test_committed_version_survives_replacement();
    test_lane_priority_suppresses_onsets_only();
    test_third_level_is_rejected();
    test_root_can_trigger_local_tick_zero();
    test_pattern_event_tag_semantics();
    test_reset_stops_instances_but_keeps_definitions();
    test_timebase_reset_preserves_local_phase();
    test_pattern_activation_wraps_with_tick_clock();
    test_configured_bounds_are_enforced();

    amy_stop();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall nested sequencer checks passed\n");
    return 0;
}
