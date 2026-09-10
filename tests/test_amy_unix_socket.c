#define _GNU_SOURCE

#include "amy_unix_socket.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define WAIT_STEPS 2000
#define WAIT_US 1000

typedef uint32_t (*counter_fn)(const amy_unix_socket_server_t *server);

static void socket_address(struct sockaddr_un *addr, const char *path) {
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    assert(strlen(path) < sizeof(addr->sun_path));
    strcpy(addr->sun_path, path);
}

static int connect_client(const char *path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    assert(fd >= 0);

    struct sockaddr_un addr;
    socket_address(&addr, path);
    assert(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    return fd;
}

static int wait_receive(amy_unix_socket_server_t *server,
                        char *buffer,
                        size_t buffer_len) {
    for (int i = 0; i < WAIT_STEPS; ++i) {
        int rc = amy_unix_socket_receive(server, buffer, buffer_len);
        if (rc != 0) return rc;
        usleep(WAIT_US);
    }
    return -ETIMEDOUT;
}

static ssize_t wait_client_receive(int fd, void *buffer, size_t len) {
    for (int i = 0; i < WAIT_STEPS; ++i) {
        ssize_t rc = recv(fd, buffer, len, MSG_DONTWAIT);
        if (rc >= 0) return rc;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return -1;
        }
        usleep(WAIT_US);
    }
    errno = ETIMEDOUT;
    return -1;
}

static void wait_counter(counter_fn counter,
                         amy_unix_socket_server_t *server,
                         uint32_t expected) {
    for (int i = 0; i < WAIT_STEPS; ++i) {
        if (counter(server) >= expected) return;
        usleep(WAIT_US);
    }
    assert(counter(server) >= expected);
}

static void wait_until_disconnected(amy_unix_socket_server_t *server) {
    const char probe[] = "x";
    for (int i = 0; i < WAIT_STEPS; ++i) {
        int rc = amy_unix_socket_send(server, probe, sizeof(probe) - 1u);
        if (rc == -ENOTCONN) return;
        assert(rc == (int)(sizeof(probe) - 1u) || rc == -EPIPE ||
               rc == -ECONNRESET);
        usleep(WAIT_US);
    }
    assert(amy_unix_socket_send(server, probe, sizeof(probe) - 1u) ==
           -ENOTCONN);
}

static void make_temp_path(char *dir_template,
                           char *path,
                           size_t path_len) {
    char *dir = mkdtemp(dir_template);
    assert(dir != NULL);
    assert(chmod(dir, 0700) == 0);
    int written = snprintf(path, path_len, "%s/amy.sock", dir);
    assert(written > 0 && (size_t)written < path_len);
}

static void remove_temp_dir(const char *path) {
    char dir[256];
    size_t len = strlen(path);
    assert(len < sizeof(dir));
    memcpy(dir, path, len + 1u);
    char *slash = strrchr(dir, '/');
    assert(slash != NULL);
    *slash = '\0';
    assert(rmdir(dir) == 0);
}

static void send_packet(int fd, const void *data, size_t len) {
    assert(send(fd, data, len, MSG_NOSIGNAL) == (ssize_t)len);
}

static void test_invalid_arguments(void) {
    char output[MAX_MESSAGE_LEN];
    amy_unix_socket_server_t *server = NULL;

    assert(amy_unix_socket_start(NULL, "/tmp/unused.sock") == -EINVAL);
    assert(amy_unix_socket_start(&server, NULL) == -EINVAL);
    assert(amy_unix_socket_start(&server, "") == -EINVAL);

    char long_path[512];
    memset(long_path, 'x', sizeof(long_path));
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1u] = '\0';
    assert(amy_unix_socket_start(&server, long_path) == -ENAMETOOLONG);
    assert(server == NULL);

    assert(amy_unix_socket_receive(NULL, output, sizeof(output)) == -EINVAL);
    assert(amy_unix_socket_receive(NULL, NULL, 0) == -EINVAL);
    assert(amy_unix_socket_send(NULL, "x", 1) == -EINVAL);
    assert(amy_unix_socket_send(NULL, NULL, 0) == -EINVAL);
    assert(amy_unix_socket_queue_overruns(NULL) == 0);
    assert(amy_unix_socket_oversize_packets(NULL) == 0);
    assert(amy_unix_socket_rejected_peers(NULL) == 0);
    amy_unix_socket_stop(NULL);
}

static void test_round_trip_limits_and_permissions(void) {
    char dir_template[] = "/tmp/amy-unix-roundtrip-XXXXXX";
    char path[256];
    make_temp_path(dir_template, path, sizeof(path));

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == 0);
    assert(server != NULL);

    struct stat st;
    assert(lstat(path, &st) == 0);
    assert(S_ISSOCK(st.st_mode));
    assert((st.st_mode & 0777) == 0600);
    assert(st.st_uid == geteuid());
    assert(amy_unix_socket_send(server, "x", 1) == -ENOTCONN);

    int client = connect_client(path);
    const char command[] = "n60l1i2Z";
    send_packet(client, command, strlen(command));

    char received[MAX_MESSAGE_LEN];
    int rc = wait_receive(server, received, sizeof(received));
    assert(rc == (int)strlen(command));
    assert(strcmp(received, command) == 0);

    // A too-small destination leaves the packet at the head of the queue.
    const char second[] = "K28i2Z";
    send_packet(client, second, strlen(second));
    for (int i = 0; i < WAIT_STEPS; ++i) {
        rc = amy_unix_socket_receive(server, received, 4);
        if (rc != 0) break;
        usleep(WAIT_US);
    }
    assert(rc == -EMSGSIZE);
    rc = amy_unix_socket_receive(server, received, sizeof(received));
    assert(rc == (int)strlen(second));
    assert(strcmp(received, second) == 0);

    char maximum[MAX_MESSAGE_LEN];
    memset(maximum, 'm', AMY_UNIX_SOCKET_MAX_PACKET);
    send_packet(client, maximum, AMY_UNIX_SOCKET_MAX_PACKET);
    rc = wait_receive(server, received, sizeof(received));
    assert(rc == (int)AMY_UNIX_SOCKET_MAX_PACKET);
    assert(memcmp(received, maximum, AMY_UNIX_SOCKET_MAX_PACKET) == 0);
    assert(received[AMY_UNIX_SOCKET_MAX_PACKET] == '\0');

    assert(amy_unix_socket_send(server, maximum, MAX_MESSAGE_LEN) ==
           -EMSGSIZE);
    rc = amy_unix_socket_send(server, maximum, AMY_UNIX_SOCKET_MAX_PACKET);
    assert(rc == (int)AMY_UNIX_SOCKET_MAX_PACKET);

    char reply[MAX_MESSAGE_LEN];
    ssize_t reply_len = wait_client_receive(client, reply, sizeof(reply));
    assert(reply_len == (ssize_t)AMY_UNIX_SOCKET_MAX_PACKET);
    assert(memcmp(reply, maximum, AMY_UNIX_SOCKET_MAX_PACKET) == 0);

    close(client);
    amy_unix_socket_stop(server);
    assert(lstat(path, &st) < 0 && errno == ENOENT);
    remove_temp_dir(path);
}

static void test_oversize_packet_is_dropped(void) {
    char dir_template[] = "/tmp/amy-unix-oversize-XXXXXX";
    char path[256];
    make_temp_path(dir_template, path, sizeof(path));

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == 0);
    int client = connect_client(path);

    char packet[MAX_MESSAGE_LEN];
    memset(packet, 'x', sizeof(packet));
    send_packet(client, packet, sizeof(packet));
    wait_counter(amy_unix_socket_oversize_packets, server, 1);
    assert(amy_unix_socket_oversize_packets(server) == 1);

    char received[MAX_MESSAGE_LEN];
    assert(amy_unix_socket_receive(server, received, sizeof(received)) == 0);

    close(client);
    amy_unix_socket_stop(server);
    remove_temp_dir(path);
}

static void test_queue_is_bounded_and_ordered(void) {
    char dir_template[] = "/tmp/amy-unix-queue-XXXXXX";
    char path[256];
    make_temp_path(dir_template, path, sizeof(path));

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == 0);
    int client = connect_client(path);

    const uint32_t extra = 8;
    for (uint32_t i = 0; i < AMY_UNIX_SOCKET_QUEUE_CAPACITY + extra; ++i) {
        char packet[32];
        int len = snprintf(packet, sizeof(packet), "packet-%03u", i);
        assert(len > 0 && (size_t)len < sizeof(packet));
        send_packet(client, packet, (size_t)len);
    }

    wait_counter(amy_unix_socket_queue_overruns, server, extra);
    assert(amy_unix_socket_queue_overruns(server) == extra);

    for (uint32_t i = 0; i < AMY_UNIX_SOCKET_QUEUE_CAPACITY; ++i) {
        char expected[32];
        int expected_len = snprintf(expected, sizeof(expected),
                                    "packet-%03u", i);
        char received[MAX_MESSAGE_LEN];
        int rc = amy_unix_socket_receive(server, received, sizeof(received));
        assert(rc == expected_len);
        assert(strcmp(received, expected) == 0);
    }

    char received[MAX_MESSAGE_LEN];
    assert(amy_unix_socket_receive(server, received, sizeof(received)) == 0);
    close(client);
    amy_unix_socket_stop(server);
    remove_temp_dir(path);
}

static void test_only_one_client_and_reconnect(void) {
    char dir_template[] = "/tmp/amy-unix-clients-XXXXXX";
    char path[256];
    make_temp_path(dir_template, path, sizeof(path));

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == 0);
    int first = connect_client(path);

    char received[MAX_MESSAGE_LEN];
    send_packet(first, "first", 5);
    assert(wait_receive(server, received, sizeof(received)) == 5);
    assert(strcmp(received, "first") == 0);

    int rejected = connect_client(path);
    wait_counter(amy_unix_socket_rejected_peers, server, 1);
    assert(amy_unix_socket_rejected_peers(server) == 1);

    // Rejecting a second connection must not disturb the established client.
    send_packet(first, "still-first", 11);
    assert(wait_receive(server, received, sizeof(received)) == 11);
    assert(strcmp(received, "still-first") == 0);
    close(rejected);

    close(first);
    wait_until_disconnected(server);

    int second = connect_client(path);
    send_packet(second, "second", 6);
    assert(wait_receive(server, received, sizeof(received)) == 6);
    assert(strcmp(received, "second") == 0);

    close(second);
    amy_unix_socket_stop(server);
    remove_temp_dir(path);
}

static void test_live_socket_is_not_stolen(void) {
    char dir_template[] = "/tmp/amy-unix-live-XXXXXX";
    char path[256];
    make_temp_path(dir_template, path, sizeof(path));

    amy_unix_socket_server_t *first_server = NULL;
    assert(amy_unix_socket_start(&first_server, path) == 0);
    int client = connect_client(path);
    send_packet(client, "before", 6);

    char received[MAX_MESSAGE_LEN];
    assert(wait_receive(first_server, received, sizeof(received)) == 6);

    amy_unix_socket_server_t *second_server = NULL;
    assert(amy_unix_socket_start(&second_server, path) == -EADDRINUSE);
    assert(second_server == NULL);

    send_packet(client, "after", 5);
    assert(wait_receive(first_server, received, sizeof(received)) == 5);
    assert(strcmp(received, "after") == 0);

    close(client);
    amy_unix_socket_stop(first_server);
    remove_temp_dir(path);
}

static void test_owned_stale_socket_is_replaced(void) {
    char dir_template[] = "/tmp/amy-unix-stale-XXXXXX";
    char path[256];
    make_temp_path(dir_template, path, sizeof(path));

    int stale = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    assert(stale >= 0);
    struct sockaddr_un addr;
    socket_address(&addr, path);
    assert(bind(stale, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    close(stale);

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == 0);
    amy_unix_socket_stop(server);

    struct stat st;
    assert(lstat(path, &st) < 0 && errno == ENOENT);
    remove_temp_dir(path);
}

static void test_existing_regular_file_is_never_removed(void) {
    char dir_template[] = "/tmp/amy-unix-file-XXXXXX";
    char path[256];
    make_temp_path(dir_template, path, sizeof(path));

    int fd = open(path, O_CREAT | O_WRONLY | O_EXCL, 0600);
    assert(fd >= 0);
    close(fd);

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == -EEXIST);
    assert(server == NULL);

    struct stat st;
    assert(lstat(path, &st) == 0);
    assert(S_ISREG(st.st_mode));
    assert(unlink(path) == 0);
    remove_temp_dir(path);
}

static void test_stop_preserves_replacement_path(void) {
    char dir_template[] = "/tmp/amy-unix-replaced-XXXXXX";
    char path[256];
    make_temp_path(dir_template, path, sizeof(path));

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == 0);
    assert(unlink(path) == 0);

    int fd = open(path, O_CREAT | O_WRONLY | O_EXCL, 0600);
    assert(fd >= 0);
    close(fd);

    amy_unix_socket_stop(server);

    struct stat st;
    assert(lstat(path, &st) == 0);
    assert(S_ISREG(st.st_mode));
    assert(unlink(path) == 0);
    remove_temp_dir(path);
}

int main(void) {
    test_invalid_arguments();
    test_round_trip_limits_and_permissions();
    test_oversize_packet_is_dropped();
    test_queue_is_bounded_and_ordered();
    test_only_one_client_and_reconnect();
    test_live_socket_is_not_stolen();
    test_owned_stale_socket_is_replaced();
    test_existing_regular_file_is_never_removed();
    test_stop_preserves_replacement_path();
    puts("amy unix socket tests passed");
    return 0;
}
