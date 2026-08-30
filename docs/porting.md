# Porting AMY and local-service transports

AMY's C engine can run inside an application or in a separate local process.
The second form is useful when a framework or language should remain a
wire-protocol client and a small native service should own AMY and the audio
device.

This page records portable pieces and verified porting results. The complete
Android, Godot Android, and Windows applications linked below are external
reference implementations; their platform-specific build trees are not part
of the core AMY repository.

## Embedding boundary

A native host normally:

1. creates an `amy_config_t` with `amy_default_config()`;
2. selects the host's audio and MIDI ownership before calling `amy_start()`;
3. delivers complete AMY wire messages through `amy_add_message()` at a safe
   control or render boundary;
4. obtains audio with `amy_simple_fill_buffer()` when the host owns rendering;
5. calls `amy_stop()` during shutdown.

Keep blocking IPC away from the realtime audio callback. If a receiver thread
accepts commands, move them through a bounded queue and let the AMY/audio owner
drain that queue between render blocks.

## Linux/Android packet transport

`src/amy_unix_socket.[ch]` implements a local pathname `AF_UNIX` /
`SOCK_SEQPACKET` server for Linux and Android. It is transport-only: its thread
does not call AMY.

The server provides:

- one logical request per packet, up to `MAX_MESSAGE_LEN - 1` bytes;
- a fixed 64-packet single-producer/single-consumer queue;
- one connected client at a time;
- pathname mode `0600` and same-effective-UID peer checks with `SO_PEERCRED`;
- refusal to replace a live listener or remove a non-socket/reused pathname;
- non-blocking dequeue and reply calls;
- counters for queue overruns, oversized packets, and rejected clients.

The render owner can drain commands immediately before a new AMY block:

```c
char message[MAX_MESSAGE_LEN];
for (;;) {
    int length = amy_unix_socket_receive(server, message, sizeof(message));
    if (length <= 0) break;
    amy_add_message(message);
}
```

`amy_unix_socket_send()` supports replies from a non-realtime control/status
path. It must not be called from the audio callback.

Run the AddressSanitizer/UndefinedBehaviorSanitizer host regression on Linux:

```bash
bash tests/run_amy_unix_socket_test.sh
```

The test covers maximum and oversized packets, non-consuming `EMSGSIZE`, queue
ordering/overrun behavior, connection replacement/rejection, reconnects,
permissions, active/stale paths, and safe shutdown cleanup.

On unsupported platforms these functions return `-ENOTSUP`. A stream or
platform-native IPC adapter can preserve the same higher-level rule: one
complete AMY wire request is delivered to the AMY owner at a safe boundary.

## Verified Android NDK recipe

The external [Android Oboe service reference][android-oboe] demonstrates that
the AMY core compiles for Android NDK without changes to its synthesis sources.
That build uses:

```text
AMY_DAISY=1
AMY_HOST_MIDI=1
AMY_NO_MINIAUDIO=1
AMY_WAVETABLE=1
```

`AMY_DAISY` selects AMY's existing 48 kHz / 128-frame profile.
`AMY_NO_MINIAUDIO` lets Oboe own audio, and `AMY_HOST_MIDI` lets the service
supply the MIDI lifecycle hooks. The Oboe callback calls
`amy_simple_fill_buffer()` only when it needs another AMY block and drains the
socket queue before that block.

One declaration-only compatibility header is force-included in the C
translation units so `pcm.c` sees allocators already supplied by `delay.c`
under `AMY_DAISY`:

```c
#include <stddef.h>

void *qspi_malloc(size_t size);
void qspi_free(void *ptr);
```

Do not link a second allocator implementation.

The Android audio-level regression also caught an important gain detail:
AMY's `V` bus/master control is a `0..10` scale, and final mixdown multiplies
it by `0.1`. Therefore `V2.0` is 20% linear gain, while `V10.0` is full master
gain. This differs from oscillator velocity/amplitude and per-synth `iV`.

The reference branch includes the Gradle AAR, private `:amy` service, Oboe
adapter, transport-only Java client, emulator tests, and captured AMY-to-Oboe
audio comparison. Those framework-specific files remain outside the core AMY
tree.

## Godot lifecycle and Android reference

The shared `godot/amy.gd` wrapper exposes two platform-independent signals:

- `backend_ready`, emitted after the selected native or web backend can accept
  messages;
- `backend_error(message)`, emitted if backend initialization fails.

Connect them before adding the `Amy` node to the scene tree, because a native
backend may become ready synchronously:

```gdscript
var amy := Amy.new()
amy.backend_ready.connect(func(): amy.send({"osc": 0, "note": 60, "vel": 1}))
amy.backend_error.connect(func(message: String): push_error(message))
add_child(amy)
```

The external [Godot Android reference][godot-android] uses the same signals and
Dictionary-to-wire encoder while keeping AMY in the separate Android service.
It contains the AAR packaging example and Android emulator validation. The
Android backend itself is not part of the core Godot addon.

## Verified Windows named-pipe adapter

Windows local IPC did not require changes to AMY. The native
[LB Omnichord Windows service][windows-service] compiles the normal AMY C
sources and implements the transport entirely in its host wrapper:

- `CreateNamedPipeA()` creates one private byte-mode pipe instance with
  `PIPE_REJECT_REMOTE_CLIENTS`;
- the [launcher][windows-launcher] supplies a unique per-run pipe name and
  publishes readiness only after the pipe and AMY exist;
- the [Qt client][windows-client] uses `QLocalSocket` and writes LF-framed
  records because a Windows named pipe is a byte stream rather than a
  `SOCK_SEQPACKET` endpoint;
- the service buffers partial/multiple `ReadFile()` results, requires each
  completed request to end in `Z`, then calls `amy_add_message()`;
- AMY remains in a separate native service process and owns miniaudio output.

The [Windows build target][windows-cmake], [packaging regression][windows-test],
and [Windows Server 2025 release test][windows-ci] compile the service, run an
offline `amy_simple_fill_buffer()` self-test, and exercise the packaged
Qt-to-pipe-to-AMY boundary. These hosted tests prove compilation, command
delivery, non-silent offline rendering, and process cleanup; they do not prove
physical audio, MIDI, latency, or dropout behavior. See the [full Windows
design and validation notes][windows-doc] for those limits.

[android-oboe]: https://github.com/linuxificator/amy/tree/upstream/android-oboe
[godot-android]: https://github.com/linuxificator/amy/tree/upstream/godot-android
[windows-service]: https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/packaging/windows/amy_service.c
[windows-launcher]: https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/packaging/windows/run_windows.ps1
[windows-client]: https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/code/amy_transport.py
[windows-cmake]: https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/packaging/windows/CMakeLists.txt
[windows-test]: https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/tests/test_packaging.py
[windows-ci]: https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/.github/workflows/desktop-release.yml
[windows-doc]: https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/docs/WINDOWS_NATIVE.md
