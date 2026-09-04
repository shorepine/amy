// Regression and behavior tests for reusable sequencer groups.

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

static int marks_named_at(const char *name, uint32_t tick) {
    int count = 0;
    for (int i = 0; i < mark_count; ++i)
        if (!strcmp(marks[i].name, name) && marks[i].tick == tick) count++;
    return count;
}

static void clear_group(uint32_t group) {
    char wire[32];
    snprintf(wire, sizeof(wire), "zQ%" PRIu32 ",4Z", group);
    amy_add_message(wire);
}

static void test_legacy_ticks_are_unchanged(void) {
    printf("legacy root ticks behavior remains unchanged\n");
    sequencer_reset();
    clear_marks();
    uint32_t first = next_boundary(sequencer_ticks(), 4);

    amy_add_message("H0,4,0zProotZ");
    clock_to(first + 4);
    CHECK(mark_at("root", first), "root period event fires at global modulo");
    CHECK(mark_at("root", first + 4), "root period event keeps looping");
    amy_add_message("H0,0,0Z");

    clear_marks();
    uint32_t target = sequencer_ticks() + 4;
    char wire[96];
    snprintf(wire, sizeof(wire), "H%" PRIu32 ",0,9zPoldZ", target);
    amy_add_message(wire);
    snprintf(wire, sizeof(wire), "H%" PRIu32 ",0,9zPnewZ", target);
    amy_add_message(wire);
    clock_to(target);
    CHECK(!marks_named("old") && mark_at("new", target),
          "legacy root tags still replace by tag");

    clear_marks();
    uint32_t group_zero = next_boundary(sequencer_ticks(), 4);
    amy_add_message("H0,4,5,0zPgroup-zero-rootZ");
    clock_to(group_zero);
    CHECK(mark_at("group-zero-root", group_zero),
          "an explicit group tag zero follows the legacy root path");
    amy_add_message("H0,0,5Z");
}

static void test_legacy_c_event_wire_is_unchanged(void) {
    printf("legacy C events keep their three-value ticks wire format\n");
    amy_event event = amy_default_event();
    event.osc = 2;
    event.wave = TRIANGLE;
    event.ticks[TICKS_TICK] = 3;
    event.ticks[TICKS_PERIOD] = 8;
    event.ticks[TICKS_TAG] = 7;

    char wire[MAX_MESSAGE_LEN];
    sprint_event(&event, wire, sizeof(wire), true);
    CHECK(strncmp(wire, "H3,8,7", 6) == 0
          && strncmp(wire, "H3,8,7,", 7) != 0,
          "an unset group field adds no fourth ticks value: %s", wire);

    event.ticks[TICKS_GROUP] = 2;
    sprint_event(&event, wire, sizeof(wire), true);
    CHECK(strncmp(wire, "H3,8,7,2", 8) == 0,
          "a grouped C event adds exactly one ticks value: %s", wire);
}

static void test_group_local_tags_are_independent(void) {
    printf("event tags are local to each sequencer group\n");
    sequencer_reset();
    clear_group(6);
    clear_group(7);
    clear_marks();
    amy_add_message("H0,4,0,6zPgroup-six-tag-zeroZ");
    amy_add_message("H0,4,0,7zPgroup-seven-tag-zeroZ");
    amy_add_message("H0,4,0zProot-tag-zeroZ");
    amy_add_message("zQ6,3,4Z");
    amy_add_message("zQ7,3,4Z");

    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ6,1,1,4Z");
    amy_add_message("zQ7,1,1,4Z");
    clock_to(start);
    CHECK(mark_at("group-six-tag-zero", start),
          "group 6 owns its event tag zero");
    CHECK(mark_at("group-seven-tag-zero", start),
          "group 7 independently owns event tag zero");
    CHECK(mark_at("root-tag-zero", start),
          "root tag zero remains independent of every group-local tag zero");
    amy_add_message("H0,0,0Z");
}

static void test_one_n_and_infinite_repeats(void) {
    printf("groups support one, N and infinite repeats\n");
    sequencer_reset();
    clear_group(1);
    clear_marks();
    amy_add_message("H0,4,0,1zPzeroZ");
    amy_add_message("H2,4,1,1zPtwoZ");
    amy_add_message("zQ1,3,4Z");

    uint32_t one = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ1,1,1,4Z");
    clock_to(one + 6);
    CHECK(mark_at("zero", one) && mark_at("two", one + 2),
          "one-shot uses local ticks from its activation");
    CHECK(marks_named("zero") == 1 && marks_named("two") == 1,
          "one-shot does not wrap");

    clear_marks();
    uint32_t twice = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ1,1,2,4Z");
    clock_to(twice + 10);
    CHECK(mark_at("zero", twice) && mark_at("zero", twice + 4),
          "repeat count two runs exactly two phrases");
    CHECK(marks_named("zero") == 2, "N-shot finishes after N phrases");

    clear_marks();
    uint32_t loop = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ1,1,0,4,77Z");
    clock_to(loop + 8);
    CHECK(mark_at("zero", loop) && mark_at("zero", loop + 8),
          "repeat count zero loops indefinitely");
    amy_add_message("zQ1,0,0,0,77Z");
    clock_to(loop + 12);
    CHECK(!mark_at("zero", loop + 12), "tagged stop ends the loop");
}

static void test_atomic_revision_lifetime(void) {
    printf("published revisions are atomic and immutable while active\n");
    sequencer_reset();
    clear_group(2);
    clear_marks();
    amy_add_message("H0,8,0,2zPold-zeroZ");
    amy_add_message("H6,8,1,2zPold-tailZ");
    amy_add_message("zQ2,3,8Z");

    uint32_t old_start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ2,1,1,4Z");
    amy_add_message("H0,8,0,2zPnew-zeroZ");
    amy_add_message("H0,0,1,2Z");

    uint32_t still_old = old_start + 8;
    char root[80];
    snprintf(root, sizeof(root), "H%" PRIu32 ",0,31zQ2,1,1,0Z", still_old);
    amy_add_message(root);
    clock_to(old_start + 6);
    CHECK(mark_at("old-zero", old_start) && mark_at("old-tail", old_start + 6),
          "an active execution finishes its original revision");

    clock_to(still_old);
    CHECK(mark_at("old-zero", still_old),
          "staged edits are invisible before publication");
    amy_add_message("zQ2,3,8Z");
    uint32_t new_start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ2,1,1,4Z");
    clock_to(new_start + 6);
    CHECK(mark_at("new-zero", new_start), "future execution uses published edit");
    CHECK(!mark_at("old-tail", new_start + 6), "published local-tag clear took effect");
}

static void test_root_launches_local_zero_on_same_tick(void) {
    printf("a root event can launch group local tick zero on the same tick\n");
    sequencer_reset();
    clear_group(3);
    clear_marks();
    amy_add_message("H0,4,0,3zPchildZ");
    amy_add_message("zQ3,3,4Z");

    uint32_t start = sequencer_ticks() + 4;
    char wire[80];
    snprintf(wire, sizeof(wire), "H%" PRIu32 ",0,22zQ3,1,1,0Z", start);
    amy_add_message(wire);
    clock_to(start);
    CHECK(mark_at("child", start), "root launch and group local zero coincide");
}

static void test_direct_start_begins_on_next_tick(void) {
    printf("an unquantized direct start begins on the next tick\n");
    sequencer_reset();
    clear_group(1);
    clear_marks();
    amy_add_message("H0,4,0,1zPnext-tickZ");
    amy_add_message("zQ1,3,4Z");

    uint32_t start = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(1, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "unquantized direct start is accepted");
    CHECK(!marks_named("next-tick"), "start does not fire synchronously");
    sequencer_midi_clock_tick();
    CHECK(mark_at("next-tick", start), "local tick zero fires on the next tick");
}

static void test_tagged_start_replaces_at_activation(void) {
    printf("a tagged start replaces its predecessor at the activation boundary\n");
    sequencer_reset();
    clear_group(2);
    clear_marks();
    amy_add_message("H0,2,0,2zPold-executionZ");
    amy_add_message("zQ2,3,2Z");
    uint32_t predecessor_start = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(2, SEQUENCE_CONTROL_START, 0, 0, 41, true),
          "the predecessor starts");
    sequencer_midi_clock_tick();
    CHECK(mark_at("old-execution", predecessor_start),
          "the predecessor is running before replacement");

    amy_add_message("H0,2,0,2zPnew-executionZ");
    amy_add_message("zQ2,3,2Z");
    clear_marks();
    uint32_t replacement = next_boundary(sequencer_ticks(), 4);
    CHECK(sequencer_group_control(2, SEQUENCE_CONTROL_START, 1, 4, 41, true),
          "the tagged replacement is accepted");
    clock_to(replacement);
    CHECK(!mark_at("old-execution", replacement),
          "the predecessor does not fire at the replacement boundary");
    CHECK(marks_named_at("new-execution", replacement) == 1,
          "exactly one replacement fires at the boundary");
}

static void test_c_event_uses_fourth_ticks_field(void) {
    printf("the C event API defines grouped events through ticks[3]\n");
    sequencer_reset();
    clear_group(6);
    amy_event event = amy_default_event();
    event.osc = 0;
    event.wave = TRIANGLE;
    event.ticks[TICKS_TICK] = 0;
    event.ticks[TICKS_PERIOD] = 4;
    event.ticks[TICKS_TAG] = 0;
    event.ticks[TICKS_GROUP] = 6;
    amy_add_event(&event);
    CHECK(sequencer_group_control(6, SEQUENCE_CONTROL_PUBLISH, 4, 0, 0, false),
          "C-authored grouped event publishes");
    CHECK(sequencer_group_control(6, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "C-authored group starts");
    clock_to(sequencer_ticks() + 2);
    amy_execute_deltas();
    CHECK(synth[0] != NULL && synth[0]->wave == TRIANGLE,
          "C-authored grouped event reaches normal playback");
}

static void test_quantized_gate_preserves_phase(void) {
    printf("finite event gating preserves local phase\n");
    sequencer_reset();
    clear_group(4);
    clear_group(5);
    clear_marks();
    amy_add_message("H0,2,0,4zPbackgroundZ");
    amy_add_message("zQ4,3,4Z");
    amy_add_message("H0,4,0,5zQ4,2,4,0,81Z");
    amy_add_message("H0,4,1,5zPforegroundZ");
    amy_add_message("zQ5,3,4Z");

    uint32_t background = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ4,1,0,4,81Z");
    clock_to(background + 2);
    CHECK(mark_at("background", background)
          && mark_at("background", background + 2),
          "background loop initially emits on phase");

    uint32_t foreground = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ5,1,1,4Z");
    clock_to(foreground + 4);
    CHECK(mark_at("foreground", foreground), "foreground group starts normally");
    CHECK(!mark_at("background", foreground)
          && !mark_at("background", foreground + 2),
          "gate suppresses events for its exact duration");
    CHECK(mark_at("background", foreground + 4),
          "background resumes on its unchanged phase");
    amy_add_message("zQ4,0,0,0,81Z");
    clock_to(foreground + 6);
}

static void test_quantized_stop_precedes_boundary_event(void) {
    printf("quantized stop takes effect before an event at its boundary\n");
    sequencer_reset();
    clear_group(6);
    clear_marks();
    amy_add_message("H0,4,0,6zPstoppedZ");
    amy_add_message("zQ6,3,4Z");
    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ6,1,0,4,91Z");
    clock_to(start);
    CHECK(mark_at("stopped", start), "loop starts on its boundary");

    uint32_t stop = next_boundary(sequencer_ticks(), 8);
    amy_add_message("zQ6,0,0,8,91Z");
    clock_to(stop);
    CHECK(!mark_at("stopped", stop), "stop suppresses the boundary event");
}

static void test_tagged_gate_and_stop_are_selective(void) {
    printf("execution tags make gate and stop selective\n");
    sequencer_reset();
    clear_group(3);
    clear_group(4);
    clear_marks();
    amy_add_message("H0,1,0,3zPsharedZ");
    amy_add_message("zQ3,3,8Z");
    amy_add_message("H0,1,0,4zPother-groupZ");
    amy_add_message("zQ4,3,8Z");
    amy_add_message("zQ3,1,0,0,101Z");
    amy_add_message("zQ3,1,0,0,102Z");
    amy_add_message("zQ4,1,0,0,101Z");
    sequencer_midi_clock_tick();
    CHECK(marks_named_at("shared", sequencer_ticks()) == 2,
          "two tagged executions of one group can overlap");
    CHECK(marks_named_at("other-group", sequencer_ticks()) == 1,
          "the same execution tag is independent in another group");

    clear_marks();
    CHECK(sequencer_group_control(3, SEQUENCE_CONTROL_GATE, 2, 0, 101, true),
          "a matching tagged gate is accepted");
    uint32_t gate_tick = sequencer_ticks() + 1;
    clock_to(gate_tick + 2);
    CHECK(marks_named_at("shared", gate_tick) == 1
          && marks_named_at("shared", gate_tick + 1) == 1,
          "only the selected execution is gated");
    CHECK(marks_named_at("shared", gate_tick + 2) == 2,
          "the selected execution resumes after the exact duration");
    CHECK(marks_named_at("other-group", gate_tick) == 1,
          "a tagged gate does not cross group boundaries");

    clear_marks();
    CHECK(sequencer_group_control(3, SEQUENCE_CONTROL_GATE, 100, 0, 101, true),
          "a longer tagged gate is accepted");
    uint32_t long_gate_tick = sequencer_ticks() + 1;
    clock_to(long_gate_tick);
    CHECK(marks_named_at("shared", long_gate_tick) == 1,
          "a positive gate duration suppresses the selected execution");
    uint32_t ungate_tick = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(3, SEQUENCE_CONTROL_GATE, 0, 0, 101, true),
          "gate duration zero requests an early ungate");
    clock_to(ungate_tick);
    CHECK(marks_named_at("shared", ungate_tick) == 2,
          "gate duration zero resumes the selected execution on its phase");

    clear_marks();
    uint32_t tagged_stop_tick = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(3, SEQUENCE_CONTROL_STOP, 0, 0, 102, true),
          "a matching tagged stop is accepted");
    clock_to(tagged_stop_tick);
    CHECK(marks_named_at("shared", tagged_stop_tick) == 1,
          "only the selected execution stops");
    CHECK(!sequencer_group_control(3, SEQUENCE_CONTROL_STOP, 0, 0, 999, true),
          "a nonmatching execution tag reports no affected execution");
    uint32_t all_stop_tick = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(3, SEQUENCE_CONTROL_STOP, 0, 0, 0, false),
          "an untagged stop selects every remaining execution in the group");
    clock_to(all_stop_tick);
    int remaining = marks_named_at("shared", all_stop_tick);
    CHECK(remaining == 0,
          "the untagged stop removed the remaining execution (got %d events)",
          remaining);
    CHECK(mark_at("other-group", all_stop_tick),
          "the untagged stop remains scoped to its group");
    amy_add_message("zQ4,0Z");
    sequencer_midi_clock_tick();
}

static void test_tagged_control_does_not_select_untagged_execution(void) {
    printf("tagged controls do not select untagged executions\n");
    sequencer_reset();
    clear_group(2);
    clear_marks();
    amy_add_message("H0,1,0,2zPuntaggedZ");
    amy_add_message("zQ2,3,1Z");
    CHECK(sequencer_group_control(2, SEQUENCE_CONTROL_START, 0, 0, 0, false),
          "an untagged execution starts");
    sequencer_midi_clock_tick();

    clear_marks();
    CHECK(!sequencer_group_control(2, SEQUENCE_CONTROL_STOP, 0, 0, 77, true),
          "a tagged stop reports no match for an untagged execution");
    sequencer_midi_clock_tick();
    CHECK(marks_named("untagged") == 2,
          "the unmatched tagged stop leaves the untagged execution running");
    CHECK(sequencer_group_control(2, SEQUENCE_CONTROL_STOP, 0, 0, 0, false),
          "an untagged stop still selects the execution");
    sequencer_midi_clock_tick();
}

static void test_group_lifecycle_control_is_not_recursive(void) {
    printf("a group payload cannot start, publish or clear a group\n");
    sequencer_reset();
    clear_group(7);
    clear_group(8);
    clear_marks();
    amy_add_message("H0,4,0,8zPpublished-revisionZ");
    amy_add_message("zQ8,3,4Z");
    amy_add_message("H0,4,0,8zPstaged-revisionZ");
    amy_add_message("H0,4,0,7zQ8,1,1,0Z");
    amy_add_message("H0,4,1,7zQ8,3,4Z");
    amy_add_message("H0,4,2,7zQ8,4Z");
    amy_add_message("zQ7,3,4Z");

    uint32_t start = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ7,1,1,4Z");
    clock_to(start);
    CHECK(!marks_named("published-revision") && !marks_named("staged-revision"),
          "group-to-group start is rejected");

    uint32_t old_revision_start = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(8, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "the target group can still be started directly");
    sequencer_midi_clock_tick();
    CHECK(mark_at("published-revision", old_revision_start),
          "nested clear was rejected and the published revision remains");
    CHECK(!mark_at("staged-revision", old_revision_start),
          "nested publish was rejected and staged edits remain private");

    amy_add_message("zQ8,3,4Z");
    uint32_t new_revision_start = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(8, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "the newly published target group starts");
    sequencer_midi_clock_tick();
    CHECK(mark_at("staged-revision", new_revision_start),
          "the rejected nested publish did not discard staged edits");
}

static void test_group_stop_control_is_a_supported_leaf(void) {
    printf("a group payload may stop an existing group execution\n");
    sequencer_reset();
    clear_group(7);
    clear_group(8);
    clear_marks();
    amy_add_message("H0,1,0,8zPmust-be-stoppedZ");
    amy_add_message("zQ8,3,4Z");
    amy_add_message("H0,4,0,7zQ8,0,0,0,55Z");
    amy_add_message("zQ7,3,4Z");

    uint32_t boundary = next_boundary(sequencer_ticks(), 4);
    CHECK(sequencer_group_control(8, SEQUENCE_CONTROL_START, 0, 4, 55, true),
          "the target execution is queued");
    CHECK(sequencer_group_control(7, SEQUENCE_CONTROL_START, 1, 4, 0, false),
          "the stopping group is queued on the same boundary");
    clock_to(boundary);
    CHECK(!mark_at("must-be-stopped", boundary),
          "the leaf stop takes effect before ordinary events on that tick");
}

static void test_invalid_edits_are_repairable(void) {
    printf("invalid definitions fail without losing staged edits\n");
    sequencer_reset();
    clear_group(5);
    CHECK(!sequencer_group_control(5, SEQUENCE_CONTROL_PUBLISH, 0, 0, 0, false),
          "zero-length publication is rejected");
    CHECK(!sequencer_group_add_wire(0, 1, 0, 5, NULL),
          "a NULL wire is rejected safely");
    CHECK(!sequencer_group_add_wire(0, 1, 0, 5, strdup("H0zPnestedZ")),
          "a second ticks command is rejected");

    CHECK(sequencer_group_add_wire(3, 2, 0, 5, strdup("zPbad-periodZ")),
          "an invalid-period edit can be staged");
    CHECK(!sequencer_group_control(5, SEQUENCE_CONTROL_PUBLISH, 4, 0, 0, false),
          "publication rejects tick >= period");
    CHECK(sequencer_group_add_wire(1, 2, 0, 5, strdup("zPrepairedZ")),
          "the invalid staged event can be replaced");
    CHECK(sequencer_group_control(5, SEQUENCE_CONTROL_PUBLISH, 4, 0, 0, false),
          "the repaired definition publishes");

    CHECK(sequencer_group_add_wire(4, 0, 1, 5, strdup("zPtoo-lateZ")),
          "an out-of-length event can be staged");
    CHECK(!sequencer_group_control(5, SEQUENCE_CONTROL_PUBLISH, 4, 0, 0, false),
          "publication rejects tick >= group length");
    CHECK(sequencer_group_add_wire(0, 0, 1, 5, strdup("")),
          "the invalid local tag can be cleared");
    CHECK(sequencer_group_control(5, SEQUENCE_CONTROL_PUBLISH, 4, 0, 0, false),
          "publication succeeds after clearing the invalid tag");

    CHECK(sequencer_group_control(5, SEQUENCE_CONTROL_PUBLISH, 4, 0, 0, false),
          "publishing without new edits clones the published definition");
    clear_marks();
    uint32_t cloned_start = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(5, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "the cloned definition can be started");
    sequencer_midi_clock_tick();
    CHECK(mark_at("repaired", cloned_start + 1),
          "the cloned definition retains its event wire");
    sequencer_reset();

    CHECK(!sequencer_group_control(5, 99, 0, 0, 0, false),
          "an unknown lifecycle action is rejected");
    CHECK(!sequencer_group_control(0, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "reserved group zero is rejected by group control");
    CHECK(!sequencer_group_control(9, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "a group beyond the configured range is rejected");
    clear_group(6);
    CHECK(!sequencer_group_control(6, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "start without a published definition is rejected");
    CHECK(!sequencer_group_control(6, SEQUENCE_CONTROL_GATE, 1, 0, 0, false),
          "gate with no active execution reports no affected execution");
}

static void test_clear_preserves_active_revision(void) {
    printf("clearing storage does not invalidate an active revision\n");
    sequencer_reset();
    clear_group(6);
    clear_marks();
    amy_add_message("H0,4,0,6zPactive-after-clearZ");
    amy_add_message("zQ6,3,4Z");
    uint32_t start = sequencer_ticks() + 1;
    CHECK(sequencer_group_control(6, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "the execution starts before storage is cleared");
    clear_group(6);
    sequencer_midi_clock_tick();
    CHECK(mark_at("active-after-clear", start),
          "an active execution retains its published revision");
    CHECK(!sequencer_group_control(6, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "clear prevents future starts until another publication");
}

static void test_resets_keep_definitions_only(void) {
    printf("sequencer and timebase resets stop executions but keep definitions\n");
    sequencer_reset();
    clear_group(8);
    clear_marks();
    amy_add_message("H0,4,0,8zPsurvivorZ");
    amy_add_message("zQ8,3,4Z");
    uint32_t first = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ8,1,0,4Z");
    clock_to(first);
    CHECK(mark_at("survivor", first), "definition runs before reset");

    clear_marks();
    sequencer_reset();
    clock_to(first + 4);
    CHECK(!marks_named("survivor"), "RESET_SEQUENCER stops active executions");
    uint32_t second = next_boundary(sequencer_ticks(), 4);
    amy_add_message("zQ8,1,1,4Z");
    clock_to(second);
    CHECK(mark_at("survivor", second), "definition survives RESET_SEQUENCER");

    clear_marks();
    amy_add_message("zQ8,1,0,0Z");
    sequencer_midi_clock_tick();
    amy_add_message("S4096Z");
    amy_execute_deltas();
    clear_marks();
    clock_to(sequencer_ticks() + 4);
    CHECK(!marks_named("survivor"),
          "the public RESET_SEQUENCER wire stops group executions");
    amy_add_message("zQ8,1,1,0Z");
    sequencer_midi_clock_tick();
    CHECK(marks_named("survivor") == 1,
          "the public RESET_SEQUENCER wire preserves definitions");

    clear_marks();
    amy_add_message("zQ8,1,0,0Z");
    clock_to(sequencer_ticks() + 2);
    sequencer_group_reset_timebase();
    clear_marks();
    uint32_t after_reset = sequencer_ticks() + 4;
    clock_to(after_reset);
    CHECK(!marks_named("survivor"), "RESET_TIMEBASE stops active executions");
    amy_add_message("zQ8,1,1,0Z");
    clock_to(sequencer_ticks() + 2);
    CHECK(marks_named("survivor") == 1, "definition survives RESET_TIMEBASE");
}

static void test_group_start_crosses_clock_rollover(void) {
    printf("group phase remains correct across the 32-bit tick rollover\n");
    sequencer_reset();
    clear_group(5);
    clear_marks();
    amy_add_message("H0,4,0,5zPwrap-zeroZ");
    amy_add_message("H1,0,1,5zPwrap-oneZ");
    amy_add_message("zQ5,3,4Z");

    amy_global.sequencer_tick_count = UINT32_MAX - 2;
    amy_add_message("zQ5,1,1,4Z");
    clock_to(1);
    CHECK(mark_at("wrap-zero", 0),
          "quantized local tick zero fired after rollover");
    CHECK(mark_at("wrap-one", 1),
          "local elapsed time advanced across rollover");
}

static void test_configured_bounds(void) {
    printf("configured group, local-tag and execution bounds are enforced\n");
    sequencer_reset();
    clear_group(8);
    char *valid = strdup("zPlastZ");
    char *bad_group = strdup("zPbad-groupZ");
    char *bad_tag = strdup("zPbad-tagZ");
    CHECK(sequencer_group_add_wire(0, 4, 7, 8, valid),
          "last configured group and local tag are valid");
    CHECK(!sequencer_group_add_wire(0, 4, 0, 9, bad_group),
          "first group past the configured range is rejected");
    CHECK(!sequencer_group_add_wire(0, 4, 8, 8, bad_tag),
          "first local tag past the configured range is rejected");
    CHECK(sequencer_group_control(8, SEQUENCE_CONTROL_PUBLISH, 4, 0, 0, false),
          "last group publishes");
    for (uint32_t i = 0; i < 8; ++i)
        CHECK(sequencer_group_control(8, SEQUENCE_CONTROL_START, 1, 64,
                                      i, true),
              "execution slot %" PRIu32 " is available", i);
    CHECK(!sequencer_group_control(8, SEQUENCE_CONTROL_START, 1, 64,
                                   8, true),
          "one execution beyond the configured pool is rejected");
    clear_marks();
    uint32_t start = next_boundary(sequencer_ticks(), 64);
    clock_to(start);
    CHECK(marks_named_at("last", start) == 8,
          "a rejected ninth start does not disturb the eight queued executions");
    clock_to(start + 4);
    CHECK(sequencer_group_control(8, SEQUENCE_CONTROL_START, 1, 0, 0, false),
          "completed one-shots return their execution slots to the pool");
    sequencer_reset();
}

static void test_disabled_configuration(void) {
    printf("zero capacities disable sequencer groups safely\n");
    const uint32_t capacities[][3] = {
        {0, 8, 8},
        {8, 0, 8},
        {8, 8, 0},
    };
    for (size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
        amy_config_t config = amy_default_config();
        config.features.startup_bleep = 0;
        config.audio = AMY_AUDIO_IS_NONE;
        config.max_sequence_groups = capacities[i][0];
        config.max_sequence_group_tags = capacities[i][1];
        config.max_sequence_group_executions = capacities[i][2];
        amy_start(config);
        CHECK(!sequencer_group_add_wire(0, 1, 0, 1, strdup("zPdisabledZ")),
              "group storage is disabled when capacity set %zu contains zero",
              i + 1);
        CHECK(!sequencer_group_control(1, SEQUENCE_CONTROL_START, 1, 0, 0, false),
              "group control is disabled when capacity set %zu contains zero",
              i + 1);
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
    config.max_sequence_groups = 8;
    config.max_sequence_group_tags = 8;
    config.max_sequence_group_executions = 8;
    amy_start(config);

    test_legacy_ticks_are_unchanged();
    test_legacy_c_event_wire_is_unchanged();
    test_group_local_tags_are_independent();
    test_one_n_and_infinite_repeats();
    test_atomic_revision_lifetime();
    test_root_launches_local_zero_on_same_tick();
    test_direct_start_begins_on_next_tick();
    test_tagged_start_replaces_at_activation();
    test_c_event_uses_fourth_ticks_field();
    test_quantized_gate_preserves_phase();
    test_quantized_stop_precedes_boundary_event();
    test_tagged_gate_and_stop_are_selective();
    test_tagged_control_does_not_select_untagged_execution();
    test_group_lifecycle_control_is_not_recursive();
    test_group_stop_control_is_a_supported_leaf();
    test_invalid_edits_are_repairable();
    test_clear_preserves_active_revision();
    test_resets_keep_definitions_only();
    test_group_start_crosses_clock_rollover();
    test_configured_bounds();

    amy_stop();
    test_disabled_configuration();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall sequencer group checks passed\n");
    return 0;
}
