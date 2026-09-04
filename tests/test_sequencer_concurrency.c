// Deterministic two-writer publication/retry regression test.

#include <pthread.h>
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

static pthread_mutex_t rendezvous_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rendezvous_changed = PTHREAD_COND_INITIALIZER;
static int writers_at_pin = 0;
static int release_writers = 0;
static int a_hits = 0;
static int b_hits = 0;

static void after_source_pin(void) {
    pthread_mutex_lock(&rendezvous_lock);
    writers_at_pin++;
    if (writers_at_pin == 2) {
        release_writers = 1;
        pthread_cond_broadcast(&rendezvous_changed);
    } else {
        while (!release_writers)
            pthread_cond_wait(&rendezvous_changed, &rendezvous_lock);
    }
    pthread_mutex_unlock(&rendezvous_lock);
}

typedef struct writer_args_t {
    uint32_t tick;
    const char *wire;
    uint8_t result;
} writer_args_t;

static void *append_event(void *opaque) {
    writer_args_t *args = (writer_args_t *)opaque;
    args->result = sequencer_sequence_add_wire(
        1, args->tick, 0, strdup(args->wire));
    return NULL;
}

static void mark_hook(const char *code) {
    if (!strcmp(code, "writer-a")) a_hits++;
    if (!strcmp(code, "writer-b")) b_hits++;
}

static void clock_to(uint32_t target) {
    while (!AMY_TIME_GEQ(sequencer_ticks(), target)) sequencer_midi_clock_tick();
}

static void test_losing_writer_retries_cumulatively(void) {
    printf("two writers publishing from one generation both survive\n");
    sequencer_reset();
    CHECK(sequencer_sequence_add_wire(1, 0, 0, strdup("zPbaseZ")),
          "base definition exists");
    CHECK(sequencer_sequence_add_wire(1, 6, 0, strdup("zPtailZ")),
          "base definition has a finite tail");
    CHECK(sequencer_sequence_control(1, SEQUENCE_CONTROL_START, 0, 0),
          "an execution pins the shared source generation");

    writer_args_t a = {2, "zPwriter-aZ", 0};
    writer_args_t b = {4, "zPwriter-bZ", 0};
    pthread_t a_thread;
    pthread_t b_thread;
    sequencer_test_set_after_pin_hook(after_source_pin);
    CHECK(pthread_create(&a_thread, NULL, append_event, &a) == 0,
          "writer A starts");
    CHECK(pthread_create(&b_thread, NULL, append_event, &b) == 0,
          "writer B starts");
    pthread_join(a_thread, NULL);
    pthread_join(b_thread, NULL);
    sequencer_test_set_after_pin_hook(NULL);
    CHECK(a.result && b.result, "both competing edits report success");

    a_hits = 0;
    b_hits = 0;
    CHECK(sequencer_sequence_control(1, SEQUENCE_CONTROL_START, 0, 0),
          "the cumulatively published generation starts");
    uint32_t start = sequencer_ticks() + 1;
    clock_to(start + 6);
    CHECK(a_hits == 1 && b_hits == 1,
          "the losing compare/retry path loses and duplicates no event");
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
    amy_start(config);

    test_losing_writer_retries_cumulatively();

    amy_stop();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall concurrent sequence publication checks passed\n");
    return 0;
}
