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

extern void *event_generator_for_patch_number(uint16_t patch_number, struct amy_event *event, void *state);
extern void print_event(amy_event *e);

void print_events_for_patch_number(int patch_number) {
    void *state = NULL;
    fprintf(stderr, "start delta_num_free = %d\n", delta_num_free());
    do {
        amy_event event = amy_default_event();
        state = event_generator_for_patch_number(patch_number, &event, state);
        print_event(&event);
    } while (state != NULL);
    fprintf(stderr, "end delta_num_free = %d\n", delta_num_free());
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
    fprintf(stderr, "playback_device_id=%d\n", playback_device_id);
    amy_config.capture_device_id = capture_device_id;
    amy_config.features.default_synths = 1;

    for (int tries = 0; tries < 2; ++tries) {
    amy_start(amy_config);
        
    //example_fm(0);
    //example_voice_chord(0,0);
    example_synth_chord(0, /* patch */ 0);
    //example_sustain_pedal(0, /* patch */ 256);
    //example_sequencer_drums(0);
    //example_patch_from_events();

    // Check that trying to program a non-user patch doesn't crash
    amy_event e = amy_default_event();
    e.patch_number = 25;
    e.osc = 0;
    e.wave = SINE;
    amy_add_event(&e);

    // Reading back a stored patch
    int patch_number = 0;
    print_events_for_patch_number(patch_number);
    // Reading back a user-defined patch
    patch_number = 1024;
    e = amy_default_event();
    e.patch_number = patch_number;
    e.osc = 0;
    e.wave = PULSE;
    e.mod_source = 2;
    e.amp_coefs[COEF_VEL] = 1.0f;
    e.amp_coefs[COEF_EG0] = 1.0f;
    e.amp_coefs[COEF_EG1] = 0;
    e.freq_coefs[COEF_CONST] = 130.81f;
    e.freq_coefs[COEF_NOTE] = 1.f;
    e.portamento_ms = 0;
    strcpy(e.bp0, "30,1,1355,0.354,232,0");
    amy_add_event(&e);

    e = amy_default_event();
    e.patch_number = patch_number;
    e.osc = 0;
    e.chained_osc = 1;
    e.filter_type = FILTER_LPF24;
    e.filter_freq_coefs[COEF_CONST] = 126.54f;
    e.filter_freq_coefs[COEF_NOTE] = 0.677f;
    e.filter_freq_coefs[COEF_EG1] = 5.024f;
    e.resonance = 0.93f;
    strcpy(e.bp1, "30,1,1355,0.354,232,0");
    amy_add_event(&e);

    e = amy_default_event();
    e.patch_number = patch_number;
    e.osc = 1;
    e.wave = SAW_UP;
    e.mod_source = 2;
    e.amp_coefs[COEF_VEL] = 1.0f;
    e.amp_coefs[COEF_EG0] = 1.0f;
    e.amp_coefs[COEF_EG1] = 0;
    e.freq_coefs[COEF_CONST] = 130.81f;
    e.freq_coefs[COEF_NOTE] = 1.f;
    e.portamento_ms = 0;
    strcpy(e.bp0, "30,1,1355,0.354,232,0");
    amy_add_event(&e);

    e = amy_default_event();
    e.patch_number = patch_number;
    e.osc = 2;
    e.wave = TRIANGLE;
    e.mod_source = 2;
    e.amp_coefs[COEF_CONST] = 1.f;
    e.amp_coefs[COEF_VEL] = 0;
    e.amp_coefs[COEF_EG0] = 1.0f;
    e.freq_coefs[COEF_CONST] = 4.0f;
    e.freq_coefs[COEF_NOTE] = 0;
    strcpy(e.bp0, "0,1,10000,0");
    amy_add_event(&e);

    e = amy_default_event();
    e.patch_number = patch_number;
    e.eq_l = 0;
    e.eq_m = 0;
    e.eq_h = 0;
    e.chorus_level = 0;
    e.chorus_lfo_freq = 0.5f;
    e.chorus_depth = 0.5f;
    amy_add_event(&e);

    print_events_for_patch_number(patch_number);    
    
    
    // Now just spin for a while
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < 5000) {
        usleep(THREAD_USLEEP);
    }

    //show_debug(99);
    
    amy_stop();

    // Make sure libminiaudio has time to clean up.
    sleep(2);

    }

    return 0;
}

#endif
