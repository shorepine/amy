// libminiaudio-audio.c
// functions for running AMY on a computer
#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) && !defined(ARDUINO)
#include "amy.h"

//#define MA_NO_DECODING
//#define MA_NO_ENCODING
//#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
//#define MA_DEBUG_OUTPUT

#ifdef __APPLE__
    #define MA_NO_RUNTIME_LINKING
#endif
#define MINIAUDIO_IMPLEMENTATION
//#define MA_NO_PULSEAUDIO
//#define MA_NO_JACK

#include "miniaudio.h"

#include <stdio.h>
#include <unistd.h>

#define DEVICE_FORMAT       ma_format_s16

int16_t * leftover_buf;
uint16_t leftover_samples = 0;
int16_t amy_playback_device_id = -1;
int16_t amy_capture_device_id = -1;
uint8_t amy_running = 0;
pthread_t amy_live_thread;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

void main_loop__em()
{
}
#endif


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
#ifdef __EMSCRIPTEN__
//#include "console.h"
#endif

// I've seen frame counts as big as 1440, I think *8 is enough room (2048)
#define OUTPUT_RING_FRAMES (AMY_BLOCK_SIZE*8)
#define OUTPUT_RING_LENGTH (OUTPUT_RING_FRAMES*AMY_NCHANS)
int16_t output_ring[OUTPUT_RING_LENGTH];

uint16_t ring_write_ptr = AMY_BLOCK_SIZE*AMY_NCHANS; // start after one AMY frame
uint16_t ring_read_ptr = 0 ;
uint16_t in_ptr = 0;
extern int16_t amy_in_block[AMY_BLOCK_SIZE*AMY_NCHANS];

static void data_callback_pro(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frame_count) {
    // Lag input and render only when we have 256 frames of input

    short int *poke = (short *)pOutput;
    short int *peek = (short *)pInput;

    // We lag a AMY block behind input here because our frame size is never a multiple of AMY block size

    for(uint16_t frame=0;frame<frame_count;frame++) {
        for(uint8_t c=0;c<AMY_NCHANS;c++) {
            amy_in_block[in_ptr++] = peek[AMY_NCHANS * frame + c];
        }
        if(in_ptr == (AMY_BLOCK_SIZE*AMY_NCHANS)) { // we have a block of input ready
            // render and copy into output
            int16_t * buf = amy_simple_fill_buffer();
            in_ptr = 0;
            // copy this output to a ring buffer
            for(uint16_t amy_frame=0;amy_frame<AMY_BLOCK_SIZE;amy_frame++) {
                for(uint8_t c=0;c<AMY_NCHANS;c++) {
                    output_ring[ring_write_ptr % OUTPUT_RING_LENGTH] = buf[AMY_NCHANS * amy_frame + c];
                    ring_write_ptr++; if(ring_write_ptr == OUTPUT_RING_LENGTH) ring_write_ptr = 0;
                }
            }
        }

        for(uint8_t c=0;c<AMY_NCHANS;c++) {
            poke[frame*AMY_NCHANS + c] = output_ring[ring_read_ptr];
            ring_read_ptr++; if(ring_read_ptr == OUTPUT_RING_LENGTH ) ring_read_ptr = 0;
        }
    }

}


static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frame_count) {
    // Different audio devices on mac have wildly different frame_count_maxes, so we have to be ok with 
    // an audio buffer that is not an even multiple of BLOCK_SIZE. my iMac's speakers were always 512 frames, but
    // external headphones on a MBP is 432 or 431, and airpods were something like 1440.
    short int *poke = (short *)pOutput;

    // Deal with the output. The output gets filled into poke[], and likely requires multiple amy_simple_fill_buffer() calls.
    // First send over the leftover samples, if any
    int ptr = 0;

    for(uint16_t frame=0;frame<leftover_samples;frame++) {
        for(uint8_t c=0;c<pDevice->playback.channels;c++) {
            poke[ptr++] = leftover_buf[AMY_NCHANS * frame + c];
        }
    }

    frame_count -= leftover_samples;
    leftover_samples = 0;

    // Now send the bulk of the frames
    for(uint8_t i=0;i<(uint8_t)(frame_count / AMY_BLOCK_SIZE);i++) {
        int16_t * buf = amy_simple_fill_buffer();
        for(uint16_t frame=0;frame<AMY_BLOCK_SIZE;frame++) {
            for(uint8_t c=0;c<pDevice->playback.channels;c++) {
                poke[ptr++] = buf[AMY_NCHANS * frame + c];
            }
        }
    } 

    // If any leftover, let's put those in the outgoing buf and the rest in leftover_samples
    uint16_t still_need = frame_count % AMY_BLOCK_SIZE;
    if(still_need != 0) {
        int16_t * buf = amy_simple_fill_buffer();
        for(uint16_t frame=0;frame<still_need;frame++) {
            for(uint8_t c=0;c<pDevice->playback.channels;c++) {
                poke[ptr++] = buf[AMY_NCHANS * frame + c];
            }
        }
        memcpy(leftover_buf, buf+(still_need*AMY_NCHANS), (AMY_BLOCK_SIZE - still_need)*AMY_BYTES_PER_SAMPLE*AMY_NCHANS);
        leftover_samples = AMY_BLOCK_SIZE - still_need;
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
    leftover_buf = malloc_caps(sizeof(int16_t)*AMY_BLOCK_SIZE*AMY_NCHANS, FBL_RAM_CAPS);

    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        printf("Failed to setup context for device list.\n");
        exit(1);
    }

    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
        printf("Failed to get device list.\n");
        exit(1);
    }
    
    if (amy_playback_device_id >= (int32_t)playbackCount || amy_capture_device_id >= (int32_t)captureCount) {
        printf("invalid device\n");
        exit(1);
    }

    deviceConfig = ma_device_config_init(ma_device_type_duplex);

    if(amy_playback_device_id >= 0) {
        deviceConfig.playback.pDeviceID = &pPlaybackInfos[amy_playback_device_id].id;
    } else {
        deviceConfig.playback.pDeviceID = NULL; // system default
    }
    deviceConfig.playback.format   = DEVICE_FORMAT;
    deviceConfig.playback.channels = AMY_NCHANS;

    if(amy_capture_device_id >= 0) {
        deviceConfig.capture.pDeviceID = &pCaptureInfos[amy_capture_device_id].id;
    } else {
        deviceConfig.capture.pDeviceID = NULL; // system default
    }
    deviceConfig.capture.format   = DEVICE_FORMAT;
    deviceConfig.capture.channels = AMY_NCHANS;

    deviceConfig.sampleRate        = AMY_SAMPLE_RATE;
    deviceConfig.dataCallback      = data_callback_pro;
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
    for(uint16_t i=0;i<OUTPUT_RING_LENGTH;i++) output_ring[i] = 0;

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
    #ifdef __EMSCRIPTEN__
    miniaudio_init();
    emscripten_set_main_loop(main_loop__em, 0, 0);
    #else
    pthread_create(&amy_live_thread, NULL, miniaudio_run, NULL);
    #endif
}


void amy_live_stop() {
    amy_running = 0;
    ma_device_uninit(&device);
    free(leftover_buf);
}
#endif
