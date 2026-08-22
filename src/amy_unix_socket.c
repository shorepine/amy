#define _GNU_SOURCE

#include "amy_unix_socket.h"

#if defined(__linux__) || defined(__ANDROID__)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define AMY_UNIX_SOCKET_POLL_MS 50

struct amy_unix_socket_packet {
    uint16_t len;
    char data[MAX_MESSAGE_LEN];
};

struct amy_unix_socket_server {
    int listen_fd;
    int client_fd;
    pthread_t thread;
    pthread_mutex_t client_lock;
    bool thread_started;
    volatile uint32_t running;

    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];

    struct amy_unix_socket_packet queue[AMY_UNIX_SOCKET_QUEUE_CAPACITY];
    volatile uint32_t write_index;
    volatile uint32_t read_index;

    volatile uint32_t queue_overruns;
    volatile uint32_t oversize_packets;
    volatile uint32_t rejected_peers;
};

static uint32_t load_u32(const volatile uint32_t *value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void store_u32(volatile uint32_t *value, uint32_t new_value) {
    __atomic_store_n(value, new_value, __ATOMIC_RELEASE);
}

static void increment_u32(volatile uint32_t *value) {
    __atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

static int set_nonblocking_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -errno;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -errno;

    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) return -errno;
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) return -errno;
    return 0;
}

static int remove_owned_stale_socket(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        return errno == ENOENT ? 0 : -errno;
    }

    if (!S_ISSOCK(st.st_mode)) return -EEXIST;
    if (st.st_uid != geteuid()) return -EPERM;
    if (unlink(path) < 0) return -errno;
    return 0;
}

static bool peer_has_same_uid(int fd) {
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        return false;
    }
    return cred.uid == geteuid();
}

static void close_client_locked(amy_unix_socket_server_t *server) {
    if (server->client_fd >= 0) {
        shutdown(server->client_fd, SHUT_RDWR);
        close(server->client_fd);
        server->client_fd = -1;
    }
}

static void close_client(amy_unix_socket_server_t *server) {
    pthread_mutex_lock(&server->client_lock);
    close_client_locked(server);
    pthread_mutex_unlock(&server->client_lock);
}

static void queue_packet(amy_unix_socket_server_t *server,
                         const char *data,
                         size_t len) {
    if (len == 0) return;
    if (len > AMY_UNIX_SOCKET_MAX_PACKET) {
        increment_u32(&server->oversize_packets);
        return;
    }

    uint32_t write_index = load_u32(&server->write_index);
    uint32_t read_index = load_u32(&server->read_index);
    if ((uint32_t)(write_index - read_index) >=
        AMY_UNIX_SOCKET_QUEUE_CAPACITY) {
        increment_u32(&server->queue_overruns);
        return;
    }

    struct amy_unix_socket_packet *slot =
        &server->queue[write_index % AMY_UNIX_SOCKET_QUEUE_CAPACITY];
    memcpy(slot->data, data, len);
    slot->data[len] = '\0';
    slot->len = (uint16_t)len;

    store_u32(&server->write_index, write_index + 1u);
}

static void receive_client_packets(amy_unix_socket_server_t *server,
                                   int client_fd) {
    for (;;) {
        char packet[MAX_MESSAGE_LEN];
        ssize_t received = recv(client_fd,
                                packet,
                                sizeof(packet),
                                MSG_DONTWAIT | MSG_TRUNC);
        if (received > 0) {
            if ((size_t)received > AMY_UNIX_SOCKET_MAX_PACKET) {
                increment_u32(&server->oversize_packets);
            } else {
                queue_packet(server, packet, (size_t)received);
            }
            continue;
        }

        if (received == 0) {
            close_client(server);
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        if (errno == EINTR) continue;

        close_client(server);
        return;
    }
}

static void accept_clients(amy_unix_socket_server_t *server) {
    for (;;) {
        int fd = accept(server->listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            return;
        }

        if (set_nonblocking_cloexec(fd) < 0 || !peer_has_same_uid(fd)) {
            increment_u32(&server->rejected_peers);
            close(fd);
            continue;
        }

        pthread_mutex_lock(&server->client_lock);
        if (server->client_fd >= 0) {
            increment_u32(&server->rejected_peers);
            close(fd);
        } else {
            server->client_fd = fd;
        }
        pthread_mutex_unlock(&server->client_lock);
    }
}

static int current_client_fd(amy_unix_socket_server_t *server) {
    int fd;
    pthread_mutex_lock(&server->client_lock);
    fd = server->client_fd;
    pthread_mutex_unlock(&server->client_lock);
    return fd;
}

static void *socket_thread(void *arg) {
    amy_unix_socket_server_t *server =
        (amy_unix_socket_server_t *)arg;

    while (load_u32(&server->running)) {
        struct pollfd fds[2];
        nfds_t count = 1;

        fds[0].fd = server->listen_fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        int client_fd = current_client_fd(server);
        if (client_fd >= 0) {
            fds[1].fd = client_fd;
            fds[1].events = POLLIN;
            fds[1].revents = 0;
            count = 2;
        }

        int ready = poll(fds, count, AMY_UNIX_SOCKET_POLL_MS);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;

        if (fds[0].revents & POLLIN) accept_clients(server);

        if (count == 2) {
            if (fds[1].revents & POLLIN) {
                receive_client_packets(server, client_fd);
            }
            if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close_client(server);
            }
        }
    }

    close_client(server);
    return NULL;
}

int amy_unix_socket_start(amy_unix_socket_server_t **out_server,
                          const char *path) {
    if (out_server == NULL || path == NULL || path[0] == '\0') return -EINVAL;
    *out_server = NULL;

    size_t path_len = strlen(path);
    if (path_len >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        return -ENAMETOOLONG;
    }

    int rc = remove_owned_stale_socket(path);
    if (rc < 0) return rc;

    amy_unix_socket_server_t *server = calloc(1, sizeof(*server));
    if (server == NULL) return -ENOMEM;

    server->listen_fd = -1;
    server->client_fd = -1;
    memcpy(server->path, path, path_len + 1u);

    int mutex_rc = pthread_mutex_init(&server->client_lock, NULL);
    if (mutex_rc != 0) {
        free(server);
        return -mutex_rc;
    }

    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        rc = -errno;
        goto fail;
    }
    server->listen_fd = fd;

    rc = set_nonblocking_cloexec(fd);
    if (rc < 0) goto fail;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, path_len + 1u);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        rc = -errno;
        goto fail;
    }

    // The Android app-data parent directory is already sandboxed. Mode 0600
    // additionally makes filesystem pathname access same-UID only.
    if (chmod(path, S_IRUSR | S_IWUSR) < 0) {
        rc = -errno;
        goto fail;
    }

    if (listen(fd, 1) < 0) {
        rc = -errno;
        goto fail;
    }

    store_u32(&server->running, 1u);
    int thread_rc = pthread_create(&server->thread, NULL,
                                   socket_thread, server);
    if (thread_rc != 0) {
        rc = -thread_rc;
        store_u32(&server->running, 0u);
        goto fail;
    }
    server->thread_started = true;

    *out_server = server;
    return 0;

fail:
    if (server->listen_fd >= 0) close(server->listen_fd);
    if (server->path[0] != '\0') unlink(server->path);
    pthread_mutex_destroy(&server->client_lock);
    free(server);
    return rc;
}

void amy_unix_socket_stop(amy_unix_socket_server_t *server) {
    if (server == NULL) return;

    store_u32(&server->running, 0u);
    if (server->thread_started) {
        pthread_join(server->thread, NULL);
    }

    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }

    if (server->path[0] != '\0') unlink(server->path);
    pthread_mutex_destroy(&server->client_lock);
    free(server);
}

int amy_unix_socket_receive(amy_unix_socket_server_t *server,
                            char *out,
                            size_t out_len) {
    if (server == NULL || out == NULL) return -EINVAL;

    uint32_t read_index = load_u32(&server->read_index);
    uint32_t write_index = load_u32(&server->write_index);
    if (read_index == write_index) return 0;

    const struct amy_unix_socket_packet *slot =
        &server->queue[read_index % AMY_UNIX_SOCKET_QUEUE_CAPACITY];
    size_t len = slot->len;
    if (out_len <= len) return -EMSGSIZE;

    memcpy(out, slot->data, len);
    out[len] = '\0';
    store_u32(&server->read_index, read_index + 1u);
    return (int)len;
}

int amy_unix_socket_send(amy_unix_socket_server_t *server,
                         const void *data,
                         size_t len) {
    if (server == NULL || (data == NULL && len != 0)) return -EINVAL;
    if (len > AMY_UNIX_SOCKET_MAX_PACKET) return -EMSGSIZE;

    pthread_mutex_lock(&server->client_lock);
    int fd = server->client_fd;
    if (fd < 0) {
        pthread_mutex_unlock(&server->client_lock);
        return -ENOTCONN;
    }

    ssize_t sent = send(fd, data, len,
                        MSG_DONTWAIT | MSG_NOSIGNAL);
    int saved_errno = errno;
    pthread_mutex_unlock(&server->client_lock);

    if (sent < 0) return -saved_errno;
    return (int)sent;
}

uint32_t amy_unix_socket_queue_overruns(
    const amy_unix_socket_server_t *server) {
    return server == NULL ? 0u : load_u32(&server->queue_overruns);
}

uint32_t amy_unix_socket_oversize_packets(
    const amy_unix_socket_server_t *server) {
    return server == NULL ? 0u : load_u32(&server->oversize_packets);
}

uint32_t amy_unix_socket_rejected_peers(
    const amy_unix_socket_server_t *server) {
    return server == NULL ? 0u : load_u32(&server->rejected_peers);
}

#else

#include <errno.h>

int amy_unix_socket_start(amy_unix_socket_server_t **out_server,
                          const char *path) {
    (void)out_server;
    (void)path;
    return -ENOTSUP;
}

void amy_unix_socket_stop(amy_unix_socket_server_t *server) {
    (void)server;
}

int amy_unix_socket_receive(amy_unix_socket_server_t *server,
                            char *out,
                            size_t out_len) {
    (void)server;
    (void)out;
    (void)out_len;
    return -ENOTSUP;
}

int amy_unix_socket_send(amy_unix_socket_server_t *server,
                         const void *data,
                         size_t len) {
    (void)server;
    (void)data;
    (void)len;
    return -ENOTSUP;
}

uint32_t amy_unix_socket_queue_overruns(
    const amy_unix_socket_server_t *server) {
    (void)server;
    return 0u;
}

uint32_t amy_unix_socket_oversize_packets(
    const amy_unix_socket_server_t *server) {
    (void)server;
    return 0u;
}

uint32_t amy_unix_socket_rejected_peers(
    const amy_unix_socket_server_t *server) {
    (void)server;
    return 0u;
}

#endif
