// amy-example.c
// a simple C example that plays audio using AMY out your speaker or a raw file

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
    start_amy();
    reset_oscs();

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
            case 'g':
                DEBUG = 1;
                break;
            case 'h':
                printf("usage: alles\n");
                printf("\t[-d sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-c sound channel, default -1 for all channels on device]\n");
                printf("\t[-l list all sound devices and exit]\n");
                printf("\t[-g show debug info]\n");
                printf("\t[-r output audio to specified raw file (1-channel 16-bit signed int, 44100Hz)\n");
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
    live_start();
    usleep(1000*1000);
    reset_oscs();
    while(1) {
        usleep(THREAD_USLEEP);
    }

    return 0;
}

