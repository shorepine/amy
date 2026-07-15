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
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define DEVICE_FORMAT       ma_format_s16

int16_t * leftover_buf;
uint16_t leftover_samples = 0;
//pthread_t amy_live_thread;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
float amy_web_cv_1 = 0;
float amy_web_cv_2 = 0;
void main_loop__em()
{
    // Ticks are counted in the render loop (AudioWorklet thread); replay them
    // to the page's amy_sequencer_js_hook from the main thread here.
    sequencer_check_and_call_js_hook();
    // Same story for outgoing MIDI realtime clock: the worklet queues the
    // bytes, and only the main thread can reach the page's midiOutputDevice.
    midi_clock_out_flush();
    amy_web_cv_1 = EM_ASM_DOUBLE({ 
        if(typeof cv_1_voltage != 'undefined') { return cv_1_voltage; } else { return 0; }
    });
    amy_web_cv_2 = EM_ASM_DOUBLE({ 
        if(typeof cv_2_voltage != 'undefined') { return cv_2_voltage; } else { return 0; }
    });
}
#endif

// Semaphore for passing most recent audio buffer to amy_update.
// It's the pointer that's volatile, not the data it points to.
int16_t *volatile last_audio_buffer = NULL;

void amy_update_tasks() {
}

void amy_platform_init() {
}

void amy_platform_deinit() {
}

size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) {
    return 0;
}

int16_t *amy_render_audio() {
    // For miniaudio, we just return a semaphore buffer.
    while (last_audio_buffer == NULL)
        ;
    int16_t *buf = last_audio_buffer;
    last_audio_buffer = NULL;
    return buf;
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


// I've seen frame counts as big as 1440. *16 leaves room for the larger
// Windows ring lead (see miniaudio_init) needed when the device period isn't
// a multiple of AMY_BLOCK_SIZE.
#define OUTPUT_RING_FRAMES (AMY_BLOCK_SIZE*16)
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
        if (peek != NULL) {
            for(uint8_t c=0;c<AMY_NCHANS;c++) {
                amy_in_block[in_ptr++] = peek[AMY_NCHANS * frame + c];
            }
        } else {
            // no audio in, so fill it with 0s
            for(uint8_t c=0;c<AMY_NCHANS;c++) {
                amy_in_block[in_ptr++] = 0;
            }
        }
        if(in_ptr == (AMY_BLOCK_SIZE*AMY_NCHANS)) { // we have a block of input ready
            // render and copy into output ring buffer
            int16_t * buf = amy_simple_fill_buffer();
            // Maybe pass to amy_update.
            last_audio_buffer = buf;
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

    //fprintf(stderr, "miniaudio_init: has_audio_in %d playback_id %d capture_id %d\n",
    //        AMY_HAS_AUDIO_IN, amy_global.config.playback_device_id, amy_global.config.capture_device_id);

#ifdef _WIN32
    // Experiment: prefer DirectSound over WASAPI. The stream itself measures
    // clean (no ring starvation, callbacks well under budget) yet output
    // crackles on some devices/VMs — try the other Windows device path.
    ma_backend win_backends[] = { ma_backend_dsound, ma_backend_wasapi, ma_backend_winmm };
    if (ma_context_init(win_backends, 3, NULL, &context) != MA_SUCCESS) {
#else
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
#endif
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
#ifdef _WIN32
    // WASAPI in shared mode (esp. under a VM / emulation) glitches with a
    // single tiny 256-frame period. Let WASAPI align to its own quantum with
    // a generous ms-based buffer split into several periods instead of
    // forcing a frame count.
    deviceConfig.periodSizeInMilliseconds = 20;
    deviceConfig.periods = 4;
#else
    deviceConfig.periodSizeInFrames = AMY_BLOCK_SIZE;
#endif
    
    if (ma_device_init(&context, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Failed to open playback device.\n");
        exit(1);
    }

#ifdef _WIN32
    // The device period (e.g. 882 or 1323 frames) is usually NOT a multiple
    // of AMY_BLOCK_SIZE (256). The output ring writes in 256-frame bursts but
    // the device reads `period` frames per callback, so the read/write gap
    // oscillates by ~one block around the initial lead; the stock 1-block
    // lead dips to zero -> underrun crackle. Lead by enough blocks to cover
    // the negotiated period plus margin. Must be set BEFORE the device starts
    // or the first callbacks run with the short lead and crackle at startup.
    // (mac/linux force period==256, so the gap is constant there and the
    // 1-block default is fine.)
    {
        uint32_t p = device.playback.internalPeriodSizeInFrames;
        uint32_t lead_blocks = (p + AMY_BLOCK_SIZE - 1) / AMY_BLOCK_SIZE + 2;
        uint32_t lead = lead_blocks * AMY_BLOCK_SIZE * AMY_NCHANS;
        if (lead >= OUTPUT_RING_LENGTH) lead = AMY_BLOCK_SIZE * 3 * AMY_NCHANS;
        ring_write_ptr = (uint16_t)lead;
    }
#endif
    // Zero the ring before the device starts pulling from it.
    for(uint16_t i=0;i<OUTPUT_RING_LENGTH;i++) output_ring[i] = 0;

    if (ma_device_start(&device) != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&device);
        exit(1);
    }
    return AMY_OK;
}

void miniaudio_deinit(void) {
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    free(leftover_buf);
}


#ifdef _WIN32
static HANDLE amy_live_thread_handle = NULL;

static DWORD WINAPI miniaudio_run(LPVOID lpParam) {
    (void)lpParam;
    while(amy_global.running) {
        Sleep(1000);
    }
    return 0;
}

void miniaudio_start(void) {
    miniaudio_init();
    amy_global.running = 1;
    amy_live_thread_handle = CreateThread(NULL, 0, miniaudio_run, NULL, 0, NULL);
}

void miniaudio_stop(void) {
    amy_global.running = 0;
    if (amy_live_thread_handle) {
        WaitForSingleObject(amy_live_thread_handle, 2000);
        CloseHandle(amy_live_thread_handle);
        amy_live_thread_handle = NULL;
    }
    miniaudio_deinit();
}
#else
void *miniaudio_run(void *vargp) {
    while(amy_global.running) {
        sleep(1);
    }
    return NULL;
}

void miniaudio_start(void) {
    miniaudio_init();
    amy_global.running = 1;
    pthread_t amy_live_thread;
    pthread_create(&amy_live_thread, NULL, miniaudio_run, NULL);
}

void miniaudio_stop(void) {
    amy_global.running = 0;
    miniaudio_deinit();
}
#endif


#ifdef __EMSCRIPTEN__
void amy_live_start_web_audioin() {
    amy_global.config.features.audio_in = 1;
    emscripten_cancel_main_loop();
    miniaudio_start();
    emscripten_set_main_loop(main_loop__em, 0, 0);
}
void amy_live_start_web() {
    amy_global.config.features.audio_in = 0;
    emscripten_cancel_main_loop();
    miniaudio_start();
    emscripten_set_main_loop(main_loop__em, 0, 0);
}
void amy_live_stop() {
    amy_global.running = 0;
    emscripten_cancel_main_loop();
    miniaudio_deinit();
}
#endif  // __EMSCRIPTEN__

#endif  // Not ESP_PLATFORM or PICO_ON_DEVICE or ARDUINO
