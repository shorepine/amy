// amy-example.c
// a simple C example that plays audio using AMY out your speaker 

#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) &&!defined(ARDUINO)

#include "amy.h"
#include "examples.h"
#include "libminiaudio-audio.h"

#include <sndfile.h>

int main(int argc, char ** argv) {
    char *output_filename = NULL;
    SNDFILE *outfile = NULL;
    SF_INFO sfinfo;

    
    int opt;
    while((opt = getopt(argc, argv, ":d:o:lh")) != -1) 
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
            case 'o':
                output_filename = strdup(optarg);
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
    if (output_filename) {
	memset (&sfinfo, 0, sizeof (sfinfo));
	sfinfo.samplerate = AMY_SAMPLE_RATE;
	sfinfo.channels = AMY_NCHANS;
	sfinfo.format = (SF_FORMAT_WAV | SF_FORMAT_PCM_16) ;
        if (! (outfile = sf_open(output_filename, SFM_WRITE, &sfinfo))) {
            fprintf(stderr, "Error : could not open file : %s\n", output_filename) ;
            exit (1) ;
        }
    } else {
        amy_live_start();
    }
    amy_reset_oscs();

    //example_reverb();
    //example_chorus();
    example_drums(start, 4);
    example_multimbral_fm(start + 2000, /* start_osc= */ 6);

    // Now just spin for 20s
    while(amy_sysclock() - start < 20000) {
        if (outfile) {
            int16_t *samples = fill_audio_buffer_task();
            int num_samples = AMY_BLOCK_SIZE * AMY_NCHANS;
            sf_write_short(outfile, samples, num_samples);
        }
        usleep(THREAD_USLEEP);
    }
    
    if (outfile) {
        sf_close(outfile);
    } else {
        amy_live_stop();
    }

    return 0;
}

#endif
