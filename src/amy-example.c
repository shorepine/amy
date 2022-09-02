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

extern void print_devices();
char *raw_file;

int main(int argc, char ** argv) {
    amy_start();

    raw_file = (char*)malloc(sizeof(char)*1025);
    raw_file[0] = 0;

    int opt;
    while((opt = getopt(argc, argv, ":d:c:r:lgh")) != -1) 
    { 
        switch(opt) 
        { 
            case 'r': 
                strcpy(raw_file, optarg);
                break; 
            case 'd': 
                device_id = atoi(optarg);
                break;
            case 'c': 
                channel = atoi(optarg);
                break; 
            case 'l':
                print_devices();
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
    e.time = amy_sysclock();
    e.velocity = 1;
    e.wave = ALGO;
    e.patch = 15;
    e.midi_note = 60;
    amy_add_event(e);
    e.time = amy_sysclock() + 500;
    e.osc = 8; // remember that an FM patch takes up 7 oscillators
    e.midi_note = 64;
    amy_add_event(e);
    e.time = amy_sysclock() + 1000;
    e.osc = 16;
    e.midi_note = 68;
    amy_add_event(e);

    // Now just spin forever 
    while(1) {
        usleep(THREAD_USLEEP);
    }

    return 0;
}

