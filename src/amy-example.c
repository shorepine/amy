// amy-example.c
// a simple C example that plays audio using AMY out your speaker 

#include "amy.h"
#include <pthread.h>
#include <unistd.h>

// AMY synth states
extern struct state global;
extern uint32_t event_counter;
extern uint32_t message_counter;
extern int16_t channel;
extern int16_t device_id;


int main(int argc, char ** argv) {
    amy_start();

    int opt;
    while((opt = getopt(argc, argv, ":d:c:lh")) != -1) 
    { 
        switch(opt) 
        { 
            case 'd': 
                device_id = atoi(optarg);
                break;
            case 'c': 
                channel = atoi(optarg);
                break; 
            case 'l':
                amy_print_devices();
                return 0;
                break;
            case 'h':
                printf("usage: amy-example\n");
                printf("\t[-d sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-c sound channel, default -1 for all channels on device]\n");
                printf("\t[-l list all sound devices and exit]\n");
                printf("\t[-h show this help and exit]\n");
                return 0;
                break;
            case ':': 
                printf("option needs a value\n"); 
                break; 
            case '?': 
                printf("unknown option: %c\n", optopt);
                break; 
        } 
    }
    amy_live_start();
    amy_reset_oscs();
    
    // Play a few notes in FM
    struct event e = amy_default_event();
    int64_t start = amy_sysclock();
    e.time = start;
    e.velocity = 1;
    e.wave = ALGO;
    e.patch = 15;
    e.midi_note = 60;
    amy_add_event(e);

    e.time = start + 500;
    e.osc += 9; // remember that an FM patch takes up 9 oscillators
    e.midi_note = 64;
    amy_add_event(e);
    
    e.time = start + 1000;
    e.osc += 9;
    e.midi_note = 68;
    amy_add_event(e);

    // Now just spin for 5s
    while(amy_sysclock() - start < 5000) {
        usleep(THREAD_USLEEP);
    }

    return 0;
}

