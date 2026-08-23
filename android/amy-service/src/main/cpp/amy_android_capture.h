#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AmyAndroidAudioCapture {
public:
    AmyAndroidAudioCapture(const char *socketPath, int32_t sampleRate, int32_t channels);
    ~AmyAndroidAudioCapture();

    bool enabled() const { return mEnabled; }

    // Called only from Oboe's realtime callback. These methods allocate no
    // memory, perform no file I/O, and never take the writer mutex.
    void beginCallback(int32_t numFrames);
    void captureAmyChunk(const int16_t *samples, int32_t frames, int32_t outputFrame);
    void finishCallback(const int16_t *oboeOutput, int32_t numFrames);

    // Called after the Oboe stream has stopped. A partial capture is still
    // written, which makes diagnostics useful even on early shutdown/error.
    void stop();

private:
    void writerLoop();
    void writeCaptureFiles();

    bool mEnabled = false;
    bool mStopped = false;
    int32_t mSampleRate = 0;
    int32_t mChannels = 0;
    int32_t mTargetFrames = 0;
    int32_t mFramesCaptured = 0;
    int32_t mCallbackStartFrame = 0;
    int32_t mCallbackFrames = 0;

    std::string mDirectory;
    std::vector<int16_t> mAmySamples;
    std::vector<int16_t> mOboeSamples;

    std::mutex mWriterMutex;
    std::condition_variable mWriterCv;
    std::atomic<bool> mWriterReady{false};
    bool mWriterStop = false;
    std::thread mWriter;
};
