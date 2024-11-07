#include "sequencer.h"

typedef struct  {
    struct event e; // parsed event -- we clear out sequence and time here obv
    uint32_t tag;
    uint32_t tick; // 0 means not used 
    uint32_t length; // 0 means not used 
} sequence_entry;

// linked list of sequence_entries
typedef struct sequence_entry_ll_t{
    sequence_entry *sequence;
    struct sequence_entry_ll_t *next;
} sequence_entry_ll_t;

sequence_entry_ll_t * sequence_entry_ll_start;

// Optional sequencer hook that's called every tick
void (*amy_external_sequencer_hook)(uint32_t) = NULL;

// Our internal accounting
uint32_t sequencer_tick_count = 0;
uint64_t next_amy_tick_us = 0;
uint32_t us_per_tick = 0;

#ifdef ESP_PLATFORM
#include "esp_timer.h"
esp_timer_handle_t periodic_timer;
#else
#include <pthread.h>
#endif

void sequencer_reset() {
    // Remove all events
    sequence_entry_ll_t **entry_ll_ptr = &sequence_entry_ll_start; // Start pointing to the root node.
    while ((*entry_ll_ptr) != NULL) {
        sequence_entry_ll_t *doomed = *entry_ll_ptr;
        *entry_ll_ptr = doomed->next; // close up list.
        free(doomed->sequence);
        free(doomed);
    }
    sequence_entry_ll_start = NULL;
}

void sequencer_recompute() {
    us_per_tick = (uint32_t) (1000000.0 / ((amy_global.tempo/60.0) * (float)AMY_SEQUENCER_PPQ));    
    next_amy_tick_us = amy_sysclock()*1000 + us_per_tick;
}

uint8_t sequencer_add_event(struct event e, uint32_t tick, uint32_t length, uint32_t tag) {
    // add this event to the list of sequencer events in the LL
    // if the tag already exists - if there's tick/length, overwrite, if there's no tick / length, we should remove the entry
    sequence_entry_ll_t **entry_ll_ptr = &sequence_entry_ll_start; // Start pointing to the root node.
    while ((*entry_ll_ptr) != NULL) {
        if ((*entry_ll_ptr)->sequence->tag == tag) {
            if (length == 0 && tick == 0) { // delete
                sequence_entry_ll_t *doomed = *entry_ll_ptr;
                *entry_ll_ptr = doomed->next; // close up list.
                free(doomed->sequence);
                free(doomed);
                return 0;
            } else { // replace
                (*entry_ll_ptr)->sequence->length = length;
                (*entry_ll_ptr)->sequence->tick = tick;
                (*entry_ll_ptr)->sequence->e = e;
                return 0;
            }
        }
        entry_ll_ptr = &((*entry_ll_ptr)->next); // Update to point to the next field in the preceding list node.
    }

    // If we got here, we didn't find the tag in the list, so add it at the end.
    if(tick == 0 && length == 0) return 0; // Ignore non-schedulable event.
    if(tick != 0 && length == 0 && tick <= sequencer_tick_count) return 0; // don't schedule things in the past.
    (*entry_ll_ptr) = malloc(sizeof(sequence_entry_ll_t));
    (*entry_ll_ptr)->sequence = malloc(sizeof(sequence_entry));
    (*entry_ll_ptr)->sequence->e = e;
    (*entry_ll_ptr)->sequence->tick = tick;
    (*entry_ll_ptr)->sequence->length = length;
    (*entry_ll_ptr)->sequence->tag = tag;
    (*entry_ll_ptr)->next = NULL;
    return 1;
}


static void sequencer_check_and_fill() {
    // The while is in case the timer fires later than a tick; (on esp this would be due to SPI or wifi ops)
    while(amy_sysclock()  >= (next_amy_tick_us/1000)) {
        sequencer_tick_count++;
        // Scan through LL looking for matches
        sequence_entry_ll_t **entry_ll_ptr = &sequence_entry_ll_start; // Start pointing to the root node.
        while ((*entry_ll_ptr) != NULL) {
            uint8_t deleted = 0;
            uint8_t hit = 0;
            uint8_t delete = 0;
            if((*entry_ll_ptr)->sequence->length != 0) { // length set 
                uint32_t offset = sequencer_tick_count % (*entry_ll_ptr)->sequence->length;
                if(offset == (*entry_ll_ptr)->sequence->tick) hit = 1;
            } else {
                // Test for absolute tick (no length set)
                if ((*entry_ll_ptr)->sequence->tick == sequencer_tick_count) { hit = 1; delete = 1; }
            }
            if(hit) {
                struct event to_add = (*entry_ll_ptr)->sequence->e;
                to_add.status = EVENT_SCHEDULED;
                to_add.time = 0; // play now
                amy_add_event(to_add);

                // Delete absolute tick addressed sequence entry if sent
                if(delete) {
                    sequence_entry_ll_t *doomed = *entry_ll_ptr;
                    *entry_ll_ptr = doomed->next; // close up list.
                    free(doomed->sequence);
                    free(doomed);
                    deleted = 1;
                }
            }
            if(!deleted) {
                entry_ll_ptr = &((*entry_ll_ptr)->next); // Update to point to the next field in the preceding list node.
            }
        }

        if(amy_external_sequencer_hook!=NULL) {
            amy_external_sequencer_hook(sequencer_tick_count);
        }
        next_amy_tick_us = next_amy_tick_us + us_per_tick;
    }
}

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
    // 500uS = 0.5mS
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 500));
}
#elif defined _POSIX_THREADS
void * run_sequencer(void *vargs) {
    // Loop forever, checking for time and sleeping
    while(1) {
        sequencer_check_and_fill();            
        // 500000nS = 500uS = 0.5mS
        nanosleep((const struct timespec[]){{0, 500000L}}, NULL);
    }
}
#endif


void sequencer_init() {
    sequence_entry_ll_start = NULL;
    sequencer_recompute();        

    #ifdef ESP_PLATFORM
    // This kicks off a timer 
    run_sequencer();
    #elif defined _POSIX_THREADS
    // This kicks off a thread
    pthread_t sequencer_thread_id;
    pthread_create(&sequencer_thread_id, NULL, run_sequencer, NULL);
    #else
    fprintf(stderr, "No sequencer support for this chip / platform\n");
    #endif

}

