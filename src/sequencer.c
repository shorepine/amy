#include "sequencer.h"
#include "amy.h"



// Optional sequencer hook that's called every tick
extern void (*amy_external_sequencer_hook)(uint32_t);

#ifdef ESP_PLATFORM
#include "esp_timer.h"
esp_timer_handle_t periodic_timer;
#else
#include <pthread.h>
#endif


#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

uint32_t sequencer_ticks() { return amy_global.sequencer_tick_count; }

typedef struct sequence_info_t {
    struct delta *deltas;
    //uint32_t tag;  // tag is implicit, it's its index in the table
    uint32_t tick; // 0 means not used
    uint32_t period; // 0 means not used
} sequence_info_t;

struct sequence_info_t *sequences = NULL;  // An array indexed by tag.
int32_t max_sequences = 0;
int32_t highest_tag = -1;

void sequencer_init(int max_sequencer_tags) {
    max_sequences = max_sequencer_tags;
    sequences = (struct sequence_info_t *)malloc_caps(max_sequences * sizeof(struct sequence_info_t),
                                                      amy_global.config.ram_caps_synth);
    for (int32_t i = 0; i < max_sequences; ++i) {
        sequences[i].deltas = NULL;
        sequences[i].tick = 0;
        sequences[i].period = 0;
    }
}

void sequencer_free() {
    if (sequences != NULL) free(sequences);
    max_sequences = 0;
}

void sequencer_reset() {
    // Remove all events
    for (int32_t i = 0; i < max_sequences; ++i) {
        if (sequences[i].deltas) {
            delta_release_list(sequences[i].deltas);
            sequences[i].deltas = NULL;
            sequences[i].tick = 0;
            sequences[i].period = 0;
        }
    }
    highest_tag = -1;
}

void sequencer_debug() {
    fprintf(stderr, "sequencer: max_sequences %" PRIi32" highest_tag %" PRIi32 "\n", max_sequences, highest_tag);
    for (int32_t tag = 0; tag < max_sequences; ++tag) {
        if (sequences[tag].deltas) {
            fprintf(stderr, "sequence tag %" PRIu32" tick %" PRIu32 " period %"PRIu32 " num_deltas %"PRIu32 "\n",
                    tag, sequences[tag].tick, sequences[tag].period, delta_list_len(sequences[tag].deltas));
        }
    }
}

void sequencer_recompute() {
    amy_global.us_per_tick = (uint32_t) (1000000.0 / ((amy_global.tempo/60.0) * (float)AMY_SEQUENCER_PPQ));    
    amy_global.next_amy_tick_us = (((uint64_t)amy_sysclock()) * 1000L) + (uint64_t)amy_global.us_per_tick;
}

uint8_t sequencer_add_event(amy_event *e) {
    // add this event to the list of sequencer events in the LL.
    // e->sequence is set up.
    // if the tag already exists - if there's tick/period, overwrite, if there's no tick / period, we should remove the entry
    //fprintf(stderr, "sequencer_add_event: e->instrument %d e->note %.0f e->vel %.2f tick %d period %d tag %d\n", e->instrument, e->midi_note, e->velocity, e->sequence[SEQUENCE_TICK], e->sequence[SEQUENCE_PERIOD], e->sequence[SEQUENCE_TAG]);
    int32_t tag = e->sequence[SEQUENCE_TAG];
    if (tag > max_sequences) {
        fprintf(stderr, "sequencer tag %" PRIi32" (with tick %" PRIu32", period %" PRIu32") is greater than or eq max_sequences %" PRIi32"\n",
                tag, e->sequence[SEQUENCE_TICK], e->sequence[SEQUENCE_PERIOD], max_sequences);
        // ignore
        return 0;
    }
    // Release any existing deltas for this tag, even if we're just going to rewrite them.
    delta_release_list(sequences[tag].deltas);
    sequences[tag].deltas = NULL;

    if(e->sequence[SEQUENCE_TICK] == 0 && e->sequence[SEQUENCE_PERIOD] == 0) return 0; // Ignore non-schedulable event.
    if(e->sequence[SEQUENCE_TICK] != 0 && e->sequence[SEQUENCE_PERIOD] == 0 && e->sequence[SEQUENCE_TICK] <= amy_global.sequencer_tick_count) return 0; // don't schedule things in the past.

    // Save the tick & period.
    sequences[tag].tick = e->sequence[SEQUENCE_TICK];
    sequences[tag].period = e->sequence[SEQUENCE_PERIOD];
    // Copy all the deltas for this event to the sequences entry.
    amy_event_to_deltas_queue(e, 0, &sequences[tag].deltas);

    if (tag > highest_tag) highest_tag = tag;  // To limit scanning through tags.

    return 1;
}


void sequencer_check_and_fill() {
    // The while is in case the timer fires later than a tick; (on esp this would be due to SPI or wifi ops)
    while(amy_sysclock()  >= (uint32_t)(amy_global.next_amy_tick_us / 1000L)) {
        amy_global.sequencer_tick_count++;
        // Scan through LL looking for matches
        for (int32_t tag = 0; tag <= highest_tag; ++tag) {
            if (sequences[tag].deltas != NULL) {
                bool hit = false;
                bool delete = false;
                if(sequences[tag].period != 0) { // period set
                    uint32_t offset = amy_global.sequencer_tick_count % sequences[tag].period;
                    if (offset == sequences[tag].tick) hit = true;
                } else {
                    // Test for absolute tick (no period set)
                    if (sequences[tag].tick == amy_global.sequencer_tick_count) { hit = true; delete = true; }
                }
                if(hit) {
                    struct delta *d = sequences[tag].deltas;
                    while(d) {
                        // assume the d->time is 0 and that's good.
                        add_delta_to_queue(d, &amy_global.delta_queue);
                        d = d->next;
                    }
                    // Delete absolute tick addressed sequence entry if sent
                    if(delete) {
                        delta_release_list(sequences[tag].deltas);
                        sequences[tag].deltas = NULL;
                    }
                }
            }
        }
        // call the right hook:
#ifdef __EMSCRIPTEN__
        EM_ASM({
            if(typeof amy_sequencer_js_hook === 'function') {
                amy_sequencer_js_hook($0);
            }
        }, amy_global.sequencer_tick_count);
#endif
        if(amy_external_sequencer_hook!=NULL) {
            amy_external_sequencer_hook(amy_global.sequencer_tick_count);
        }
        amy_global.next_amy_tick_us = amy_global.next_amy_tick_us + (uint64_t)amy_global.us_per_tick;
    }
}

///// Sequencers per platform

#ifdef ESP_PLATFORM
// ESP: do it with hardware timer
static void sequencer_timer_callback(void* arg) {
    sequencer_check_and_fill();
}

void run_sequencer() {
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &sequencer_timer_callback,
            //.dispatch_method = ESP_TIMER_ISR,
            .name = "sequencer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    // 500us = 0.5ms
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 500));
}

#elif defined(PICO_RP2350) || defined(PICO_RP2040)
// pico: do it with a hardware timer

#include "pico/time.h"
repeating_timer_t pico_sequencer_timer;

static bool sequencer_timer_callback(repeating_timer_t *rt) {
    sequencer_check_and_fill();
    return true;
}

void run_sequencer() {
    add_repeating_timer_us(-500, sequencer_timer_callback, NULL, &pico_sequencer_timer);
}

#elif defined _POSIX_THREADS
// posix: threads
void * sequencer_thread(void *vargs) {
    // Loop forever, checking for time and sleeping
    while(1) {
        sequencer_check_and_fill();            
        // 500000ns = 500us = 0.5ms
        nanosleep((const struct timespec[]){{0, 500000L}}, NULL);
    }
}
void run_sequencer() {
    pthread_t sequencer_thread_id;
    pthread_create(&sequencer_thread_id, NULL, sequencer_thread, NULL);
}

#elif defined AMY_DAISY
void run_sequencer() {
    // Set up in DaisyMain.cc
}

#else

void run_sequencer() {
    fprintf(stderr, "No sequencer support for this chip / platform\n");
}

#endif

void sequencer_start() {
    sequencer_recompute();
    run_sequencer();
}
