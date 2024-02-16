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
                amy_device_id = atoi(optarg);
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


#ifdef PCM_MUTABLE
    for (int i=pcm_samples; i<pcm_samples + PCM_USER; i++) {
        pcm_map[i].offset = 0;
        pcm_map[i].length = 0;
        pcm_map[i].loopstart = 0;
        pcm_map[i].loopend = 0;
        pcm_map[i].sample = NULL;
        pcm_map[i].sample_rate_index = 0; // 0=22050 (for sf), 1=44100 (for user)
        pcm_map[i].midinote = 0;
    }
#endif

    amy_start(/* cores= */ 1, /* reverb= */ 1, /* chorus= */ 1);
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
#ifdef PCM_MUTABLE
                case 'm': {
                        int p = 26;
                        int16_t *sample = NULL;
                        int length = pcm_map[p].length;
                        fprintf(stdout, "creating PCM sample @ p%d from [%d]\n", pcm_samples, p);
                        if (sample) {
                            free(sample);
                        }
                        sample = (int16_t *)malloc(length * sizeof(int16_t));
                        int offset = pcm_map[p].offset;
                        printf("%p/%d <- [%d]\n", sample, length, offset);
                        for (int i=0; i<length; i++) {
                            sample[length-i] = pcm[offset+i];
                        }
                        pcm_map[pcm_samples].offset = 0;
                        pcm_map[pcm_samples].length = length;
                        pcm_map[pcm_samples].loopstart = pcm_map[p].loopstart;
                        pcm_map[pcm_samples].loopend = pcm_map[p].loopend;
                        pcm_map[pcm_samples].sample = sample;
                        pcm_map[pcm_samples].sample_rate_index = 0;
                        pcm_map[pcm_samples].midinote = pcm_map[p].midinote;
                    }
                    break;
                case 'p': {
                        char c = input[2];
                        int n = pcm_samples;
                        if (c >= '0' && c <= '9') {
                            n = atoi(input+2);
                        }
                        printf("[%d] off:%d len:%d lstart:%d lend:%d midi:%d rate:%d sample:%p\n",
                            n,
                            pcm_map[n].offset,
                            pcm_map[n].length,
                            pcm_map[n].loopstart,
                            pcm_map[n].loopend,
                            pcm_map[n].midinote,
                            pcm_map[n].sample_rate_index,
                            pcm_map[n].sample);
                        int16_t *table = NULL;
                        if (pcm_map[n].sample == NULL) {
                            table = (int16_t *)pcm + pcm_map[n].offset;
                            for (int i=0; i<8; i++) {
                                printf("%5d ", table[i]);
                            }
                            puts("");
                        } else {
                            table = pcm_map[n].sample;
                            for (int i=0; i<8; i++) {
                                printf("%5d ", table[i]);
                            }
                            puts("");
                        }
                    }
                    break;
#endif
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
