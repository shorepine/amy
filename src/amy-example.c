// amy-example.c
// a simple C example that plays audio using AMY out your speaker 


#include "amy.h"
#include "examples.h"
#include "libminiaudio-audio.h"

int main(int argc, char ** argv) {

    int opt;
    while((opt = getopt(argc, argv, ":d:lh")) != -1) 
    { 
        switch(opt) 
        { 
            case 'd': 
                amy_device_id = atoi(optarg);
                break;
            case 'l':
                amy_print_devices();
                return 0;
                break;
            case 'h':
                printf("usage: amy-example\n");
                printf("\t[-d sound device id, use -l to list, default, autodetect]\n");
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
    int64_t start =amy_sysclock();
    amy_start();
    amy_live_start();
    amy_reset_oscs();

    //example_reverb();
    //example_chorus();
    example_drums(start, 2);
    //example_multimbral_fm(start);

    // Now just spin for 20s
    while(amy_sysclock() - start < 20000) {
        usleep(THREAD_USLEEP);
    }
    
    amy_live_stop();

    return 0;
}

