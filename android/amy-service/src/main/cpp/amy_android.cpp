#include <jni.h>
#include <android/log.h>
#include <oboe/Oboe.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include "amy_android_capture.h"

extern "C" {
#include "amy.h"
#include "amy_unix_socket.h"
}

#define LOG_TAG "AmyAndroid"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/*
 * AMY's generic API always calls these platform hooks. The Android build does
 * not use AMY's miniaudio/I2S platform layer; Oboe owns the output stream and
 * calls amy_simple_fill_buffer() directly.
 */
extern "C" void amy_platform_init(void) {}
extern "C" void amy_platform_deinit(void) {}
extern "C" void amy_update_tasks(void) {}
extern "C" int16_t *amy_render_audio(void) { return nullptr; }
extern "C" size_t amy_i2s_write(const uint8_t *, size_t) { return 0; }

/*
 * AMY_HOST_MIDI makes the embedder own the MIDI device layer. This Android
 * service is controlled by AMY wire messages rather than a MIDI device, so the
 * lifecycle hooks are no-ops. Preserve AMY's optional outgoing MIDI hook even
 * though no platform MIDI port is opened here.
 */
extern "C" void run_midi(void) {}
extern "C" void stop_midi(void) {}
extern "C" void midi_out(uint8_t *bytes, uint16_t len) {
    if (amy_global.config.amy_external_midi_output_hook != nullptr) {
        amy_global.config.amy_external_midi_output_hook(bytes, len);
    }
}

namespace {

constexpr int kMaxCommandsPerBlock = 64;
constexpr int kAudioReadyTimeoutMs = 2000;
constexpr int kAudioReadyPollMs = 2;

class AmyAndroidEngine final : public oboe::AudioStreamDataCallback,
                               public oboe::AudioStreamErrorCallback {
public:
    int start(const char *socketPath) {
        if (socketPath == nullptr || socketPath[0] == '\0') return -EINVAL;
        if (mRunning.load(std::memory_order_acquire)) return -EALREADY;

        amy_config_t config = amy_default_config();
        config.audio = AMY_AUDIO_IS_NONE;
        config.features.audio_in = 0;
        config.features.default_synths = 0;
        config.features.startup_bleep = 0;
        /* Keep AMY rendering entirely on Oboe's realtime callback thread. */
        config.platform.multicore = 0;
        config.platform.multithread = 0;
        /* Physical-string clients can require many simultaneous KS voices. */
        config.ks_oscs = 16;

        amy_start(config);
        mAmyStarted = true;

        // The helper remains dormant unless the hello-world test has created
        // its one-shot private capture marker. It captures the exact samples
        // returned by AMY and the exact I16 samples handed to Oboe.
        mCapture = std::make_unique<AmyAndroidAudioCapture>(
            socketPath, AMY_SAMPLE_RATE, AMY_NCHANS);

        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output);
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        builder.setSharingMode(oboe::SharingMode::Exclusive);
        builder.setFormat(oboe::AudioFormat::I16);
        builder.setChannelCount(AMY_NCHANS);
        builder.setSampleRate(AMY_SAMPLE_RATE);
        builder.setUsage(oboe::Usage::Game);
        builder.setContentType(oboe::ContentType::Music);
        builder.setDataCallback(this);
        builder.setErrorCallback(this);

        oboe::Result result = builder.openStream(mStream);
        if (result != oboe::Result::OK || !mStream) {
            LOGE("Oboe openStream failed: %s", oboe::convertToText(result));
            stopAmy();
            return static_cast<int>(result);
        }

        if (mStream->getFormat() != oboe::AudioFormat::I16 ||
            mStream->getChannelCount() != AMY_NCHANS ||
            mStream->getSampleRate() != AMY_SAMPLE_RATE) {
            LOGE("Unexpected Oboe format: format=%d channels=%d rate=%d",
                 static_cast<int>(mStream->getFormat()),
                 mStream->getChannelCount(),
                 mStream->getSampleRate());
            mStream->close();
            mStream.reset();
            stopAmy();
            return -ERANGE;
        }

        LOGI("Oboe output: deviceId=%d sharing=%d performance=%d usage=%d content=%d framesPerBurst=%d capacity=%d",
             mStream->getDeviceId(),
             static_cast<int>(mStream->getSharingMode()),
             static_cast<int>(mStream->getPerformanceMode()),
             static_cast<int>(mStream->getUsage()),
             static_cast<int>(mStream->getContentType()),
             mStream->getFramesPerBurst(),
             mStream->getBufferCapacityInFrames());

        mBlock = nullptr;
        mBlockFrame = AMY_BLOCK_SIZE;
        mAudioCallbackSeen.store(false, std::memory_order_release);
        mRunning.store(true, std::memory_order_release);

        result = mStream->requestStart();
        if (result != oboe::Result::OK) {
            LOGE("Oboe requestStart failed: %s", oboe::convertToText(result));
            mRunning.store(false, std::memory_order_release);
            mStream->close();
            mStream.reset();
            stopAmy();
            return static_cast<int>(result);
        }

        // Do not publish amy.sock until the realtime audio callback has actually
        // executed. This makes successful socket connect a useful readiness
        // boundary for generic clients, including the first launch after install.
        int waitedMs = 0;
        while (!mAudioCallbackSeen.load(std::memory_order_acquire) &&
               mRunning.load(std::memory_order_acquire) &&
               waitedMs < kAudioReadyTimeoutMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kAudioReadyPollMs));
            waitedMs += kAudioReadyPollMs;
        }

        if (!mAudioCallbackSeen.load(std::memory_order_acquire)) {
            LOGE("Timed out waiting for first Oboe audio callback");
            mRunning.store(false, std::memory_order_release);
            mStream->requestStop();
            mStream->close();
            mStream.reset();
            stopAmy();
            return -ETIMEDOUT;
        }

        if (!mRunning.load(std::memory_order_acquire)) {
            LOGE("Oboe stream stopped before AMY socket became ready");
            mStream->close();
            mStream.reset();
            stopAmy();
            return -EIO;
        }

        amy_unix_socket_server_t *socket = nullptr;
        int socketResult = amy_unix_socket_start(&socket, socketPath);
        if (socketResult != 0) {
            mRunning.store(false, std::memory_order_release);
            mStream->requestStop();
            mStream->close();
            mStream.reset();
            stopAmy();
            return socketResult;
        }
        mSocket.store(socket, std::memory_order_release);

        LOGI("AMY/Oboe started: %d Hz, %d-frame AMY blocks, socket=%s",
             AMY_SAMPLE_RATE, AMY_BLOCK_SIZE, socketPath);
        return 0;
    }

    int32_t outputDeviceId() const {
        return mStream ? mStream->getDeviceId() : -1;
    }

    void stop() {
        mRunning.store(false, std::memory_order_release);

        if (mStream) {
            mStream->requestStop();
            mStream->close();
            mStream.reset();
        }

        // No callback can touch the capture buffers after the stream closes.
        if (mCapture) {
            mCapture->stop();
            mCapture.reset();
        }

        cleanupSocketAndAmy();
        mAudioCallbackSeen.store(false, std::memory_order_release);
        mBlock = nullptr;
        mBlockFrame = AMY_BLOCK_SIZE;
    }

    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream *,
        void *audioData,
        int32_t numFrames) override {
        int16_t *output = static_cast<int16_t *>(audioData);
        if (!mRunning.load(std::memory_order_acquire)) {
            std::memset(output, 0,
                        static_cast<size_t>(numFrames) * AMY_NCHANS * sizeof(int16_t));
            return oboe::DataCallbackResult::Stop;
        }

        mAudioCallbackSeen.store(true, std::memory_order_release);
        if (mCapture && mCapture->enabled()) mCapture->beginCallback(numFrames);

        int32_t outputFrame = 0;
        while (outputFrame < numFrames) {
            if (mBlock == nullptr || mBlockFrame >= AMY_BLOCK_SIZE) {
                drainCommands();
                mBlock = amy_simple_fill_buffer();
                mBlockFrame = 0;
                if (mBlock == nullptr) {
                    std::memset(output + outputFrame * AMY_NCHANS, 0,
                                static_cast<size_t>(numFrames - outputFrame) *
                                    AMY_NCHANS * sizeof(int16_t));
                    break;
                }
            }

            const int32_t available = AMY_BLOCK_SIZE - mBlockFrame;
            const int32_t frames = std::min(available, numFrames - outputFrame);
            if (mCapture && mCapture->enabled()) {
                mCapture->captureAmyChunk(
                    mBlock + mBlockFrame * AMY_NCHANS, frames, outputFrame);
            }
            std::memcpy(
                output + outputFrame * AMY_NCHANS,
                mBlock + mBlockFrame * AMY_NCHANS,
                static_cast<size_t>(frames) * AMY_NCHANS * sizeof(int16_t));
            outputFrame += frames;
            mBlockFrame += frames;
        }

        if (mCapture && mCapture->enabled()) mCapture->finishCallback(output, numFrames);
        return oboe::DataCallbackResult::Continue;
    }

    void onErrorAfterClose(oboe::AudioStream *, oboe::Result error) override {
        mRunning.store(false, std::memory_order_release);
        LOGE("Oboe stream closed after error: %s", oboe::convertToText(error));
        /* Lifecycle owner may restart the service; no work is done on Oboe's error thread. */
    }

private:
    void drainCommands() {
        amy_unix_socket_server_t *socket = mSocket.load(std::memory_order_acquire);
        if (socket == nullptr) return;

        char command[MAX_MESSAGE_LEN];
        for (int count = 0; count < kMaxCommandsPerBlock; ++count) {
            int length = amy_unix_socket_receive(socket, command, sizeof(command));
            if (length <= 0) break;
            amy_add_message(command);
        }
    }

    void stopAmy() {
        if (mAmyStarted) {
            amy_stop();
            mAmyStarted = false;
        }
    }

    void cleanupSocketAndAmy() {
        amy_unix_socket_server_t *socket =
            mSocket.exchange(nullptr, std::memory_order_acq_rel);
        if (socket != nullptr) {
            uint32_t overruns = amy_unix_socket_queue_overruns(socket);
            uint32_t oversize = amy_unix_socket_oversize_packets(socket);
            uint32_t rejected = amy_unix_socket_rejected_peers(socket);
            if (overruns || oversize || rejected) {
                LOGE("AMY socket diagnostics: overruns=%u oversize=%u rejected=%u",
                     overruns, oversize, rejected);
            }
            amy_unix_socket_stop(socket);
        }
        stopAmy();
    }

    std::atomic<bool> mRunning{false};
    std::atomic<bool> mAudioCallbackSeen{false};
    bool mAmyStarted = false;
    std::atomic<amy_unix_socket_server_t *> mSocket{nullptr};
    std::shared_ptr<oboe::AudioStream> mStream;
    std::unique_ptr<AmyAndroidAudioCapture> mCapture;
    int16_t *mBlock = nullptr;
    int32_t mBlockFrame = AMY_BLOCK_SIZE;
};

std::mutex gLifecycleMutex;
std::unique_ptr<AmyAndroidEngine> gEngine;

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_org_amy_audio_AmyService_nativeStart(JNIEnv *env, jclass, jstring socketPath) {
    if (socketPath == nullptr) return -EINVAL;

    const char *path = env->GetStringUTFChars(socketPath, nullptr);
    if (path == nullptr) return -ENOMEM;

    std::lock_guard<std::mutex> guard(gLifecycleMutex);
    if (gEngine) gEngine->stop();
    gEngine = std::make_unique<AmyAndroidEngine>();
    int result = gEngine->start(path);
    if (result != 0) gEngine.reset();

    env->ReleaseStringUTFChars(socketPath, path);
    return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_amy_audio_AmyService_nativeGetOutputDeviceId(JNIEnv *, jclass) {
    std::lock_guard<std::mutex> guard(gLifecycleMutex);
    return gEngine ? gEngine->outputDeviceId() : -1;
}

extern "C" JNIEXPORT void JNICALL
Java_org_amy_audio_AmyService_nativeStop(JNIEnv *, jclass) {
    std::lock_guard<std::mutex> guard(gLifecycleMutex);
    if (gEngine) {
        gEngine->stop();
        gEngine.reset();
    }
}
