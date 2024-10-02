// amy-message.c
// hacked from amy-example.c, this code shows using miniaudio and allows entry it AMY ASCII commands.
#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) &&!defined(ARDUINO)

#include "amy.h"
#include "libminiaudio-audio.h"

void delay_ms(uint32_t ms) {
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < ms) usleep(THREAD_USLEEP);
}
int main(int argc, char ** argv) {

    int opt;
    while((opt = getopt(argc, argv, ":d:lh")) != -1) 
    { 
        switch(opt) 
        { 
            case 'd': 
                amy_playback_device_id = atoi(optarg);
                break;
            case 'c': 
                amy_capture_device_id = atoi(optarg);
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



    amy_start(/* cores= */ 1, /* reverb= */ 1, /* chorus= */ 1, /* echo= */1);
    amy_live_start();
    amy_reset_oscs();

    while (1) {
        char input[1024];
        fprintf(stdout, "#;\n");
        if (fgets(input, sizeof(input)-1, stdin) == NULL) break;
        if (input[0] == '?') {
            switch (input[1]) {
                case 'c':
                    fprintf(stdout, "%" PRIu32 "\n", amy_sysclock());
                    break;
                case 's':
                    fprintf(stdout, "%" PRIu32 "\n", total_samples);
                    break;
                default:
                    fprintf(stdout, "?\n");
                    break;
            }
        } else {
            amy_play_message(input);
        }
    }
    
    amy_live_stop();

    return 0;
}
#endif
