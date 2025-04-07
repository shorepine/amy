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

// Example how to use external render hook
uint8_t render(uint16_t osc, SAMPLE * buf, uint16_t len) {
    //fprintf(stderr, "render hook %d\n", osc);
    return 0; // 0 means, ignore this. 1 means, i handled this and don't mix it in with the audio
}

extern void instrument_test(void);

int main(int argc, char ** argv) {
    char *output_filename = NULL;
    //fprintf(stderr, "main init. pcm is %p pcm_map is %p\n",  pcm, pcm_map);
    /*
    fprintf(stderr, "sizeof synthinfo %d event %d delta %d\n",
	sizeof(struct synthinfo),
	sizeof(struct event),
	sizeof(struct delta)
	);
    */
    int opt;
    while((opt = getopt(argc, argv, ":d:o:lh")) != -1) 
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
            case 'o':
                output_filename = strdup(optarg);
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

    
    #if AMY_HAS_CUSTOM == 1
    example_init_custom();
    #endif

    amy_start(/* cores= */ 1, /* reverb= */ 1, /* chorus= */ 1, /* echo= */ 1, /* setup default synth */ 0);
    
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
        amy_live_start(1);
    }

    /*
    example_voice_chord(0, 0);

    // Reset the oscs 
    struct event e = amy_default_event();
    e.osc = 0;
    e.reset_osc = 1000;
    e.time = 4500;
    amy_add_event(e);

    example_fm(5000);



    example_drums(10000,2);
    */


  instrument_test();

  struct event a = amy_default_event();
  a.osc = 1;
  a.wave = SINE;
  a.freq_coefs[0] = 0.25;

  a.amp_coefs[0] = 1;

  amy_add_event(a);

  struct event e = amy_default_event();
  e.osc = 0;
  e.wave = PULSE;
  e.freq_coefs[0] = 440;

  e.mod_source = 1;

  e.duty_coefs[5] = 1;
  e.velocity = 1;
  amy_add_event(e);

    // Now just spin for 15s
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < 15000) {
        if (output_filename) {
            int16_t * frames = amy_simple_fill_buffer();
            int num_frames = AMY_BLOCK_SIZE;
            ma_encoder_write_pcm_frames(&encoder, frames, num_frames, NULL);
        }
        usleep(THREAD_USLEEP);
    }
    
    amy_reset_oscs();
    example_fm(0);

    // Now just spin for 1s
    while(amy_sysclock() - start < 6000) {
        if (output_filename) {
            int16_t * frames = amy_simple_fill_buffer();
            int num_frames = AMY_BLOCK_SIZE;
            ma_encoder_write_pcm_frames(&encoder, frames, num_frames, NULL);
        }
        usleep(THREAD_USLEEP);
    }


    if (output_filename) {
        ma_encoder_uninit(&encoder);
    } else {
        amy_live_stop();
    }

    return 0;
}

#endif
