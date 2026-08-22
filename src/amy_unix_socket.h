#ifndef AMY_UNIX_SOCKET_H
#define AMY_UNIX_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#include "amy.h"

#ifdef __cplusplus
extern "C" {
#endif

// Private pathname AF_UNIX transport for local AMY control.
//
// Intended Android topology:
//   Qt/Python process <-> amy.sock <-> native AMY/Oboe process
//
// The socket thread never calls AMY. It only copies complete SOCK_SEQPACKET
// packets into this fixed SPSC queue. The audio/control owner drains packets
// explicitly at a safe point (for example, immediately before rendering the
// next AMY block) and may then pass them to amy_add_message().
//
// One connected client is supported at a time. On Linux/Android, accepted
// peers must have the same effective UID as the server process. The pathname
// is created mode 0600 and a stale socket is removed only when it is owned by
// the same UID; an existing non-socket path is never removed.

#define AMY_UNIX_SOCKET_QUEUE_CAPACITY 64u
#define AMY_UNIX_SOCKET_MAX_PACKET ((size_t)MAX_MESSAGE_LEN - 1u)

typedef struct amy_unix_socket_server amy_unix_socket_server_t;

// Start a server at path. Returns 0 on success or -errno on failure.
// out_server is set only on success.
int amy_unix_socket_start(amy_unix_socket_server_t **out_server,
                          const char *path);

// Stop the receiver thread, close any client, unlink the socket pathname and
// free the server. Safe to call with NULL.
void amy_unix_socket_stop(amy_unix_socket_server_t *server);

// Non-blocking dequeue for the AMY/control owner.
// Returns payload length (>0), 0 when no packet is queued, or -errno.
// On success out is NUL-terminated; packet payloads themselves need not carry
// a trailing NUL. If out_len is too small, returns -EMSGSIZE and leaves the
// packet queued.
int amy_unix_socket_receive(amy_unix_socket_server_t *server,
                            char *out,
                            size_t out_len);

// Send one reply packet to the currently connected client. This is intended
// for non-realtime status/introspection replies, not the audio callback.
// Returns bytes sent or -errno. The accepted client socket is non-blocking.
int amy_unix_socket_send(amy_unix_socket_server_t *server,
                         const void *data,
                         size_t len);

// Diagnostic counters. They are monotonic until the server is stopped.
uint32_t amy_unix_socket_queue_overruns(
    const amy_unix_socket_server_t *server);
uint32_t amy_unix_socket_oversize_packets(
    const amy_unix_socket_server_t *server);
uint32_t amy_unix_socket_rejected_peers(
    const amy_unix_socket_server_t *server);

#ifdef __cplusplus
}
#endif

#endif  // AMY_UNIX_SOCKET_H
