#if defined(ARDUINO_ARCH_SPRESENSE)

#include <AMY-Arduino.h>
#include <OutputMixer.h>
#include <MemoryUtil.h>
#include <arch/board/board.h>

#define AMY_PCM_FRAME_SIZE (256)  // Samples per frame (match AMY block size)
#define AMY_BYTEWIDTH (2)         // 16-bit
#define AMY_CHANNELS (2)          // Stereo
#define AMY_READSIZE (AMY_PCM_FRAME_SIZE * AMY_BYTEWIDTH * AMY_CHANNELS)

static OutputMixer *theMixer = nullptr;
static AsPcmDataParam g_pcm_frame;
static volatile bool g_frame_valid = false;

// Forward declaration for callback
extern "C" void spresense_mixer_callback(int32_t identifier, bool is_end);

extern "C" {

static bool spresense_make_silence_frame(AsPcmDataParam *pcm_param) {
    if (pcm_param == nullptr) {
        return false;
    }
    if (pcm_param->mh.allocSeg(S0_REND_PCM_BUF_POOL, AMY_READSIZE) != ERR_OK) {
        return false;
    }

    uint8_t *pcm_buffer = (uint8_t *)pcm_param->mh.getPa();
    memset(pcm_buffer, 0, AMY_READSIZE);

    pcm_param->identifier = 0;
    pcm_param->callback = 0;
    pcm_param->bit_length = 16;
    pcm_param->size = AMY_READSIZE;
    pcm_param->sample = AMY_PCM_FRAME_SIZE;
    pcm_param->is_end = false;
    pcm_param->is_valid = true;
    return true;
}

/**
 * @brief Mixer done callback procedure
 */
static void outputmixer_done_callback(MsgQueId requester_dtq,
                                      MsgType reply_of,
                                      AsOutputMixDoneParam *done_param) {
    // Not used in this implementation
    (void)requester_dtq;
    (void)reply_of;
    (void)done_param;
}

/**
 * @brief Mixer data send callback - called when mixer needs more PCM data
 */
void spresense_mixer_callback(int32_t identifier, bool is_end) {
    (void)identifier;

    if (is_end) {
        return;
    }

    AsPcmDataParam pcm_param;
    bool has_frame = g_frame_valid;
    if (has_frame) {
        pcm_param = g_pcm_frame;
        g_frame_valid = false;
    }
    if (!has_frame) {
        if (!spresense_make_silence_frame(&pcm_param)) {
            return;
        }
    }

    (void)theMixer->sendData(OutputMixer0, spresense_mixer_callback, pcm_param);
}

void spresense_setup_audio(amy_config_t *config) {
    if (theMixer == nullptr && config != nullptr) {
        // Initialize memory pools
        initMemoryPools();
        createStaticPools(MEM_LAYOUT_PLAYER);

        // Get OutputMixer instance
        theMixer = OutputMixer::getInstance();
        theMixer->activateBaseband();

        // Create mixer object with done callback
        theMixer->create(outputmixer_done_callback);

        // Set rendering clock mode (Normal = 48kHz)
        theMixer->setRenderingClkMode(OUTPUTMIXER_RNDCLK_NORMAL);

        // Select output device based on config
        int output_device = (config->spresense_output_device == SPRESENSE_OUTPUT_I2S) 
                           ? I2SOutputDevice 
                           : HPOutputDevice;

        // Activate mixer with selected output device
        theMixer->activate(OutputMixer0, output_device, outputmixer_done_callback);

        // Set output gain to -6 dB (0.1 dB units).
        theMixer->setVolume(-60, -60, -60);

            // Unmute amplifier
            board_external_amp_mute_control(false);

        g_frame_valid = false;

        // Prime callback chain with 3 initial frames.
        int err = OUTPUTMIXER_ECODE_OK;
        for (int i = 0; i < 3; ++i) {
            AsPcmDataParam pcm_param;
            if (!spresense_make_silence_frame(&pcm_param)) {
                err = -1;
                break;
            }

            err = theMixer->sendData(OutputMixer0, spresense_mixer_callback, pcm_param);
            if (err != OUTPUTMIXER_ECODE_OK) {
                break;
            }
        }
        g_frame_valid = false;
    }
}

void spresense_teardown_audio(amy_config_t *config) {
    (void)config;
    if (theMixer != nullptr) {
        g_frame_valid = false;
        AsPcmDataParam pcm_param;
        if (pcm_param.mh.allocSeg(S0_REND_PCM_BUF_POOL, AMY_READSIZE) == ERR_OK) {
            pcm_param.identifier = 0;
            pcm_param.callback = 0;
            pcm_param.bit_length = 16;
            pcm_param.size = 0;
            pcm_param.sample = 0;
            pcm_param.is_end = true;
            pcm_param.is_valid = false;
            theMixer->sendData(OutputMixer0, spresense_mixer_callback, pcm_param);
        }
        theMixer = nullptr;
    }
}

/**
 * @brief Trigger audio stream - initiates mixer callback chain
 */
size_t spresense_audio_write(const uint8_t *buffer, size_t nbytes) {
    // Wait until frame is consumed by callback
    for (;;) {
        if (!g_frame_valid) { break; }
        delayMicroseconds(50);
    }

    AsPcmDataParam pcm_param;
    if (pcm_param.mh.allocSeg(S0_REND_PCM_BUF_POOL, AMY_READSIZE) != ERR_OK) {
        return 0;
    }

    uint8_t *pcm_buffer = (uint8_t *)pcm_param.mh.getPa();
    size_t copy_size = (nbytes < AMY_READSIZE) ? nbytes : AMY_READSIZE;
    memset(pcm_buffer, 0, AMY_READSIZE);
    if (buffer != nullptr && copy_size > 0) {
        memcpy(pcm_buffer, buffer, copy_size);
    }

    pcm_param.identifier = 0;
    pcm_param.callback = 0;
    pcm_param.bit_length = 16;
    pcm_param.size = AMY_READSIZE;
    pcm_param.sample = AMY_PCM_FRAME_SIZE;
    pcm_param.is_end = false;
    pcm_param.is_valid = true;

    g_pcm_frame = pcm_param;
    __asm__ volatile("dmb" ::: "memory");  // Ensure frame data is visible before setting valid flag
    g_frame_valid = true;

    return nbytes;
}

} // extern "C"

#endif
