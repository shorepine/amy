// amy-example.c
// a simple C example that plays audio using AMY out your speaker 

#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) &&!defined(ARDUINO)

#include "amy.h"
#include "examples.h"
#include "miniaudio.h"
#include "libminiaudio-audio.h"

int main(int argc, char ** argv) {

    char *output_filename = NULL;
    
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
    uint32_t start = amy_sysclock();

    amy_start();

    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, AMY_NCHANS, AMY_SAMPLE_RATE);
    ma_encoder encoder;
    ma_result result;
    if (output_filename) {
        result = ma_encoder_init_file(output_filename, &config, &encoder);
        if (result != MA_SUCCESS) {
            fprintf(stderr, "Error : could not open file : %s (%d)\n", output_filename, result) ;
            exit (1) ;
        }
    } else {
        amy_live_start();
    }

    amy_reset_oscs();

    //example_reverb();
    //example_chorus();
    //example_sine(start);
    example_drums(start, 4);
    example_multimbral_fm(start + 2000, /* start_osc= */ 6);

    // Now just spin for 10s
    while(amy_sysclock() - start < 10000) {
        if (output_filename) {
            int16_t *frames = fill_audio_buffer_task();
            int num_frames = AMY_BLOCK_SIZE;
            result = ma_encoder_write_pcm_frames(&encoder, frames, num_frames, NULL);
        }
        usleep(THREAD_USLEEP);
    }
    show_debug(3);

    if (output_filename) {
        ma_encoder_uninit(&encoder);
    } else {
        amy_live_stop();
    }

    return 0;
}

#endif
