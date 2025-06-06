// libminiaudio-audio.c
// functions for running AMY on a computer
#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) && !defined(ARDUINO)
#include "amy.h"

#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION

extern SAMPLE ** fbl;

#ifdef __APPLE__
    #define MA_NO_RUNTIME_LINKING
#endif
#define MINIAUDIO_IMPLEMENTATION

#define MA_AUDIO_WORKLETS_THREAD_STACK_SIZE 131072
#define MINIAUDIO_IMPLEMENTATION

#include "miniaudio.h"

#include <stdio.h>
#include <unistd.h>

#define DEVICE_FORMAT       ma_format_s16

int16_t * leftover_buf;
uint16_t leftover_samples = 0;
pthread_t amy_live_thread;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
float amy_web_cv_1 = 0;
float amy_web_cv_2 = 0;
extern void sequencer_check_and_fill();
void main_loop__em()
{
    // We call repeatedly here to fill the sequencer, for webassembly (no threads)
    sequencer_check_and_fill();
    amy_web_cv_1 = EM_ASM_DOUBLE({ 
        if(typeof cv_1_voltage != 'undefined') { return cv_1_voltage; } else { return 0; }
    });
    amy_web_cv_2 = EM_ASM_DOUBLE({ 
        if(typeof cv_2_voltage != 'undefined') { return cv_2_voltage; } else { return 0; }
    });
}
#endif

void amy_update() {
    // does nothing on miniaudio
}

void amy_print_devices() {
    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        fprintf(stderr,"Failed to setup context for device list.\n");
        exit(1);
    }

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info* pCaptureInfos;
    ma_uint32 captureCount;
    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
        fprintf(stderr,"Failed to get device list.\n");
        exit(1);
    }
    fprintf(stderr, "output devices:\n");
    for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice += 1) {
        fprintf(stderr,"\t%d - %s\n", iDevice, pPlaybackInfos[iDevice].name);
    }

    fprintf(stderr, "input devices:\n");
    for (ma_uint32 iDevice = 0; iDevice < captureCount; iDevice += 1) {
        fprintf(stderr,"\t%d - %s\n", iDevice, pCaptureInfos[iDevice].name);
    }

    ma_context_uninit(&context);
}


// I've seen frame counts as big as 1440, I think *8 is enough room (2048)
#define OUTPUT_RING_FRAMES (AMY_BLOCK_SIZE*8)
#define OUTPUT_RING_LENGTH (OUTPUT_RING_FRAMES*AMY_NCHANS)


int16_t output_ring[OUTPUT_RING_LENGTH];

uint16_t ring_write_ptr = AMY_BLOCK_SIZE*AMY_NCHANS; // start after one AMY frame
uint16_t ring_read_ptr = 0 ;
uint16_t in_ptr = 0;
extern output_sample_type *amy_in_block;

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frame_count) {
    short int *poke = (short *)pOutput;
    short int *peek = (short *)pInput;
    // We lag a AMY block behind input here because our frame size is never a multiple of AMY block size
    for(uint16_t frame=0;frame<frame_count;frame++) {
        for(uint8_t c=0;c<AMY_NCHANS;c++) {
            amy_in_block[in_ptr++] = peek[AMY_NCHANS * frame + c];
        }
        if(in_ptr == (AMY_BLOCK_SIZE*AMY_NCHANS)) { // we have a block of input ready
            // render and copy into output ring buffer
            int16_t * buf = amy_simple_fill_buffer();
            // reset the input pointer for future input data
            in_ptr = 0;
            // copy this output to a ring buffer
            for(uint16_t amy_frame=0;amy_frame<AMY_BLOCK_SIZE;amy_frame++) {
                for(uint8_t c=0;c<AMY_NCHANS;c++) {
                    output_ring[ring_write_ptr++] = buf[AMY_NCHANS * amy_frame + c];
                    if(ring_write_ptr == OUTPUT_RING_LENGTH) ring_write_ptr = 0;
                }
            }
        }
        // Per audio frame, copy (lagged by AMY_BLOCK_SIZE) output data from AMY into expected audio out buffer
        for(uint8_t c=0;c<AMY_NCHANS;c++) {
            poke[frame*AMY_NCHANS + c] = output_ring[ring_read_ptr++];
            if(ring_read_ptr == OUTPUT_RING_LENGTH ) ring_read_ptr = 0;
        }
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

// start 

amy_err_t miniaudio_init() {
    leftover_buf = malloc_caps(sizeof(int16_t)*AMY_BLOCK_SIZE*AMY_NCHANS, amy_global.config.ram_caps_fbl);

    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        printf("Failed to setup context for device list.\n");
        exit(1);
    }
    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
        printf("Failed to get device list.\n");
        exit(1);
    }
    
    if(AMY_HAS_AUDIO_IN) {
        if (amy_global.config.playback_device_id >= (int32_t)playbackCount || amy_global.config.capture_device_id >= (int32_t)captureCount) {
            printf("invalid device\n");
            exit(1);
        }
    } else {
        if (amy_global.config.playback_device_id >= (int32_t)playbackCount) {
            printf("invalid device\n");
            exit(1);
        }
    }

    if(AMY_HAS_AUDIO_IN) {
        deviceConfig = ma_device_config_init(ma_device_type_duplex);
    } else {
        deviceConfig = ma_device_config_init(ma_device_type_playback);
    }

    if(amy_global.config.playback_device_id >= 0) {
        deviceConfig.playback.pDeviceID = &pPlaybackInfos[amy_global.config.playback_device_id].id;
    } else {
        deviceConfig.playback.pDeviceID = NULL; // system default
    }
    deviceConfig.playback.format   = DEVICE_FORMAT;
    deviceConfig.playback.channels = AMY_NCHANS;

    if(AMY_HAS_AUDIO_IN) {
        if(amy_global.config.capture_device_id >= 0) {
            deviceConfig.capture.pDeviceID = &pCaptureInfos[amy_global.config.capture_device_id].id;
        } else {
            deviceConfig.capture.pDeviceID = NULL; // system default
        }
        deviceConfig.capture.format   = DEVICE_FORMAT;
        deviceConfig.capture.channels = AMY_NCHANS;
    }

    deviceConfig.sampleRate        = AMY_SAMPLE_RATE;
    deviceConfig.dataCallback      = data_callback;
    deviceConfig.pUserData         = _custom;

    // Force miniaudio's callback to be the same size as AMY. This helps us not loop too many fill_buffer calls
    deviceConfig.periodSizeInFrames=AMY_BLOCK_SIZE;
    
    if (ma_device_init(&context, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Failed to open playback device.\n");
        exit(1);
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&device);
        exit(1);
    }
    for(uint16_t i=0;i<OUTPUT_RING_LENGTH;i++) output_ring[i] = 0;

    return AMY_OK;
}

void *miniaudio_run(void *vargp) {
    miniaudio_init();
    
    while(amy_global.running) {
        sleep(1);
    }
    return NULL;
}

#ifdef __EMSCRIPTEN__
void amy_live_start_web_audioin() {
    amy_global.config.features.audio_in = 1;
    emscripten_cancel_main_loop();
    miniaudio_init();
    emscripten_set_main_loop(main_loop__em, 0, 0);
}
void amy_live_start_web() {
    amy_global.config.features.audio_in = 0;
    emscripten_cancel_main_loop();
    miniaudio_init();
    emscripten_set_main_loop(main_loop__em, 0, 0);
}
#endif
void amy_live_start() {
    // kick off a thread running miniaudio_run
    amy_global.running = 1;
    pthread_create(&amy_live_thread, NULL, miniaudio_run, NULL);
}


void amy_live_stop() {
    amy_global.running = 0;
    ma_device_uninit(&device);
    free(leftover_buf);
}
#endif
