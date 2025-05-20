#include "sequencer.h"
#include "amy.h"



// Optional sequencer hook that's called every tick
void (*amy_external_sequencer_hook)(uint32_t) = NULL;

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

void sequencer_reset() {
    // Remove all events
    sequence_entry_ll_t **entry_ll_ptr = &amy_global.sequence_entry_ll_start; // Start pointing to the root node.
    while ((*entry_ll_ptr) != NULL) {
        sequence_entry_ll_t *doomed = *entry_ll_ptr;
        *entry_ll_ptr = doomed->next; // close up list.
        free(doomed);
    }
    amy_global.sequence_entry_ll_start = NULL;
}

void sequencer_recompute() {
    amy_global.us_per_tick = (uint32_t) (1000000.0 / ((amy_global.tempo/60.0) * (float)AMY_SEQUENCER_PPQ));    
    amy_global.next_amy_tick_us = (((uint64_t)amy_sysclock()) * 1000L) + (uint64_t)amy_global.us_per_tick;
}

void add_delta_to_sequencer(struct delta *d, void*user_data) {
    sequence_callback_info_t * cbinfo = (sequence_callback_info_t *)user_data;
    sequence_entry_ll_t ***entry_ll_ptr = &(cbinfo->pointer); 
    (**entry_ll_ptr) = malloc(sizeof(sequence_entry_ll_t));
    //fprintf(stderr, "ll %p adding tag %d period %d tick %d data %d param %d osc %d\n",
    //    (**entry_ll_ptr), cbinfo->tag, cbinfo->period, cbinfo->tick, d->data, d->param, d->osc);
    (**entry_ll_ptr)->tick = cbinfo->tick;
    (**entry_ll_ptr)->period = cbinfo->period;
    (**entry_ll_ptr)->tag = cbinfo->tag;
    (**entry_ll_ptr)->d.time = 0;
    (**entry_ll_ptr)->d.data = d->data;
    (**entry_ll_ptr)->d.param = d->param;
    (**entry_ll_ptr)->d.osc = d->osc;
    (**entry_ll_ptr)->next = NULL;
    (*entry_ll_ptr) = &((**entry_ll_ptr)->next);
}

uint8_t sequencer_add_event(struct event *e) {
    // add this event to the list of sequencer events in the LL
    // if the tag already exists - if there's tick/period, overwrite, if there's no tick / period, we should remove the entry
    //fprintf(stderr, "sequencer_add_event: e->instrument %d e->note %.0f e->vel %.2f tick %d period %d tag %d\n", e->instrument, e->midi_note, e->velocity, tick, period, tag);
    sequence_entry_ll_t **entry_ll_ptr = &amy_global.sequence_entry_ll_start; // Start pointing to the root node.
    while ((*entry_ll_ptr) != NULL) {
        // Do this first, then add the new one / replacement one at the end. Not a big deal 
        // This can delete all the deltas from a source event if it matches tag
        if ((*entry_ll_ptr)->tag == e->sequence[SEQUENCE_TAG]) {
            //fprintf(stderr, "ll %p found tag %d, deleting\n", (*entry_ll_ptr), tag);
            sequence_entry_ll_t *doomed = *entry_ll_ptr;
            *entry_ll_ptr = doomed->next; // close up list.
            free(doomed);
        } else {
            entry_ll_ptr = &((*entry_ll_ptr)->next); // Update to point to the next field in the preceding list node.
        }
    }

    if(e->sequence[SEQUENCE_TICK] == 0 && e->sequence[SEQUENCE_PERIOD] == 0) return 0; // Ignore non-schedulable event.
    if(e->sequence[SEQUENCE_TICK] != 0 && e->sequence[SEQUENCE_PERIOD] == 0 && e->sequence[SEQUENCE_TICK] <= amy_global.sequencer_tick_count) return 0; // don't schedule things in the past.

    // Get all the deltas for this event
    // For each delta, add a new entry at the end
    sequence_callback_info_t cbinfo;
    cbinfo.tag = e->sequence[SEQUENCE_TAG];
    cbinfo.tick = e->sequence[SEQUENCE_TICK];
    cbinfo.period = e->sequence[SEQUENCE_PERIOD];
    cbinfo.pointer = entry_ll_ptr;
    amy_parse_event_to_deltas(e, 0, add_delta_to_sequencer, (void*)&cbinfo);
    return 1;
}


void sequencer_check_and_fill() {
    // The while is in case the timer fires later than a tick; (on esp this would be due to SPI or wifi ops)
    while(amy_sysclock()  >= (uint32_t)(amy_global.next_amy_tick_us / 1000L)) {
        amy_global.sequencer_tick_count++;
        // Scan through LL looking for matches
        sequence_entry_ll_t **entry_ll_ptr = &amy_global.sequence_entry_ll_start; // Start pointing to the root node.
        while ((*entry_ll_ptr) != NULL) {
            uint8_t deleted = 0;
            uint8_t hit = 0;
            uint8_t delete = 0;
            if((*entry_ll_ptr)->period != 0) { // period set 
                uint32_t offset = amy_global.sequencer_tick_count % (*entry_ll_ptr)->period;
                if(offset == (*entry_ll_ptr)->tick) hit = 1;
            } else {
                // Test for absolute tick (no period set)
                if ((*entry_ll_ptr)->tick == amy_global.sequencer_tick_count) { hit = 1; delete = 1; }
            }
            if(hit) {
                add_delta_to_queue(&(*entry_ll_ptr)->d, NULL);
                // Delete absolute tick addressed sequence entry if sent
                if(delete) {
                    sequence_entry_ll_t *doomed = *entry_ll_ptr;
                    *entry_ll_ptr = doomed->next; // close up list.
                    free(doomed);
                    deleted = 1;
                }
            }
            if(!deleted) {
                entry_ll_ptr = &((*entry_ll_ptr)->next); // Update to point to the next field in the preceding list node.
            }
        }
        // call the right hook:
#ifdef __EMSCRIPTEN__
        EM_ASM({
            amy_sequencer_js_hook($0);
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

#elif (defined PICO_RP2350) || (defined PICO_RP2040)
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

#else

void run_sequencer() {
    fprintf(stderr, "No sequencer support for this chip / platform\n");
}

#endif

void sequencer_init() {
    amy_global.sequence_entry_ll_start = NULL;
    sequencer_recompute();
    run_sequencer();
}

