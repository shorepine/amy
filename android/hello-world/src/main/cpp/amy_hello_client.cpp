#include <jni.h>
#include <android/log.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define LOG_TAG "AmyHelloWorld"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

int connect_with_retry(const char *path) {
    if (path == nullptr || path[0] == '\0') return -EINVAL;

    sockaddr_un addr{};
    if (std::strlen(path) >= sizeof(addr.sun_path)) return -ENAMETOOLONG;
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    for (int attempt = 0; attempt < 100; ++attempt) {
        int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (fd < 0) return -errno;

        if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
            return fd;
        }

        int saved = errno;
        close(fd);
        if (saved != ENOENT && saved != ECONNREFUSED) return -saved;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return -ETIMEDOUT;
}

int send_wire(int fd, const char *wire) {
    size_t len = std::strlen(wire);
    ssize_t sent = send(fd, wire, len, MSG_NOSIGNAL);
    if (sent < 0) return -errno;
    if (static_cast<size_t>(sent) != len) return -EIO;
    LOGI("wire: %s", wire);
    return 0;
}

int play_c_scale(const char *path) {
    int fd = connect_with_retry(path);
    if (fd < 0) return fd;

    // Raw oscillator 0, sine wave. AMY's V control is a 0..10 bus/master
    // volume scale; the final mixer multiplies V by 0.1. Use V10.0 so this
    // audible hello-world exercises the full AMY output level.
    // Every packet is an ordinary AMY wire command sent through amy.sock.
    int rc = send_wire(fd, "v0w0V10.0Z");
    if (rc < 0) {
        close(fd);
        return rc;
    }

    // On a completely fresh AMY instance, commit oscillator setup before the
    // first note-on instead of allowing both commands into the same first drain.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    static constexpr int notes[] = {60, 62, 64, 65, 67, 69, 71, 72};
    char wire[64];

    for (int note : notes) {
        std::snprintf(wire, sizeof(wire), "v0n%dl1Z", note);
        rc = send_wire(fd, wire);
        if (rc < 0) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(350));

        rc = send_wire(fd, "v0l0Z");
        if (rc < 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    close(fd);
    if (rc == 0) LOGI("C scale complete");
    return rc;
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_org_amy_hello_MainActivity_nativePlayCScale(JNIEnv *env, jclass, jstring socketPath) {
    if (socketPath == nullptr) return -EINVAL;
    const char *path = env->GetStringUTFChars(socketPath, nullptr);
    if (path == nullptr) return -ENOMEM;
    int rc = play_c_scale(path);
    env->ReleaseStringUTFChars(socketPath, path);
    if (rc < 0) LOGE("C scale failed: %d", rc);
    return rc;
}
