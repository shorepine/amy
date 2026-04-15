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

void print_events_for_patch_number(int patch_number) {
    void *state = NULL;
    fprintf(stderr, "start delta_num_free = %d\n", delta_num_free());
    amy_event event = amy_default_event();
    char s[MAX_MESSAGE_LEN];
    do {
        state = yield_patch_events(patch_number, &event, state);
        sprint_event(&event, s, MAX_MESSAGE_LEN, false);
        fprintf(stderr, "%s\n", s);
    } while (state != NULL);
    fprintf(stderr, "end delta_num_free = %d\n", delta_num_free());
}

void print_events_for_synth(int synth, bool wirecode) {
    fprintf(stderr, "synth %d:\n", synth);
    void * state = NULL;
    amy_event event = amy_default_event();
    char s[MAX_MESSAGE_LEN];
    do {
        state = yield_synth_events(synth, &event, true, state);
        sprint_event(&event, s, MAX_MESSAGE_LEN, wirecode);
        fprintf(stderr, "%s\n", s);
    } while(state != NULL);
}

void print_events_for_synth_2(int synth, bool wirecode) {
    fprintf(stderr, "pefs2: synth %d:\n", synth);
    void * state = NULL;
    char s[MAX_MESSAGE_LEN];
    do {
        state = yield_synth_commands(synth, s, MAX_MESSAGE_LEN, true, state);
        fprintf(stderr, "%s\n", s);
    } while(state != NULL);
}

void test_patch_set() {
    // Check that trying to program a non-user patch doesn't crash
    amy_event e = amy_default_event();
    e.patch_number = 25;
    e.osc = 0;
    e.wave = SINE;
    amy_add_event(&e);

    // Change the global volume.
    e = amy_default_event();
    e.volume = 2.0f;
    amy_add_event(&e);
}

void test_loop_env_filt() {
    // After amy/test.py:TestLoopEnvFilt
    //amy.send(time=0, osc=0, wave=amy.PCM, preset=10, feedback=1)
    amy_add_message("t0v0w7p10b1Z");
    //amy.send(time=0, osc=0, filter_type=amy.FILTER_LPF24, filter_freq='200,0,0,0,3', bp1='0,1,500,0,200,0')
    amy_add_message("t0v0G4F200,0,0,0,3B0,1,500,0,200,0Z");
    //amy.send(time=0, osc=0, bp0='100,1,1000,0,1000,0')
    amy_add_message("t0v0A100,1,1000,0,1000,0Z");
    //amy.send(time=0, osc=1, freq='1')
    amy_add_message("t0v1f1Z");
    //amy.send(time=0, osc=0, mod_source=1, freq=',,,,,-0.2')
    amy_add_message("t0v0L1f,,,,,-0.2Z");
    //amy.send(time=100, osc=0, note=64, vel=5)
    amy_add_message("t100v0n64l5Z");
    //amy.send(time=500, osc=0, vel=0)
    amy_add_message("t500v0l0Z");
}

void test_algo() {
    //amy.send(time=0, voices="0",  patch=21+128)
    amy_add_message("t0r0K149Z");
    //amy.send(time=100, voices="0", note=58, vel=1)
    amy_add_message("t100r0n58l1Z");
    //amy.send(time=500, voices="0", vel=0)
    amy_add_message("t400r0l0Z");
}

void test_stored_patch() {
    // Reading back a stored patch
    int patch_number = 0;
    print_events_for_patch_number(patch_number);
    // Reading back a user-defined patch
    patch_number = 1024;
    amy_event e = amy_default_event();
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
    e.eg0_times[0] = 30;   e.eg0_values[0] = 1.0f;
    e.eg0_times[1] = 1355; e.eg0_values[1] = 0.354f;
    e.eg0_times[2] = 232;  e.eg0_values[2] = 0.0f;
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
    e.eg1_times[0] = 30;   e.eg1_values[0] = 1.0f;
    e.eg1_times[1] = 1355; e.eg1_values[1] = 0.354f;
    e.eg1_times[2] = 232;  e.eg1_values[2] = 0.0f;
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
    e.eg0_times[0] = 30;   e.eg0_values[0] = 1.0f;
    e.eg0_times[1] = 1355; e.eg0_values[1] = 0.354f;
    e.eg0_times[2] = 232;  e.eg0_values[2] = 0.0f;
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
    e.eg0_times[0] = 0;      e.eg0_values[0] = 1.0f;
    e.eg0_times[1] = 10000;  e.eg0_values[1] = 0.0f;
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
    amy_config_t amy_config = amy_default_config();
    amy_config.amy_external_render_hook = render;
    amy_config.audio = AMY_AUDIO_IS_MINIAUDIO;
    amy_config.playback_device_id = playback_device_id;
    fprintf(stderr, "playback_device_id=%d\n", playback_device_id);
    amy_config.capture_device_id = capture_device_id;
    //amy_config.features.default_synths = 1;
    amy_config.features.default_synths = 0;

    for (int tries = 0; tries < 2; ++tries) {
    amy_start(amy_config);

    //example_synth_chord(0, /* patch */ 1);

    //example_fm(0);
    //example_voice_chord(0,0);
    //example_sustain_pedal(0, /* patch */ 256);
    //example_sequencer_drums(0);
    //example_patch_from_events();

    //test_loop_env_filt();
    test_algo();

    // Now just spin for a while
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < 5000) {
        usleep(THREAD_USLEEP);
    }

    print_events_for_synth(/* synth */ 0, /* wirecode */ true);
    print_events_for_synth_2(/* synth */ 0, /* wirecode */ true);

    //show_debug(99);
    
    amy_stop();

    // Make sure libminiaudio has time to clean up.
    sleep(2);

    }

    return 0;
}

#endif
