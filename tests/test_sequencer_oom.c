// Allocation-failure regression tests for immutable sequence publication.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amy.h"
#include "sequencer.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

static int base_hits = 0;
static int unexpected_hits = 0;

static void mark_hook(const char *code) {
    if (!strcmp(code, "base-head") || !strcmp(code, "base-tail"))
        base_hits++;
    if (!strcmp(code, "must-not-publish")) unexpected_hits++;
}

static void clock_to(uint32_t target) {
    while (!AMY_TIME_GEQ(sequencer_ticks(), target)) sequencer_midi_clock_tick();
}

static void define_base(void) {
    CHECK(sequencer_sequence_add_wire(1, 0, 0, strdup("zPbase-headZ")),
          "base head is defined");
    CHECK(sequencer_sequence_add_wire(1, 4, 0, strdup("zPbase-tailZ")),
          "base tail is defined");
}

static void test_initialization_allocation_failures(amy_config_t config) {
    printf("partial sequence-pool initialization fails closed\n");
    for (int32_t fail_after = 0; fail_after < 2; ++fail_after) {
        sequencer_test_fail_allocation_after(fail_after);
        amy_start(config);
        sequencer_test_fail_allocation_after(-1);
        CHECK(!sequencer_sequence_add_wire(
                  1, 0, 0, strdup("zPmust-not-publishZ")),
              "pool allocation failure %" PRIi32 " disables definitions",
              fail_after);
        CHECK(!sequencer_sequence_control(
                  1, SEQUENCE_CONTROL_START, 0, 0),
              "pool allocation failure %" PRIi32 " disables executions",
              fail_after);
        amy_stop();
    }
}

static void test_new_definition_allocation_failures(void) {
    printf("new-definition allocation failure leaves an empty tag\n");
    for (int32_t fail_after = 0; fail_after < 2; ++fail_after) {
        sequencer_reset();
        sequencer_test_fail_allocation_after(fail_after);
        CHECK(!sequencer_sequence_add_wire(
                  1, 0, 0, strdup("zPmust-not-publishZ")),
              "definition allocation failure %" PRIi32 " rejects the append",
              fail_after);
        sequencer_test_fail_allocation_after(-1);
        CHECK(!sequencer_sequence_control(
                  1, SEQUENCE_CONTROL_START, 0, 0),
              "definition allocation failure %" PRIi32
              " publishes no empty candidate", fail_after);
    }
}

static void test_clone_allocation_failures_preserve_source(void) {
    printf("every clone allocation failure preserves the published definition\n");
    // Clone allocation order: definition, event array, then two wire strings.
    for (int32_t fail_after = 0; fail_after < 4; ++fail_after) {
        sequencer_reset();
        define_base();
        CHECK(sequencer_sequence_control(1, SEQUENCE_CONTROL_START, 0, 0),
              "source execution pins the definition (failure %" PRIi32 ")",
              fail_after);

        char *incoming = strdup("zPmust-not-publishZ");
        sequencer_test_fail_allocation_after(fail_after);
        uint8_t appended = sequencer_sequence_add_wire(1, 2, 0, incoming);
        sequencer_test_fail_allocation_after(-1);
        CHECK(!appended, "allocation failure %" PRIi32 " rejects the edit",
              fail_after);

        base_hits = 0;
        unexpected_hits = 0;
        CHECK(sequencer_sequence_control(1, SEQUENCE_CONTROL_START, 0, 0),
              "old definition remains startable");
        uint32_t start = sequencer_ticks() + 1;
        clock_to(start + 4);
        CHECK(base_hits >= 2 && unexpected_hits == 0,
              "failure %" PRIi32 " publishes neither a partial nor corrupt edit",
              fail_after);
    }
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.audio = AMY_AUDIO_IS_NONE;
    config.amy_external_exec_hook = mark_hook;
    config.max_sequencer_tags = 4;
    config.max_sequence_events = 8;
    config.max_sequence_executions = 8;
    test_initialization_allocation_failures(config);
    amy_start(config);

    test_new_definition_allocation_failures();
    test_clone_allocation_failures_preserve_source();

    amy_stop();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall sequence allocation-failure checks passed\n");
    return 0;
}
