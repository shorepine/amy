#include "amy_android_capture.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <unistd.h>

#define LOG_TAG "AmyAudioCapture"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr int32_t kCaptureSeconds = 4;
constexpr const char *kEnableMarker = "amy-audio-capture.enable";
constexpr const char *kAmyWave = "amy-render.wav";
constexpr const char *kOboeWave = "amy-oboe.wav";
constexpr const char *kStatsFile = "amy-audio-levels.txt";

std::string joinPath(const std::string &directory, const char *name) {
    return directory + "/" + name;
}

void writeLe16(FILE *file, uint16_t value) {
    const uint8_t bytes[2] = {
        static_cast<uint8_t>(value & 0xff),
        static_cast<uint8_t>((value >> 8) & 0xff),
    };
    std::fwrite(bytes, sizeof(bytes), 1, file);
}

void writeLe32(FILE *file, uint32_t value) {
    const uint8_t bytes[4] = {
        static_cast<uint8_t>(value & 0xff),
        static_cast<uint8_t>((value >> 8) & 0xff),
        static_cast<uint8_t>((value >> 16) & 0xff),
        static_cast<uint8_t>((value >> 24) & 0xff),
    };
    std::fwrite(bytes, sizeof(bytes), 1, file);
}

bool writeWave(const std::string &path,
               const std::vector<int16_t> &samples,
               int32_t frames,
               int32_t sampleRate,
               int32_t channels) {
    FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;

    const uint32_t sampleCount = static_cast<uint32_t>(frames * channels);
    const uint32_t dataBytes = sampleCount * sizeof(int16_t);
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate * channels * sizeof(int16_t));
    const uint16_t blockAlign = static_cast<uint16_t>(channels * sizeof(int16_t));

    std::fwrite("RIFF", 4, 1, file);
    writeLe32(file, 36u + dataBytes);
    std::fwrite("WAVE", 4, 1, file);
    std::fwrite("fmt ", 4, 1, file);
    writeLe32(file, 16);
    writeLe16(file, 1);  // PCM
    writeLe16(file, static_cast<uint16_t>(channels));
    writeLe32(file, static_cast<uint32_t>(sampleRate));
    writeLe32(file, byteRate);
    writeLe16(file, blockAlign);
    writeLe16(file, 16);
    std::fwrite("data", 4, 1, file);
    writeLe32(file, dataBytes);
    std::fwrite(samples.data(), sizeof(int16_t), sampleCount, file);

    const bool ok = std::fclose(file) == 0;
    return ok;
}

struct LevelStats {
    int32_t peak = 0;
    double rms = 0.0;
    double peakDbfs = -200.0;
    double rmsDbfs = -200.0;
};

LevelStats levelStats(const std::vector<int16_t> &samples, int32_t sampleCount) {
    LevelStats result;
    if (sampleCount <= 0) return result;

    long double sumSquares = 0.0;
    for (int32_t i = 0; i < sampleCount; ++i) {
        const int32_t value = samples[static_cast<size_t>(i)];
        const int32_t magnitude = value == -32768 ? 32768 : std::abs(value);
        result.peak = std::max(result.peak, magnitude);
        const long double sample = static_cast<long double>(value);
        sumSquares += sample * sample;
    }

    result.rms = std::sqrt(static_cast<double>(sumSquares / sampleCount));
    if (result.peak > 0) {
        result.peakDbfs = 20.0 * std::log10(static_cast<double>(result.peak) / 32768.0);
    }
    if (result.rms > 0.0) {
        result.rmsDbfs = 20.0 * std::log10(result.rms / 32768.0);
    }
    return result;
}

}  // namespace

AmyAndroidAudioCapture::AmyAndroidAudioCapture(
    const char *socketPath, int32_t sampleRate, int32_t channels)
    : mSampleRate(sampleRate), mChannels(channels) {
    if (socketPath == nullptr || sampleRate <= 0 || channels <= 0) return;

    std::string path(socketPath);
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return;
    mDirectory = path.substr(0, slash);

    const std::string marker = joinPath(mDirectory, kEnableMarker);
    if (access(marker.c_str(), F_OK) != 0) return;

    // The marker is one-shot. The hello-world app recreates it for each clean
    // launch; ordinary users of the AAR never pay the capture cost.
    unlink(marker.c_str());
    unlink(joinPath(mDirectory, kAmyWave).c_str());
    unlink(joinPath(mDirectory, kOboeWave).c_str());
    unlink(joinPath(mDirectory, kStatsFile).c_str());

    mTargetFrames = sampleRate * kCaptureSeconds;
    const size_t sampleCount = static_cast<size_t>(mTargetFrames) * channels;
    try {
        mAmySamples.resize(sampleCount);
        mOboeSamples.resize(sampleCount);
    } catch (...) {
        LOGE("Unable to allocate Android audio capture buffers");
        mAmySamples.clear();
        mOboeSamples.clear();
        return;
    }

    mEnabled = true;
    mWriter = std::thread(&AmyAndroidAudioCapture::writerLoop, this);
    LOGI("Audio capture armed: %d frames, %d Hz, %d channels",
         mTargetFrames, mSampleRate, mChannels);
}

AmyAndroidAudioCapture::~AmyAndroidAudioCapture() {
    stop();
}

void AmyAndroidAudioCapture::beginCallback(int32_t numFrames) {
    if (!mEnabled || mWriterReady.load(std::memory_order_acquire) || numFrames <= 0) {
        mCallbackFrames = 0;
        return;
    }

    const int32_t remaining = mTargetFrames - mFramesCaptured;
    mCallbackStartFrame = mFramesCaptured;
    mCallbackFrames = std::min(numFrames, std::max(remaining, 0));
}

void AmyAndroidAudioCapture::captureAmyChunk(
    const int16_t *samples, int32_t frames, int32_t outputFrame) {
    if (!mEnabled || samples == nullptr || frames <= 0 || mCallbackFrames <= 0) return;
    if (outputFrame < 0 || outputFrame >= mCallbackFrames) return;

    const int32_t copyFrames = std::min(frames, mCallbackFrames - outputFrame);
    const size_t destinationSample =
        static_cast<size_t>(mCallbackStartFrame + outputFrame) * mChannels;
    const size_t sampleCount = static_cast<size_t>(copyFrames) * mChannels;
    std::memcpy(mAmySamples.data() + destinationSample,
                samples,
                sampleCount * sizeof(int16_t));
}

void AmyAndroidAudioCapture::finishCallback(
    const int16_t *oboeOutput, int32_t numFrames) {
    if (!mEnabled || oboeOutput == nullptr || numFrames <= 0 || mCallbackFrames <= 0) return;

    const int32_t copyFrames = std::min(numFrames, mCallbackFrames);
    const size_t destinationSample = static_cast<size_t>(mCallbackStartFrame) * mChannels;
    const size_t sampleCount = static_cast<size_t>(copyFrames) * mChannels;
    std::memcpy(mOboeSamples.data() + destinationSample,
                oboeOutput,
                sampleCount * sizeof(int16_t));

    mFramesCaptured += copyFrames;
    mCallbackFrames = 0;

    if (mFramesCaptured >= mTargetFrames) {
        mWriterReady.store(true, std::memory_order_release);
        mWriterCv.notify_one();
    }
}

void AmyAndroidAudioCapture::stop() {
    if (!mEnabled || mStopped) return;
    mStopped = true;

    {
        std::lock_guard<std::mutex> lock(mWriterMutex);
        if (mFramesCaptured > 0) {
            mWriterReady.store(true, std::memory_order_release);
        }
        mWriterStop = true;
    }
    mWriterCv.notify_one();
    if (mWriter.joinable()) mWriter.join();
}

void AmyAndroidAudioCapture::writerLoop() {
    std::unique_lock<std::mutex> lock(mWriterMutex);
    mWriterCv.wait(lock, [this] {
        return mWriterReady.load(std::memory_order_acquire) || mWriterStop;
    });
    const bool shouldWrite =
        mWriterReady.load(std::memory_order_acquire) && mFramesCaptured > 0;
    lock.unlock();

    if (shouldWrite) writeCaptureFiles();
}

void AmyAndroidAudioCapture::writeCaptureFiles() {
    const int32_t frames = std::min(mFramesCaptured, mTargetFrames);
    const int32_t sampleCount = frames * mChannels;
    if (frames <= 0 || sampleCount <= 0) return;

    const std::string amyPath = joinPath(mDirectory, kAmyWave);
    const std::string oboePath = joinPath(mDirectory, kOboeWave);
    const std::string statsPath = joinPath(mDirectory, kStatsFile);

    const bool amyOk = writeWave(amyPath, mAmySamples, frames, mSampleRate, mChannels);
    const bool oboeOk = writeWave(oboePath, mOboeSamples, frames, mSampleRate, mChannels);

    const LevelStats amy = levelStats(mAmySamples, sampleCount);
    const LevelStats oboe = levelStats(mOboeSamples, sampleCount);

    int32_t maxAbsDiff = 0;
    int32_t mismatchSamples = 0;
    for (int32_t i = 0; i < sampleCount; ++i) {
        const int32_t a = mAmySamples[static_cast<size_t>(i)];
        const int32_t b = mOboeSamples[static_cast<size_t>(i)];
        const int32_t difference = std::abs(a - b);
        maxAbsDiff = std::max(maxAbsDiff, difference);
        if (difference != 0) ++mismatchSamples;
    }

    FILE *stats = std::fopen(statsPath.c_str(), "w");
    if (stats != nullptr) {
        std::fprintf(stats, "sample_rate=%d\n", mSampleRate);
        std::fprintf(stats, "channels=%d\n", mChannels);
        std::fprintf(stats, "frames=%d\n", frames);
        std::fprintf(stats, "amy_peak=%d\n", amy.peak);
        std::fprintf(stats, "amy_peak_dbfs=%.3f\n", amy.peakDbfs);
        std::fprintf(stats, "amy_rms=%.3f\n", amy.rms);
        std::fprintf(stats, "amy_rms_dbfs=%.3f\n", amy.rmsDbfs);
        std::fprintf(stats, "oboe_peak=%d\n", oboe.peak);
        std::fprintf(stats, "oboe_peak_dbfs=%.3f\n", oboe.peakDbfs);
        std::fprintf(stats, "oboe_rms=%.3f\n", oboe.rms);
        std::fprintf(stats, "oboe_rms_dbfs=%.3f\n", oboe.rmsDbfs);
        std::fprintf(stats, "max_abs_diff=%d\n", maxAbsDiff);
        std::fprintf(stats, "mismatch_samples=%d\n", mismatchSamples);
        std::fclose(stats);
    }

    if (!amyOk || !oboeOk || stats == nullptr) {
        LOGE("Audio capture write failed: amy=%d oboe=%d stats=%d",
             amyOk, oboeOk, stats != nullptr);
        return;
    }

    LOGI("Audio capture complete: frames=%d AMY peak=%d (%.2f dBFS) RMS=%.1f (%.2f dBFS); Oboe peak=%d (%.2f dBFS) RMS=%.1f (%.2f dBFS); mismatches=%d maxdiff=%d",
         frames,
         amy.peak, amy.peakDbfs, amy.rms, amy.rmsDbfs,
         oboe.peak, oboe.peakDbfs, oboe.rms, oboe.rmsDbfs,
         mismatchSamples, maxAbsDiff);
}
