# Android private `amy.sock` transport

`src/amy_unix_socket.c` provides a small Linux/Android pathname `AF_UNIX`
transport intended for a stand-alone AMY + Oboe Android process.

The Android application should choose a pathname below its private internal
storage directory, for example conceptually:

```
/data/user/0/<package>/files/amy.sock
```

Do not hard-code that example path. Obtain the application's actual internal
files directory from Android and pass the resulting full pathname to the native
AMY process/service.

## Security properties

The server:

- uses `AF_UNIX` + `SOCK_SEQPACKET` rather than TCP/UDP;
- creates the socket pathname mode `0600`;
- on Linux/Android accepts only peers whose `SO_PEERCRED` UID equals the
  server's effective UID;
- removes a stale socket only when it is a socket owned by the same UID;
- never removes an existing regular file or foreign-owned socket;
- supports one connected client at a time.

The Android private app-data parent directory remains the primary sandbox
boundary. Socket mode and peer credentials are defense in depth.

## Realtime ownership

The socket receiver thread never calls AMY. Each received `SOCK_SEQPACKET`
message is copied into a fixed 64-entry SPSC queue. There is no allocation in
the dequeue path.

The AMY/Oboe owner should drain the queue at a safe block boundary:

```c
#include "amy.h"
#include "amy_unix_socket.h"

static amy_unix_socket_server_t *amy_socket;

void process_amy_socket(void) {
    char message[MAX_MESSAGE_LEN];
    for (;;) {
        int len = amy_unix_socket_receive(
            amy_socket, message, sizeof(message));
        if (len <= 0) break;
        amy_add_message(message);
    }
}
```

For an Oboe backend, call `process_amy_socket()` immediately before producing a
new AMY render block, not from the socket thread.

A packet payload may omit a terminating NUL; the dequeue API adds one. Keep a
single AMY wire command or other logical request in each packet. Maximum packet
payload is `MAX_MESSAGE_LEN - 1` bytes.

## Bidirectional replies

`amy_unix_socket_send()` sends one `SOCK_SEQPACKET` reply to the current
client. It is non-blocking and intended for control/status/introspection paths,
not for the realtime audio callback.

This means the compact introspection protocol can later use the same connection:

```
Qt -> AMY   ?iv
AMY -> Qt   !iv1
```

The socket transport itself intentionally does not depend on the introspection
implementation, so the two branches can be reviewed and merged independently.

## Starting and stopping

```c
amy_unix_socket_server_t *server = NULL;
int rc = amy_unix_socket_start(&server, socket_path);
if (rc < 0) {
    // rc is -errno
}

// ... run AMY/Oboe ...

amy_unix_socket_stop(server);
```

Stopping joins the receiver thread and removes the socket pathname.

## Diagnostics

These counters can be queried from a non-realtime diagnostics path:

- `amy_unix_socket_queue_overruns()`
- `amy_unix_socket_oversize_packets()`
- `amy_unix_socket_rejected_peers()`

A queue overrun means the AMY/control owner is not draining packets quickly
enough. The transport drops the new packet rather than blocking the receiver or
allocating more memory.

## Host regression test

On Linux:

```bash
bash tests/run_amy_unix_socket_test.sh
```

The test verifies round-trip packet transport, socket mode/ownership,
non-consuming `EMSGSIZE` behavior, oversized-packet rejection, pathname cleanup,
and refusal to delete a pre-existing regular file.
