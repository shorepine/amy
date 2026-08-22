# AMY Android Oboe service

This directory builds a generic Android AAR that hosts AMY in an unexported
`:amy` service process. The service owns Oboe/AAudio output and receives native
AMY wire messages through the private pathname Unix transport implemented by
`src/amy_unix_socket.[ch]`.

```text
Android client process
    |
    | AF_UNIX / SOCK_SEQPACKET
    | <app filesDir>/amy.sock
    | one AMY wire message per packet
    v
Android :amy service process
    |
    +-- amy_unix_socket receiver thread
    +-- fixed 64-packet SPSC queue
    +-- AMY C engine
    +-- Oboe low-latency callback
            |
            v
          AAudio
```

The AAR is intended to be embedded by an Android application that wants to use
AMY as its local synth engine. The client can be written with the Android SDK,
Kotlin/Java, native code, Qt, another framework, or any other environment able
to start the service and use an Android Unix-domain `SOCK_SEQPACKET` socket.
AMY itself has no dependency on the client UI framework.

The service declaration uses `android:exported="false"` and
`android:process=":amy"`. Consequently the service runs in a separate process
from the client while remaining in the same Android application package and
under the same application UID.

The service only accepts the exact pathname `<Context.getFilesDir()>/amy.sock`.
The native transport creates that node mode `0600` and additionally verifies
accepted peers with `SO_PEERCRED` against the service effective UID. See
`docs/android_unix_socket.md` for the transport/security contract.

## Audio profile

The Android native build uses AMY's existing 48 kHz / 128-frame build profile
and defines `AMY_NO_MINIAUDIO`; Oboe is the sole audio backend.

Oboe requests:

- stereo signed 16-bit output
- 48 kHz
- `PerformanceMode::LowLatency`
- `SharingMode::Exclusive`
- callback-driven output

The callback size is not assumed to equal 128 frames. The native adapter keeps
only the unconsumed tail of the current AMY block and calls
`amy_simple_fill_buffer()` exactly when another AMY block is required. It does
not add an extra 128-frame output ring.

Before each new AMY block the callback drains up to 64 already-queued socket
packets and passes them to `amy_add_message()`. The socket thread itself never
calls AMY and never participates in audio rendering.

AMY is started with its internal platform audio disabled and with AMY rendering
owned by the Oboe callback thread. The current Android build configuration
reserves 16 Karplus-Strong oscillators.

## JNI boundary

JNI is lifecycle glue only. `AmyService` calls the native library to start and
stop AMY/Oboe with the validated socket pathname. Notes, patches, sequencer
commands and other musical control do not cross JNI; they use the unchanged AMY
wire protocol through `amy.sock`.

The client-facing architecture is therefore deliberately transport-oriented:

```text
client application -> amy.sock -> AMY/Oboe service
```

A client does not need AMY-specific JNI bindings. It only needs to start the
service and exchange AMY wire packets over the private socket.

## Socket client contract

Use `AF_UNIX` + `SOCK_SEQPACKET` and send one logical AMY request per packet.
For example the payload of three consecutive packets may be:

```text
K28i2Z
n60l1i2Z
n60l0i2Z
```

Do not add stream framing or depend on newline boundaries. Packet boundaries
are preserved by `SOCK_SEQPACKET`.

The pathname also serves as the engine readiness boundary. `amy.sock` is not
created until Oboe has started and the realtime audio callback has executed at
least once. A client may therefore retry `connect()` while the service starts;
once `connect()` succeeds it may begin sending AMY wire packets immediately.
No fixed Android-startup sleep is required.

The socket is bidirectional. The Android engine currently consumes ordinary AMY
wire commands; the existing `amy_unix_socket_send()` path is ready for compact
introspection/status replies when that functionality is integrated.

## Client integration

A client application needs to:

1. package the `amy-service` AAR/module in the Android application;
2. start `org.amy.audio.AmyService` while synthesis is required;
3. obtain the application's actual private files directory rather than
   hard-code `/data/user/...`;
4. retry an `AF_UNIX` / `SOCK_SEQPACKET` connection to `<filesDir>/amy.sock`
   until the service publishes its ready socket;
5. send one ordinary AMY wire message per packet;
6. optionally receive response packets over the same bidirectional socket;
7. stop and reconnect cleanly across Android application/audio lifecycle
   events.

The transport deliberately does not prescribe a programming language or UI
framework. A minimal example client is provided separately by the Android
hello-world application.

## Building the AAR

Requirements used by CI:

- JDK 17
- Android SDK platform 36
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.13
- Android Gradle Plugin 8.13.2
- Oboe 1.10.0 (Prefab dependency)

From the repository root:

```bash
cd android
gradle :amy-service:assembleDebug
```

The production Android service build targets `arm64-v8a`. Output is below:

```text
android/amy-service/build/outputs/aar/
```

## Tests

The private socket regression test is:

```bash
bash tests/run_amy_unix_socket_test.sh
```

It validates packet round-trip, mode/ownership, `EMSGSIZE` behavior,
oversized-packet rejection, cleanup, and protection against deleting an
existing non-socket path.

`.github/workflows/android.yml` runs that regression plus a complete Android
AAR/NDK/Oboe build. The earlier `.github/workflows/android-unix-socket.yml`
continues to isolate the transport regression itself.

## Hardware-test items

The first device tests should measure:

1. command-to-audio latency;
2. negotiated Oboe callback/device buffer sizes;
3. xruns during patch changes and heavy reverb/delay loads;
4. suspend/resume and audio-device changes;
5. whether executing rare heavy AMY commands at a block boundary needs further
   separation from the realtime callback.
