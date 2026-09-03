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
    test_one_n_and_infinite_repeats();
    test_atomic_revision_lifetime();
    test_root_launches_local_zero_on_same_tick();

    amy_stop();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall sequencer group checks passed\n");
    return 0;
}
