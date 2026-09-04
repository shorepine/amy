// Regression and behavior tests for reusable tagged sequencer sequences.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "amy.h"
#include "sequencer.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

typedef struct mark_t {
    char name[32];
    uint32_t tick;
} mark_t;

static mark_t marks[256];
static int mark_count = 0;

static void mark_hook(const char *code) {
    if (mark_count >= (int)(sizeof(marks) / sizeof(marks[0]))) return;
    snprintf(marks[mark_count].name, sizeof(marks[mark_count].name), "%s", code);
    marks[mark_count].tick = sequencer_ticks();
    mark_count++;
}

static void clear_marks(void) {
    mark_count = 0;
    memset(marks, 0, sizeof(marks));
}

static void clock_to(uint32_t target) {
    while (!AMY_TIME_GEQ(sequencer_ticks(), target)) sequencer_midi_clock_tick();
}

static uint32_t next_boundary(uint32_t now, uint32_t quantum) {
    uint32_t remainder = now % quantum;
    return now + (remainder == 0 ? quantum : quantum - remainder);
}

static int mark_at(const char *name, uint32_t tick) {
    for (int i = 0; i < mark_count; ++i)
        if (!strcmp(marks[i].name, name) && marks[i].tick == tick) return 1;
    return 0;
}

static int marks_named(const char *name) {
    int count = 0;
    for (int i = 0; i < mark_count; ++i)
        if (!strcmp(marks[i].name, name)) count++;
    return count;
}

static void test_untagged_ticks_and_cumulative_tags(void) {
    printf("untagged root ticks and cumulative tagged sequences\n");
    sequencer_reset();
    clear_marks();
    uint32_t first = next_boundary(sequencer_ticks(), 4);

    amy_add_message("H0,4zProotZ");
    clock_to(first + 4);
    CHECK(mark_at("root", first), "periodic root event fires at global modulo");
    CHECK(mark_at("root", first + 4), "periodic root event keeps looping");
    sequencer_reset();

    clear_marks();
    amy_add_message("H0,0,9zPfirstZ");
    amy_add_message("H2,0,9zPsecondZ");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("HC9,1,4Z");
    clock_to(start + 2);
    CHECK(mark_at("first", start) && mark_at("second", start + 2),
          "repeating a tag cumulates ordinary events into one sequence");
}

static void test_legacy_c_event_wire_is_unchanged(void) {
    printf("legacy C events retain three-value ticks\n");
    amy_event event = amy_default_event();
    event.osc = 2;
    event.wave = TRIANGLE;
    event.ticks[TICKS_TICK] = 3;
    event.ticks[TICKS_PERIOD] = 8;
    event.ticks[TICKS_TAG] = 7;
    char wire[MAX_MESSAGE_LEN];
    sprint_event(&event, wire, sizeof(wire), true);
    CHECK(strncmp(wire, "H3,8,7", 6) == 0,
          "C ticks serialization remains three values: %s", wire);
}

static void test_repeated_tag_and_one_shot_lifetime(void) {
    printf("repeated tagged events accumulate and finite events retire\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,10zPzeroZ");
    amy_add_message("H2,0,10zPtwoZ");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("HC10,1,4Z");
    clock_to(start + 4);
    CHECK(mark_at("zero", start), "local tick zero fires at activation");
    CHECK(mark_at("two", start + 2), "a second event shares the same tag");
    CHECK(marks_named("zero") == 1 && marks_named("two") == 1,
          "period-zero sequence events fire once and execution retires");
}

static void test_empty_tick_zero_is_reset_but_payload_is_an_event(void) {
    printf("empty tick-zero reset remains distinct from a tick-zero event\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,10zPstoredZ");
    amy_add_message("H0,0,10Z");
    CHECK(!sequencer_sequence_control(10, SEQUENCE_CONTROL_START, 0, 0),
          "an empty H0,0,tag resets that tag");
    amy_add_message("H0,0,10zPstoredZ");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("HC10,1,4Z");
    clock_to(start);
    CHECK(mark_at("stored", start),
          "H0,0,tag with a payload is a local tick-zero event");
}

static void test_active_definition_is_immutable(void) {
    printf("active executions retain the definition they started with\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,11zPold-headZ");
    amy_add_message("H4,0,11zPold-tailZ");
    uint32_t old_start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("HC11,1,4Z");
    clock_to(old_start + 2);

    amy_add_message("HR11Z");
    amy_add_message("H0,0,11zPnew-headZ");
    clock_to(old_start + 4);
    CHECK(mark_at("old-tail", old_start + 4),
          "resetting future contents does not remove an old note release");

    clear_marks();
    uint32_t new_start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("HC11,1,4Z");
    clock_to(new_start + 2);
    CHECK(mark_at("new-head", new_start) && !marks_named("old-head")
          && !marks_named("old-tail"),
          "a later start uses only the replacement definition");
}

static void test_append_while_active_uses_copy_on_write(void) {
    printf("appending while active publishes a future definition\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,11zPbaseZ");
    amy_add_message("H6,0,11zPold-tailZ");
    amy_add_message("HC11,1,0Z");
    uint32_t old_start = sequencer_ticks() + 1;
    clock_to(old_start + 1);

    amy_add_message("H2,0,11zPappendedZ");
    clock_to(old_start + 6);
    CHECK(mark_at("base", old_start) && mark_at("old-tail", old_start + 6),
          "the active execution retains its original events");
    CHECK(!mark_at("appended", old_start + 2),
          "an append cannot enter an already-running snapshot");

    clear_marks();
    amy_add_message("HC11,1,0Z");
    uint32_t new_start = sequencer_ticks() + 1;
    clock_to(new_start + 6);
    CHECK(mark_at("base", new_start)
          && mark_at("appended", new_start + 2)
          && mark_at("old-tail", new_start + 6),
          "a later execution sees the cumulative appended definition");
}

static void test_three_definition_generations_overlap(void) {
    printf("three immutable definition generations can overlap\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,12zPbaseZ");
    amy_add_message("H12,0,12zPtailZ");

    amy_add_message("HC12,1,0Z");
    uint32_t first_start = sequencer_ticks() + 1;
    clock_to(first_start);

    amy_add_message("H2,0,12zPsecondZ");
    amy_add_message("HC12,1,0Z");
    uint32_t second_start = sequencer_ticks() + 1;
    clock_to(second_start);

    amy_add_message("H4,0,12zPthirdZ");
    amy_add_message("HC12,1,0Z");
    uint32_t third_start = sequencer_ticks() + 1;
    clock_to(third_start + 12);

    CHECK(mark_at("base", first_start)
          && !mark_at("second", first_start + 2)
          && !mark_at("third", first_start + 4),
          "the first execution keeps generation one");
    CHECK(mark_at("base", second_start)
          && mark_at("second", second_start + 2)
          && !mark_at("third", second_start + 4),
          "the second execution keeps generation two");
    CHECK(mark_at("base", third_start)
          && mark_at("second", third_start + 2)
          && mark_at("third", third_start + 4),
          "the third execution sees generation three");
}

static void test_root_launches_local_zero_on_same_tick(void) {
    printf("root events can launch stored sequences\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,12zPchild-zeroZ");
    uint32_t start = sequencer_ticks() + 4;
    char wire[96];
    snprintf(wire, sizeof(wire), "H%" PRIu32 ",0HC12,1,0Z", start);
    amy_add_message(wire);
    clock_to(start);
    CHECK(mark_at("child-zero", start),
          "a root launch includes the child's local tick zero");
}

static void test_root_can_reset_a_future_definition(void) {
    printf("root events can reset future stored definitions\n");
    sequencer_reset();
    amy_add_message("H0,0,12zPfutureZ");
    uint32_t reset_tick = sequencer_ticks() + 2;
    char wire[96];
    snprintf(wire, sizeof(wire), "H%" PRIu32 ",0HR12Z", reset_tick);
    amy_add_message(wire);
    clock_to(reset_tick);
    CHECK(!sequencer_sequence_control(12, SEQUENCE_CONTROL_START, 0, 0),
          "a render-fired reset removes the future definition");
}

static void test_overlapping_executions_need_no_host_identity(void) {
    printf("one sequence tag supports bounded overlapping executions\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,13zPonZ");
    amy_add_message("H4,0,13zPoffZ");
    uint32_t first = next_boundary(sequencer_ticks(), 4);
    char wire[96];
    snprintf(wire, sizeof(wire), "H%" PRIu32 ",0HC13,1,0Z", first);
    amy_add_message(wire);
    snprintf(wire, sizeof(wire), "H%" PRIu32 ",0HC13,1,0Z", first + 2);
    amy_add_message(wire);
    clock_to(first + 6);
    CHECK(mark_at("on", first) && mark_at("on", first + 2),
          "two starts of one tag can overlap");
    CHECK(mark_at("off", first + 4) && mark_at("off", first + 6),
          "each overlap retains its own scheduled release");
}

static void test_parent_stop_leaves_started_child_to_finish(void) {
    printf("stopping a parent prevents future children without truncating one\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,15zPnote-onZ");
    amy_add_message("H4,0,15zPnote-offZ");
    amy_add_message("H0,4,14HC15,1,0Z");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("HC14,1,4Z");
    clock_to(start + 2);
    amy_add_message("HC14,0,0Z");
    clock_to(start + 8);
    CHECK(mark_at("note-on", start), "parent starts its child");
    CHECK(mark_at("note-off", start + 4),
          "the already-started child delivers its own note-off");
    CHECK(marks_named("note-on") == 1,
          "the stopped parent launches no later child");
}

static void test_controller_sequence_bounds_repetition(void) {
    printf("a finite controller sequence can bound a periodic child\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,4,8zPpulseZ");
    amy_add_message("H0,0,7HC8,1,0Z");
    amy_add_message("H12,0,7HC8,0,0Z");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("HC7,1,4Z");
    clock_to(start + 14);
    CHECK(mark_at("pulse", start) && mark_at("pulse", start + 4)
          && mark_at("pulse", start + 8),
          "controller permits exactly three periods");
    CHECK(!mark_at("pulse", start + 12) && marks_named("pulse") == 3,
          "same-tick stop precedes the child's ordinary event");
}

static void test_finite_gate_preserves_phase(void) {
    printf("finite event gating preserves the target phase\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,4,6zPbeatZ");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("HC6,1,4Z");
    clock_to(start);
    CHECK(mark_at("beat", start), "loop begins on its aligned boundary");
    CHECK(sequencer_sequence_control(6, SEQUENCE_CONTROL_GATE, 6, 0),
          "finite gate is accepted without a host timer");
    clock_to(start + 8);
    CHECK(!mark_at("beat", start + 4), "event inside gate is suppressed");
    CHECK(mark_at("beat", start + 8),
          "event resumes on the original phase after gate expiry");
}

static void test_per_tag_and_global_reset_semantics(void) {
    printf("per-tag replacement and global reset have distinct scopes\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,5zPsurvivorZ");
    amy_add_message("HC5,1,0Z");
    uint32_t start = sequencer_ticks() + 1;
    amy_add_message("HR5Z");
    clock_to(start);
    CHECK(mark_at("survivor", start),
          "per-tag reset leaves an already-started snapshot alive");
    CHECK(!sequencer_sequence_control(5, SEQUENCE_CONTROL_START, 0, 0),
          "per-tag reset removed the future definition");

    amy_add_message("H0,4,5zPclearedZ");
    amy_add_message("HC5,1,0Z");
    sequencer_reset();
    CHECK(!sequencer_sequence_control(5, SEQUENCE_CONTROL_START, 0, 0),
          "global RESET_SEQUENCER clears stored definitions");
}

static void test_timebase_reset_keeps_definitions(void) {
    printf("timebase reset drops runtime but keeps definitions\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,4zPafter-rebaseZ");
    amy_add_message("HC4,1,0Z");
    sequencer_sequence_reset_timebase();
    clock_to(sequencer_ticks() + 2);
    CHECK(!marks_named("after-rebase"), "pending execution is discarded");
    CHECK(sequencer_sequence_control(4, SEQUENCE_CONTROL_START, 0, 0),
          "definition remains available after timebase reset");
    uint32_t start = sequencer_ticks() + 1;
    clock_to(start);
    CHECK(mark_at("after-rebase", start), "definition can be relaunched");
}

static void test_bounds_and_validation(void) {
    printf("tag, event and execution bounds fail deterministically\n");
    sequencer_reset();
    CHECK(!sequencer_sequence_add_wire(16, 0, 0, strdup("zPbad-tagZ")),
          "first tag beyond max_sequencer_tags is rejected");
    CHECK(!sequencer_sequence_add_wire(3, 4, 4, strdup("zPbad-periodZ")),
          "tick equal to period is rejected");
    CHECK(!sequencer_sequence_add_wire(3, 0, 0, strdup("")),
          "empty payload is rejected");
    CHECK(!sequencer_sequence_add_wire(3, 0, 0, strdup("H0,0,1zPbadZ")),
          "stored sequences cannot edit definitions recursively");

    for (uint32_t i = 0; i < 8; ++i) {
        char *payload = strdup("zPfullZ");
        CHECK(sequencer_sequence_add_wire(3, i, 0, payload),
              "event slot %" PRIu32 " is available", i);
    }
    CHECK(!sequencer_sequence_add_wire(3, 9, 0, strdup("zPoverflowZ")),
          "one event beyond configured capacity is rejected");

    for (uint32_t i = 0; i < 8; ++i)
        CHECK(sequencer_sequence_control(3, SEQUENCE_CONTROL_START, 0, 64),
              "execution slot %" PRIu32 " is available", i);
    CHECK(!sequencer_sequence_control(3, SEQUENCE_CONTROL_START, 0, 64),
          "one execution beyond configured capacity is rejected");
    CHECK(!sequencer_sequence_control(3, 99, 0, 0),
          "unknown control action is rejected");
}

static void test_start_crosses_clock_rollover(void) {
    printf("relative sequence phase crosses uint32 clock rollover\n");
    sequencer_reset();
    clear_marks();
    amy_add_message("H0,0,2zPwrap-zeroZ");
    amy_add_message("H2,0,2zPwrap-twoZ");
    amy_global.sequencer_tick_count = UINT32_MAX - 2;
    amy_add_message("HC2,1,4Z");
    clock_to(2);
    CHECK(mark_at("wrap-zero", 0), "aligned local zero fires after rollover");
    CHECK(mark_at("wrap-two", 2), "elapsed local time crosses rollover");
}

static void test_disabled_configuration(void) {
    printf("zero reusable-sequence capacities disable the feature safely\n");
    const uint32_t capacities[][2] = {{0, 8}, {8, 0}};
    for (size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
        amy_config_t config = amy_default_config();
        config.features.startup_bleep = 0;
        config.audio = AMY_AUDIO_IS_NONE;
        config.max_sequence_events = capacities[i][0];
        config.max_sequence_executions = capacities[i][1];
        amy_start(config);
        CHECK(!sequencer_sequence_add_wire(1, 0, 0, strdup("zPdisabledZ")),
              "append is disabled for zero capacity set %zu", i + 1);
        CHECK(!sequencer_sequence_control(1, SEQUENCE_CONTROL_START, 0, 0),
              "control is disabled for zero capacity set %zu", i + 1);
        amy_stop();
    }
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.audio = AMY_AUDIO_IS_NONE;
    config.amy_external_exec_hook = mark_hook;
    config.max_sequencer_tags = 16;
    config.max_sequence_events = 8;
    config.max_sequence_executions = 8;
    amy_start(config);

    test_untagged_ticks_and_cumulative_tags();
    test_legacy_c_event_wire_is_unchanged();
    test_repeated_tag_and_one_shot_lifetime();
    test_empty_tick_zero_is_reset_but_payload_is_an_event();
    test_active_definition_is_immutable();
    test_append_while_active_uses_copy_on_write();
    test_three_definition_generations_overlap();
    test_root_launches_local_zero_on_same_tick();
    test_root_can_reset_a_future_definition();
    test_overlapping_executions_need_no_host_identity();
    test_parent_stop_leaves_started_child_to_finish();
    test_controller_sequence_bounds_repetition();
    test_finite_gate_preserves_phase();
    test_per_tag_and_global_reset_semantics();
    test_timebase_reset_keeps_definitions();
    test_start_crosses_clock_rollover();
    test_bounds_and_validation();

    amy_stop();
    test_disabled_configuration();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall reusable sequencer sequence checks passed\n");
    return 0;
}
