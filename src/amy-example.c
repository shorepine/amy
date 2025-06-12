// amy-example.c
// a simple C example that plays audio using AMY out your speaker 

#ifndef ARDUINO

#include "amy.h"
#include "examples.h"
#include "miniaudio.h"
#include "libminiaudio-audio.h"

void delay_ms(uint32_t ms) {
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < ms) usleep(THREAD_USLEEP);
}

// Example how to use external render hook
uint8_t render(uint16_t osc, SAMPLE * buf, uint16_t len) {
    //fprintf(stderr, "render hook %d\n", osc);
    return 0; // 0 means, ignore this. 1 means, i handled this and don't mix it in with the audio
}

int main(int argc, char ** argv) {
    int8_t playback_device_id = -1;
    int8_t capture_device_id = -1;
    int opt;
    while((opt = getopt(argc, argv, ":d:o:lh")) != -1) 
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
                printf("usage: amy-example\n");
                printf("\t[-d playback sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-c capture sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-l list all sound devices and exit]\n");
                printf("\t[-o filename.wav - write to filename.wav instead of playing live]\n");
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
    amy_external_render_hook = render;

    amy_config_t amy_config = amy_default_config();
    amy_config.audio = AMY_AUDIO_IS_MINIAUDIO;
    amy_config.playback_device_id = playback_device_id;
    amy_config.capture_device_id = capture_device_id;
    amy_config.features.default_synths = 0;
    amy_start(amy_config);
    
    amy_live_start();
    //example_fm(0);
    //example_voice_chord(0,0);
    example_synth_chord(0, /* patch */ 0);
    //example_sustain_pedal(0, /* patch */ 256);
    //example_sequencer_drums(0);
    //example_patch_from_events();

    // Check that pedal to a non-synth doesn't crash.
    amy_event e = amy_default_event();
    e.synth = 3;
    e.pedal = 1;
    amy_add_event(&e);

    // Now just spin for 15s
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < 5000) {
        usleep(THREAD_USLEEP);
    }

    //show_debug(99);

    amy_live_stop();

    amy_stop();

    return 0;
}

#endif
