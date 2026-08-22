#define _GNU_SOURCE

#include "amy_unix_socket.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int connect_client(const char *path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    assert(fd >= 0);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    assert(strlen(path) < sizeof(addr.sun_path));
    strcpy(addr.sun_path, path);

    assert(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    return fd;
}

static int wait_receive(amy_unix_socket_server_t *server,
                        char *buffer,
                        size_t buffer_len) {
    for (int i = 0; i < 1000; ++i) {
        int rc = amy_unix_socket_receive(server, buffer, buffer_len);
        if (rc != 0) return rc;
        usleep(1000);
    }
    return -ETIMEDOUT;
}

static ssize_t wait_client_receive(int fd, void *buffer, size_t len) {
    for (int i = 0; i < 1000; ++i) {
        ssize_t rc = recv(fd, buffer, len, MSG_DONTWAIT);
        if (rc >= 0) return rc;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return -1;
        }
        usleep(1000);
    }
    errno = ETIMEDOUT;
    return -1;
}

static void test_round_trip_and_permissions(void) {
    char dir_template[] = "/tmp/amy-unix-socket-XXXXXX";
    char *dir = mkdtemp(dir_template);
    assert(dir != NULL);
    assert(chmod(dir, 0700) == 0);

    char path[256];
    snprintf(path, sizeof(path), "%s/amy.sock", dir);

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == 0);
    assert(server != NULL);

    struct stat st;
    assert(lstat(path, &st) == 0);
    assert(S_ISSOCK(st.st_mode));
    assert((st.st_mode & 0777) == 0600);
    assert(st.st_uid == geteuid());

    int client = connect_client(path);

    const char command[] = "n60l1i2Z";
    assert(send(client, command, strlen(command), 0) ==
           (ssize_t)strlen(command));

    char received[MAX_MESSAGE_LEN];
    int rc = wait_receive(server, received, sizeof(received));
    assert(rc == (int)strlen(command));
    assert(strcmp(received, command) == 0);

    // Too-small destination must not consume the next queued packet.
    const char second[] = "K28i2Z";
    assert(send(client, second, strlen(second), 0) ==
           (ssize_t)strlen(second));
    for (int i = 0; i < 1000; ++i) {
        rc = amy_unix_socket_receive(server, received, 4);
        if (rc != 0) break;
        usleep(1000);
    }
    assert(rc == -EMSGSIZE);
    rc = amy_unix_socket_receive(server, received, sizeof(received));
    assert(rc == (int)strlen(second));
    assert(strcmp(received, second) == 0);

    const char reply[] = "!iv1";
    for (int i = 0; i < 1000; ++i) {
        rc = amy_unix_socket_send(server, reply, strlen(reply));
        if (rc != -ENOTCONN) break;
        usleep(1000);
    }
    assert(rc == (int)strlen(reply));

    char reply_buffer[32];
    ssize_t reply_len = wait_client_receive(client,
                                            reply_buffer,
                                            sizeof(reply_buffer));
    assert(reply_len == (ssize_t)strlen(reply));
    assert(memcmp(reply_buffer, reply, strlen(reply)) == 0);

    close(client);
    amy_unix_socket_stop(server);

    assert(lstat(path, &st) < 0);
    assert(errno == ENOENT);
    assert(rmdir(dir) == 0);
}

static void test_oversize_packet_is_dropped(void) {
    char dir_template[] = "/tmp/amy-unix-oversize-XXXXXX";
    char *dir = mkdtemp(dir_template);
    assert(dir != NULL);
    assert(chmod(dir, 0700) == 0);

    char path[256];
    snprintf(path, sizeof(path), "%s/amy.sock", dir);

    amy_unix_socket_server_t *server = NULL;
    assert(amy_unix_socket_start(&server, path) == 0);
    int client = connect_client(path);

    char packet[MAX_MESSAGE_LEN];
    memset(packet, 'x', sizeof(packet));
    assert(send(client, packet, sizeof(packet), 0) == (ssize_t)sizeof(packet));

    for (int i = 0; i < 1000; ++i) {
        if (amy_unix_socket_oversize_packets(server) != 0) break;
        usleep(1000);
    }
    assert(amy_unix_socket_oversize_packets(server) == 1);

    char received[MAX_MESSAGE_LEN];
    assert(amy_unix_socket_receive(server, received, sizeof(received)) == 0);

    close(client);
    amy_unix_socket_stop(server);
    assert(rmdir(dir) == 0);
}

static void test_existing_regular_file_is_never_removed(void) {
    char dir_template[] = "/tmp/amy-unix-stale-XXXXXX";
    char *dir = mkdtemp(dir_template);
    assert(dir != NULL);
    assert(chmod(dir, 0700) == 0);

    char path[256];
    snprintf(path, sizeof(path), "%s/amy.sock", dir);

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
    assert(rmdir(dir) == 0);
}

int main(void) {
    test_round_trip_and_permissions();
    test_oversize_packet_is_dropped();
    test_existing_regular_file_is_never_removed();
    puts("amy unix socket tests passed");
    return 0;
}
