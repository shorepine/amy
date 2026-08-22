# AMY Android Hello World

Minimal Android application proving the generic AMY Android service end to end.

On launch it:

1. starts `org.amy.audio.AmyService` from the `amy-service` AAR/module;
2. retries a connection to the app-private `<filesDir>/amy.sock` Unix-domain `SOCK_SEQPACKET` socket until the AMY/Oboe service publishes its ready socket;
3. configures raw oscillator 0 as a sine wave and sets AMY global output gain to `V2.0`;
4. waits 30 ms so that setup is committed on a fresh AMY instance before the first note-on;
5. sends AMY wire commands for C4, D4, E4, F4, G4, A4, B4, C5;
6. shows `C scale complete` when all packets have been sent.

The note path does not call AMY through JNI. JNI is used only for the Android client-side Unix socket syscalls because the Java `LocalSocket` API is stream-oriented. The synth process receives ordinary AMY wire packets exactly as another AMY wire transport would.

The generic AMY Android service also logs Oboe's actual output device ID and resolves it through `AudioDeviceInfo`, so device logs identify routes such as `BUILTIN_SPEAKER`, `BUILTIN_EARPIECE`, Bluetooth, wired headphones, or USB where Android exposes a matching device.

## Wire sequence

Setup:

```text
v0w0V2.0Z
```

`V` is AMY's global output gain. It is intentionally set above unity in this audible hello-world test; it is not an oscillator-local amplitude control.

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

The CI Android emulator smoke test builds the AAR/APK and performs two clean install/launch cycles. Each cycle must show exactly one AMY/Oboe startup, an output-route diagnostic, exactly one completed C scale, all eight note-on packets, and no socket failure.
