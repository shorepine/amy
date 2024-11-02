// sequencer.c
// handle timer-like events for music
// runs in a thread / task

#include "sequencer.h"

// Things that AMY can change
float sequencer_bpm = 108; // verified optimal BPM 
uint8_t sequencer_ppq = 48;

mp_obj_t sequencer_callbacks[SEQUENCER_SLOTS];
uint8_t sequencer_dividers[SEQUENCER_SLOTS];

mp_obj_t defer_callbacks[DEFER_SLOTS];
mp_obj_t defer_args[DEFER_SLOTS];
uint32_t defer_sysclock[DEFER_SLOTS];

uint8_t sequencer_running = 1;

// Our internal accounting
uint32_t sequencer_tick_count = 0;
uint64_t next_amy_tick_us = 0;
uint32_t us_per_tick = 0;

// a sequencer entry
struct sequence_entry {
    char * message; // alloced at creation
    uint16_t tick_divider; // AMY_UNSET if not set 
    uint32_t tick; // AMY_UNSET if not set
    uint32_t tag; 
    struct sequence_entry * next; // the next event
};

#ifdef ESP_PLATFORM
#include "esp_timer.h"
esp_timer_handle_t periodic_timer;
#endif

void sequencer_recompute() {
    us_per_tick = (uint32_t) (1000000.0 / ((sequencer_bpm/60.0) * (float)sequencer_ppq));    
    next_amy_tick_us = amy_sysclock()*1000 + us_per_tick;
}

void sequencer_start() {
    sequencer_recompute();
    sequencer_running = 1;
    #ifdef ESP_PLATFORM
    // Restart the timer
    run_sequencer();
    #endif
}


void sequencer_stop() {
    sequencer_running = 0;
    #ifdef ESP_PLATFORM
    // Kill the timer
    ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
    ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
    #endif
}


void sequencer_init() {
    sequencer_start();
}

static void sequencer_check_and_fill() {
    // The while is in case the timer fires later than a tick; (on esp this would be due to SPI or wifi ops), we can still use our latency to keep up
    while(amy_sysclock()  >= (next_amy_tick_us/1000)) {
        sequencer_tick_count++;
    
        // a tick has happened
        // search through the sequence
        // this is a linked list so 
        
        /*        
        for(uint32_t i=0;i<amy_sequence_length;i++) {
            if(amy_sequence[i].tick == sequencer_tick_count || sequencer_tick_count % amy_sequence[i].tick_divider == 0) {
                amy_play_message(amy_sequence.message);
                if(AMY_IS_SET(amy_sequence[i].tick)) {

                }
            }
        }
        */
        
        // if the client of AMY has set a callback (like tulip will), call it
        if(amy_sequencer_tick_callback != NULL) {
            amy_sequencer_tick_callback(sequencer_tick_count);
        }

        // now check for things happening at this tick
        //
        //
        //


        next_amy_tick_us = next_amy_tick_us + us_per_tick;
    }
}


// On posix/pthreads platforms, timer is called from a thread
#ifndef ESP_PLATFORM
void * run_sequencer(void *vargs) {
    // Loop forever, checking for time and sleeping
    while(1) {
        if(sequencer_running) sequencer_check_and_fill();            
        // 500000nS = 500uS = 0.5mS
        nanosleep((const struct timespec[]){{0, 500000L}}, NULL);
    }
}

// ESP: do it with hardware timer
#else
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

