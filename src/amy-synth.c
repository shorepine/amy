// amy-example.c
// a simple C example that plays audio using AMY out your speaker 

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

int get_engine_index(int p) {
    int n;
    if      ((p >=    0) && (p <  128)) n =  6;
    else if ((p >=  128) && (p <  256)) n =  7;
    else if ((p >=  256) && (p <  384)) n =  5;
    else if ((p >= 1024) && (p < 1280)) n =  4;
    else                                n =  0;
    return n;
}

const char *eng_name[] = {
    "(unknown)",  "(unknown)",   "(unknown)",    "(unknown)",
    "User Patch", "Dan's Piano", "Roland Juno6", "Yamaha DX7"
};

int eng_poly[] = {   1,   0,   0,   0,  16,   4,   6,   8 };	// embedded limits, can we do more on a raspberry 4+ ???
//int eng_poly[] = {   1,   0,   0,   0,  16,   6,  12,  16 };	// bummer, more than 6 notes seems to break piano anyway ?!?

int eng_nprg[] = {   1,   1,   1,   1,  32,   1, 128, 128 };	// number of programs per "engine"

#define get_eng_name(n) eng_name[n & 7]
#define get_eng_poly(n) eng_poly[n & 7]
#define get_eng_nprg(n) eng_nprg[n & 7]

int main(int argc, char *argv[])
{
    int8_t playback_device_id = -1;
    int8_t capture_device_id  = -1;
    int patch_number = -1;
    int opt, eng_ndx, maxprog, nvoices, running;

    while ((opt = getopt(argc, argv, ":d:o:p:lh")) != -1) {
        switch (opt) {
            case 'p': patch_number = atoi(optarg);		break;
            case 'd': playback_device_id = atoi(optarg);	break;
            case 'c': capture_device_id = atoi(optarg);		break;
            case 'l': amy_print_devices(); return 0;		break;
            case 'h':
                printf("usage: amy-example\n");
                printf("\t[-d playback sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-c capture sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-l list all sound devices and exit]\n");
                printf("\t[-o filename.wav - write to filename.wav instead of playing live]\n");
                printf("\t[-p initial patch number (0=juno6, 128=dx7, 1024=piano)]\n");
                printf("\t[-h show this help and exit]\n");
                return 0; break;
            case ':': printf("option needs a value\n");		break;
            case '?': printf("unknown option: %c\n", optopt);	break;
        }
    }
    amy_config_t amy_config = amy_default_config();
    amy_config.amy_external_render_hook = render;
    amy_config.audio = AMY_AUDIO_IS_MINIAUDIO;
    amy_config.midi  = AMY_MIDI_IS_UART;
    amy_config.playback_device_id = playback_device_id;
    fprintf(stderr, "playback_device_id=%d\n", playback_device_id);
    amy_config.capture_device_id = capture_device_id;
    amy_config.features.default_synths = 1;

    amy_start(amy_config);

    if ((patch_number < 0) || (patch_number > 1055))
        patch_number = 0;
//  print_events_for_patch_number(patch_number);

    eng_ndx = get_engine_index(patch_number);
    maxprog = get_eng_nprg(eng_ndx);
    nvoices = get_eng_poly(eng_ndx);

    amy_event e = amy_default_event();
    e.synth        = 1;
    e.num_voices   = nvoices;
    e.patch_number = patch_number;
    amy_add_event(&e);

    // Now just spin for a while
//    uint32_t start = amy_sysclock();
//    while (amy_sysclock() - start < 5000) {
//        usleep(THREAD_USLEEP);
//    }

//  show_debug(99);

    fprintf(stderr, "requested patch number is %d\n", patch_number);
    fprintf(stderr, "synth #1: engine%d = %s\n", eng_ndx, get_eng_name(eng_ndx));
    fprintf(stderr, "synth #1: numprgs = %d\n", maxprog);
    fprintf(stderr, "synth #1: maxpoly = %d\n", nvoices);
    fprintf(stderr, "synth #1: program = %d\n", patch_number % maxprog); // FIXME!
    running = true;
    fprintf(stdout, "\nentering MIDI loop - ctrl-C to exit\n");

    while (running) {
//	midi_process(midi_read());
	sleep(1);
    }

    amy_stop();
    // Make sure libminiaudio has time to clean up.
    sleep(2);
    return 0;
}
