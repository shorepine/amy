# AMY Android Hello World

Minimal Android application proving the generic AMY Android service end to end while keeping the client completely transport-only.

The hello-world application code does **not** import `AmyService`, does not call a start/stop API, does not load an AMY/JNI client library, and does not compile any AMY source. The `amy-service` AAR is packaged in the APK; its private Android lifecycle provider starts the separate `:amy` process. `MainActivity` only opens the app-private `<filesDir>/amy.sock` Unix-domain `SOCK_SEQPACKET` socket and sends ordinary AMY wire messages.

On launch the client:

1. retries a pure-Java `android.net.LocalSocket(SOCKET_SEQPACKET)` connection to `<filesDir>/amy.sock` until the independent AMY/Oboe process publishes its ready socket;
2. sends `v0w0V10.0Z` to configure raw oscillator 0 as a sine wave at full AMY master gain;
3. waits 30 ms so setup is committed on a fresh AMY instance before the first note-on;
4. sends wire commands for C4, D4, E4, F4, G4, A4, B4, C5;
5. shows `C scale complete` when all packets have been sent.

Each Java `OutputStream.write()` is one complete AMY wire request on the `SOCK_SEQPACKET` socket. There is no AMY-specific client API between the application and the wire transport.

This is the intended framework boundary:

```text
application/framework code
        |
        | ordinary AMY wire packets
        v
<filesDir>/amy.sock  (AF_UNIX / SOCK_SEQPACKET)
        |
        v
independent Android :amy process -> AMY -> Oboe/AAudio
```

The service remains in the same Android application package/UID because the socket is deliberately private (`0600` plus same-UID peer validation). A framework therefore needs only a way to package the Android service AAR and open an Android Unix-domain socket; it does not need AMY headers, AMY source, JNI bindings, or a language-specific AMY API.

## Wire sequence

Setup:

```text
v0w0V10.0Z
```

`V` is AMY's bus/master output-volume control, not an oscillator-local amplitude control. AMY's final mixer scales this 0..10 control by 0.1, so `V10.0` selects full master gain for this audible hello-world test.

Notes use MIDI note numbers and velocity, e.g. middle C:

```text
v0n60l1Z
v0l0Z
```

The complete scale is MIDI notes `60, 62, 64, 65, 67, 69, 71, 72`.

## Build

From `android/`:

```bash
gradle :hello-world:assembleDebug
```

APK:

```text
hello-world/build/outputs/apk/debug/hello-world-debug.apk
```

The CI Android emulator smoke test builds the AAR/APK and performs two clean install/launch cycles. CI, not the example app, arms the test-only audio-capture marker before each launch. Each cycle must show exactly one AMY/Oboe startup, an output-route diagnostic, exactly one completed C scale, all eight note-on packets, and no socket failure. The audio-level regression verifies the raw AMY render stream and the exact signed-16-bit callback buffer handed to Oboe are sample-for-sample identical and checks their measured peak/RMS level.
