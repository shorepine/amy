// amy-message.c
// hacked from amy-example.c, this code shows using miniaudio and allows entry it AMY ASCII commands.
#ifndef ARDUINO

#include "amy.h"
#include "libminiaudio-audio.h"

void delay_ms(uint32_t ms) {
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < ms) usleep(THREAD_USLEEP);
}
int main(int argc, char ** argv) {
    int8_t playback_device_id = -1;
    int8_t capture_device_id = -1;
    int opt;
    while((opt = getopt(argc, argv, ":d:lh")) != -1) 
    { 
        switch(opt) 
        { 
            case 'd': 
                playback_device_id = atoi(optarg);
                break;
            case 'c': 
                capture_device_id = atoi(optarg);
                break;
            case 'l':
                amy_print_devices();
                return 0;
                break;
            case 'h':
                printf("usage: amy-message\n");
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


    amy_config_t amy_config = amy_default_config();
    amy_config.audio = AMY_AUDIO_IS_MINIAUDIO;
    amy_config.playback_device_id = playback_device_id;
    amy_config.capture_device_id = capture_device_id;
    amy_start(amy_config);
    amy_live_start();

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
                    fprintf(stdout, "%" PRIu32 "\n", amy_global.total_blocks);
                    break;
                default:
                    fprintf(stdout, "?\n");
                    break;
            }
        } else {
            amy_add_message(input);
        }
    }
    
    amy_live_stop();

    return 0;
}
#endif
