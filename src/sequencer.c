#include "sequencer.h"

typedef struct  {
    struct event e; // parsed event -- we clear out sequence and time here obv
    uint32_t tag;
    uint32_t tick; // 0 means not used 
    uint16_t divider; // 0 means not used 
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

void sequencer_recompute() {
    us_per_tick = (uint32_t) (1000000.0 / ((amy_global.tempo/60.0) * (float)AMY_SEQUENCER_PPQ));    
    next_amy_tick_us = amy_sysclock()*1000 + us_per_tick;
}

extern int parse_list_uint32_t(char *message, uint32_t *vals, int max_num_vals, uint32_t skipped_val);

// Parses the tick, divider and tag out of a sequence message
void parse_tick_and_tag(char * message, uint32_t *tick, uint16_t *divider, uint32_t *tag) {
    uint32_t vals[3];
    parse_list_uint32_t(message, vals, 3, 0);
    *tick = (uint32_t)vals[0]; *divider = (uint16_t)vals[1]; *tag = (uint32_t)vals[2];
}

uint8_t sequencer_add_event(struct event e, uint32_t tick, uint16_t divider, uint32_t tag) {
    // add this event to the list of sequencer events in the LL
    // if the tag already exists - if there's tick/divider, overwrite, if there's no tick / divider, we should remove the entry
    if(divider != 0 && tick != 0) { divider = 0; } // if tick is set ignore divider 

    sequence_entry_ll_t *entry_ll = sequence_entry_ll_start;
    sequence_entry_ll_t *prev_entry_ll = NULL;
    uint8_t found = 0;
    while(entry_ll != NULL) {
        if(entry_ll->sequence->tag == tag) {
            found = 1;
            if(divider == 0 && tick == 0) { // delete 
               if(prev_entry_ll == NULL) { // start
                    sequence_entry_ll_start = entry_ll->next;
                } else {
                    prev_entry_ll->next = entry_ll->next;
                }
                free(entry_ll->sequence);
                free(entry_ll);
                return 0;
            } else { //replace 
                entry_ll->sequence->divider = divider;
                entry_ll->sequence->tick = tick;
                entry_ll->sequence->e = e;
                return 0;
            }
        }
        prev_entry_ll = entry_ll;
        entry_ll = entry_ll->next;
    }
    if(!found) {
        if(tick != 0 || divider != 0) {
            if(tick != 0 && tick <= sequencer_tick_count) return 0; // don't schedule things in the past.
            sequence_entry_ll_t *new_entry_ll = malloc(sizeof(sequence_entry_ll_t));
            new_entry_ll->sequence = malloc(sizeof(sequence_entry));
            new_entry_ll->sequence->e = e;
            new_entry_ll->sequence->tick = tick;
            new_entry_ll->sequence->divider = divider;
            new_entry_ll->sequence->tag = tag;
            new_entry_ll->next = NULL;

            if(prev_entry_ll == NULL) { // start 
                sequence_entry_ll_start = new_entry_ll;
            } else {
                prev_entry_ll->next = new_entry_ll;
            }
            return 1;
        }
    }
    return 0;
}


static void sequencer_check_and_fill() {
    // The while is in case the timer fires later than a tick; (on esp this would be due to SPI or wifi ops)
    while(amy_sysclock()  >= (next_amy_tick_us/1000)) {
        sequencer_tick_count++;
        // Scan through LL looking for matches
        sequence_entry_ll_t *entry_ll = sequence_entry_ll_start;
        sequence_entry_ll_t *prev_entry_ll = NULL;
        while(entry_ll != NULL) {
            uint8_t already_deleted = 0;
            if(entry_ll->sequence->tick == sequencer_tick_count || (entry_ll->sequence->divider > 0 && (sequencer_tick_count % entry_ll->sequence->divider == 0))) {
                // hit
                struct event to_add = entry_ll->sequence->e;
                to_add.status = EVENT_SCHEDULED;
                to_add.time = 0; // play now
                amy_add_event(to_add);

                // Delete tick addressed sequence entry if sent
                if(entry_ll->sequence->tick > 0) {
                    if(prev_entry_ll == NULL) { // start
                        sequence_entry_ll_start = entry_ll->next;
                    } else {
                        prev_entry_ll->next = entry_ll->next;
                    }
                    free(entry_ll->sequence);
                    free(entry_ll);

                    if(prev_entry_ll != NULL) {
                        entry_ll = prev_entry_ll->next;
                    } else {
                        entry_ll = NULL;
                    }
                    already_deleted = 1;
                }
            }
            if(!already_deleted) {
                prev_entry_ll = entry_ll;
                entry_ll = entry_ll->next;
            }
        }

        if(amy_external_sequencer_hook!=NULL) {
            amy_external_sequencer_hook(sequencer_tick_count);
        }
        next_amy_tick_us = next_amy_tick_us + us_per_tick;
    }
}

// posix: called from a thread
#if defined _POSIX_THREADS
void * run_sequencer(void *vargs) {
    // Loop forever, checking for time and sleeping
    while(1) {
        sequencer_check_and_fill();            
        // 500000nS = 500uS = 0.5mS
        nanosleep((const struct timespec[]){{0, 500000L}}, NULL);
    }
}

// ESP: do it with hardware timer
#elif defined ESP_PLATFORM
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

