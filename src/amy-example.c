// amy-example.c
// a simple C example that plays audio using AMY out your speaker 

#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) &&!defined(ARDUINO)
#include "amy.h"
#include "examples.h"
#include "miniaudio.h"
#include "libminiaudio-audio.h"

void delay_ms(uint32_t ms) {
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < ms) usleep(THREAD_USLEEP);
}

int main(int argc, char ** argv) {
    char *output_filename = NULL;
    //fprintf(stderr, "main init. pcm is %p pcm_map is %p\n",  pcm, pcm_map);

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
    uint32_t start = amy_sysclock();
    
    #if AMY_HAS_CUSTOM == 1
    example_init_custom();
    #endif

    amy_start(/* cores= */ 1, /* reverb= */ 1, /* chorus= */ 1);
    
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
    example_voice_chord(0);

    // Now just spin for 10s
    while(amy_sysclock() - start < 5000) {
        if (output_filename) {
            int16_t * frames = amy_simple_fill_buffer();
            int num_frames = AMY_BLOCK_SIZE;
            ma_encoder_write_pcm_frames(&encoder, frames, num_frames, NULL);
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
