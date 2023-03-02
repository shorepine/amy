// libminiaudio-audio.c
// functions for running AMY on a computer
#include "amy.h"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
//#define MA_DEBUG_OUTPUT
#define MINIAUDIO_IMPLEMENTATION
//#define MA_NO_PULSEAUDIO
//#define MA_NO_JACK
#include "miniaudio.h"

#include <stdio.h>
#include <unistd.h>

#define DEVICE_FORMAT       ma_format_s16
#define DEVICE_CHANNELS     0
#define DEVICE_SAMPLE_RATE  SAMPLE_RATE

int16_t leftover_buf[BLOCK_SIZE]; 
uint16_t leftover_samples = 0;
int16_t amy_channel = -1;
int16_t amy_device_id = -1;
uint8_t amy_running = 0;
pthread_t amy_live_thread;


void amy_print_devices() {
    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        printf("Failed to setup context for device list.\n");
        exit(1);
    }

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info* pCaptureInfos;
    ma_uint32 captureCount;
    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
        printf("Failed to get device list.\n");
        exit(1);
    }

    for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice += 1) {
        printf("%d - %s\n", iDevice, pPlaybackInfos[iDevice].name);
    }

    ma_context_uninit(&context);
}

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frame_count) {
    // Different audio devices on mac have wildly different frame_count_maxes, so we have to be ok with 
    // an audio buffer that is not an even multiple of BLOCK_SIZE. my iMac's speakers were always 512 frames, but
    // external headphones on a MBP is 432 or 431, and airpods were something like 1440.

    //printf("BLOCK_SIZE:%d frame_count:%d leftover_samples:%d\n", BLOCK_SIZE, frame_count, leftover_samples);
    short int *poke = (short *)pOutput;

    // First send over the leftover samples, if any
    int ptr = 0;

    for(uint16_t frame=0;frame<leftover_samples;frame++) {
        for(uint8_t c=0;c<pDevice->playback.channels;c++) {
            if(c==amy_channel || amy_channel<0) {
                poke[ptr++] = leftover_buf[frame];
            } else {
                poke[ptr++] = 0;                
            }
        }
    }

    frame_count -= leftover_samples;
    leftover_samples = 0;

    // Now send the bulk of the frames
    for(uint8_t i=0;i<(uint8_t)(frame_count / BLOCK_SIZE);i++) {
        int16_t *buf = fill_audio_buffer_task();
        for(uint16_t frame=0;frame<BLOCK_SIZE;frame++) {
            for(uint8_t c=0;c<pDevice->playback.channels;c++) {
                if(c==amy_channel || amy_channel<0) {
                    poke[ptr++] = buf[frame];
                } else {
                    poke[ptr++] = 0;                
                }
            }
        }
    } 

    // If any leftover, let's put those in the outgoing buf and the rest in leftover_samples
    uint16_t still_need = frame_count % BLOCK_SIZE;
    if(still_need != 0) {
        int16_t *buf = fill_audio_buffer_task();
        for(uint16_t frame=0;frame<still_need;frame++) {
            for(uint8_t c=0;c<pDevice->playback.channels;c++) {
                if(c==amy_channel || amy_channel<0) {
                    poke[ptr++] = buf[frame];
                } else {
                    poke[ptr++] = 0;
                }
            }
        }
        memcpy(leftover_buf, buf+still_need, (BLOCK_SIZE - still_need)*2);
        leftover_samples = BLOCK_SIZE - still_need;
    }
}

ma_device_config deviceConfig;
ma_device device;
unsigned char _custom[4096];
ma_context context;
ma_device_info* pPlaybackInfos;
ma_uint32 playbackCount;
ma_device_info* pCaptureInfos;
ma_uint32 captureCount;

// start soundio

amy_err_t miniaudio_init() {
    if (amy_device_id < 0) {
        amy_device_id = 0;
    }

    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        printf("Failed to setup context for device list.\n");
        exit(1);
    }

    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
        printf("Failed to get device list.\n");
        exit(1);
    }
    
    if (amy_device_id >= playbackCount) {
        printf("invalid playback device\n");
        exit(1);
    }

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    
    deviceConfig.playback.pDeviceID = &pPlaybackInfos[amy_device_id].id;
    deviceConfig.playback.format   = DEVICE_FORMAT;
    deviceConfig.playback.channels = DEVICE_CHANNELS;
    deviceConfig.sampleRate        = DEVICE_SAMPLE_RATE;
    deviceConfig.dataCallback      = data_callback;
    deviceConfig.pUserData         = _custom;
    
    if (ma_device_init(&context, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Failed to open playback device.\n");
        exit(1);
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&device);
        exit(1);
    }
    return AMY_OK;
}

void *miniaudio_run(void *vargp) {
    miniaudio_init();
    while(amy_running) {
        sleep(1);
    }
    return NULL;
}

void amy_live_start() {
    // kick off a thread running miniaudio_run
    amy_running = 1;
    pthread_create(&amy_live_thread, NULL, miniaudio_run, NULL);
}


void amy_live_stop() {
    amy_running = 0;
    ma_device_uninit(&device);
}

