# AMY in Godot

AMY exposes one high-level GDScript API across three different runtime backends:

- **Desktop native (macOS/Linux/Windows):** AMY runs in-process through the `AmySynth` GDExtension and feeds Godot's `AudioStreamGenerator`.
- **Android:** `Amy.gd` keeps the same Dictionary-to-wire API, but AMY runs in the independent Android `:amy` service from the `amy-service` AAR. Godot sends ordinary AMY wire messages over the app-private `amy.sock` socket. The Android APK does not contain the `AmySynth` GDExtension.
- **Web:** AMY runs in its WASM/AudioWorklet backend and `Amy.gd` sends wire messages through `JavaScriptBridge`.

The existing desktop/web addon targets Godot 4.3+. The Android integration is continuously tested with Godot 4.7.2 and Android API 35/36 tooling.

## Common GDScript API

Create an `Amy` node and connect its readiness/error signals before adding it to the scene tree. Connecting first avoids missing a backend that becomes ready immediately.

```gdscript
var amy: Amy

func _ready() -> void:
    amy = Amy.new()
    amy.backend_ready.connect(_on_amy_ready)
    amy.backend_error.connect(_on_amy_error)
    add_child(amy)

func _on_amy_ready() -> void:
    amy.send({"osc": 0, "wave": Amy.SINE, "freq": 440, "vel": 1.0})
    amy.send({"osc": 1, "wave": Amy.TRIANGLE, "note": 60, "vel": 0.5})
    amy.send({"osc": 0, "vel": 0})

func _on_amy_error(message: String) -> void:
    push_error(message)
```

You can also build or send wire messages explicitly:

```gdscript
var wire := amy.message({"osc": 0, "note": 60, "vel": 1.0})
amy.send_raw(wire)
```

`message(Dictionary)` is shared by all backends, so Android does not maintain a second AMY API or protocol encoder.

## Desktop native setup

### Option A: download the pre-built addon

Download [`amy-godot-addon.zip`](https://github.com/shorepine/amy/releases/latest/download/amy-godot-addon.zip) and unzip it into the Godot project root so it creates `addons/amy/`.

On macOS, remove the quarantine flag if necessary:

```bash
xattr -dr com.apple.quarantine addons/amy/bin/*
```

### Option B: build from source

Clone AMY and `godot-cpp`, then run:

```bash
git clone https://github.com/shorepine/amy.git
cd amy
git clone --branch godot-4.4-stable https://github.com/godotengine/godot-cpp.git ../godot-cpp
./setup_godot.sh /path/to/your/godot/project
```

For a `godot-cpp` checkout elsewhere:

```bash
GODOT_CPP_PATH=/path/to/godot-cpp ./setup_godot.sh /path/to/your/godot/project
```

`setup_godot.sh` is the **desktop-native GDExtension** installation path. Do not use the resulting `AmySynth` GDExtension as the Android backend.

### Desktop engine configuration

Before adding the `Amy` node to the tree, desktop native code can set engine configuration properties such as:

```gdscript
amy = Amy.new()
amy.startup_bleep = false
amy.default_synths = true
amy.max_oscs = 180
add_child(amy)
```

Those properties configure the in-process native engine before `AmySynth.start()`.

## Android

### Architecture

Android deliberately separates the application/framework process from AMY:

```text
Godot application process
        |
        | amy.send(Dictionary)
        v
      Amy.gd
        |
        | shared Dictionary -> AMY wire encoder
        v
AmyClient (pure Java transport helper)
        |
        | LocalSocket / SOCK_SEQPACKET
        v
<filesDir>/amy.sock
        |
        v
Android :amy service process
        |
        +-- AMY C engine
        +-- Oboe / AAudio
```

The `amy-service` AAR owns service startup through its private Android `ContentProvider`. Godot does not call `AmyService.start()` or `stop()`. The socket is published only after Oboe has delivered its first realtime callback; successful socket connection is therefore the Android readiness boundary.

The service and Godot application remain in the same APK/UID because `amy.sock` is intentionally private (`0600` plus `SO_PEERCRED` same-UID validation).

### Android startup sequence and readiness

Android starts the application process and the independent `:amy` service asynchronously. The application is therefore allowed to begin running before `amy.sock` exists. This is normal startup behavior, not an error condition.

The startup sequence is:

```text
Android starts application package
        |
        +--> AmyAutoStartProvider.onCreate()
        |         |
        |         +--> startService(AmyService)
        |                   |
        |                   v
        |             separate :amy process
        |                   |
        |                   +--> load libamy_android.so
        |                   +--> amy_start()
        |                   +--> open/start Oboe stream
        |                   +--> wait for first realtime audio callback
        |                   +--> create/listen on <filesDir>/amy.sock
        |
        +--> Godot application continues starting
                  |
                  +--> Amy.gd tries to connect to amy.sock
                  +--> missing socket / refused connection is retried
                  +--> successful connect emits backend_ready
```

`AmyAutoStartProvider` only requests that Android start `AmyService`; it does **not** create the socket. Likewise, `prepare.sh` is only a build/staging helper and has no runtime role. The native AMY service creates and listens on `amy.sock` after AMY and Oboe are running and after the first real Oboe callback has executed. If that audio-readiness step fails, the service does not publish the socket.

Because application and service startup overlap, `Amy.gd` deliberately tolerates the socket being absent at first. Its Android backend retries `AmyClient.connect()` up to 200 times with a 50 ms delay between attempts, giving the service approximately **10 seconds** to publish `amy.sock`. Failed connection attempts during that interval are expected. Once a connection succeeds, `Amy.gd` sets its backend state to ready and emits `backend_ready`; application code that needs AMY should begin musical/audio work from that readiness signal.

If the socket is still unavailable after the retry window, `Amy.gd` emits `backend_error("Could not connect to amy.sock")`. The Android UI/main process itself is not blocked while waiting; only AMY-dependent application behavior should wait for `backend_ready`.

### No AMY/GDExtension compile in the Godot application

An Android Godot project needs:

1. the shared `amy.gd` GDScript API;
2. a **prebuilt** `amy-service` AAR packaged into the Android APK;
3. an Android export plugin that adds that AAR.

It does **not** need AMY C/C++ source, `godot/amy.gdextension`, `godot/bin/`, or a Godot AMY native client library. The final APK contains one AMY native implementation: `libamy_android.so` from the service AAR. `AmyClient` is pure Java transport code and loads no JNI/native client library.

Building the AAR from an AMY source checkout is a development/distribution step, not a requirement that every Godot application must repeat. A released/prebuilt AAR can be reused by multiple Godot projects.

### Android hello-world

The repository contains `godot/android-hello-world`, which demonstrates the intended packaging and API boundary.

From an AMY source checkout:

```bash
bash godot/android-hello-world/prepare.sh
godot --editor --path godot/android-hello-world
```

`prepare.sh` builds debug/release service AARs and copies the shared `godot/amy.gd` into the example. The example's `addons/amy_android` export plugin packages the appropriate AAR. This source-build helper exists for development and CI; a downstream project can instead package a prebuilt AAR directly.

Do not add the desktop `amy.gdextension` or its compiled libraries to an Android export.

### Android API surface

The ordinary wire-oriented GDScript API works on Android:

- `send(params: Dictionary)`
- `message(params: Dictionary)`
- `send_raw(msg: String)`
- `panic()`
- `reset_sysclock()` (implemented as an ordinary wire event)
- the wave/filter/envelope constants and Dictionary formatting helpers

The table-driven methods that call AMY's C API directly in the desktop GDExtension (`render_load()`, `set_render_load_threshold()`, `bleep()`, `sequencer_ticks()`, `dump_state()`) are **not transported over `amy.sock` by this Android backend**. Do not rely on those methods on Android until a wire/status equivalent is defined.

Likewise, the `Amy` node's pre-start engine configuration properties (`max_oscs`, `max_buses`, feature toggles, and similar settings) configure the in-process desktop backend but do not reconfigure an already-started Android service. On Android, engine configuration is owned by the AAR/service. Parameters that are part of the ordinary AMY wire protocol should be sent with `amy.send(...)` instead.

## Web export

The native GDExtension is not used on web. AMY runs as its pre-built WASM module and the `Amy` class switches to `JavaScriptBridge`.

1. Run `addons/amy/install.gd` from the Godot Script Editor.
2. In the Web export preset set **Custom HTML Shell** to `res://export/custom_shell.html`.
3. Exclude native/build files while keeping `amy.gd`:

   ```text
   addons/amy/bin/*,addons/amy/src/*,addons/amy/amy_src/*,addons/amy/web/*,addons/amy/SConstruct,addons/amy/install.gd,addons/amy/amy.gdextension
   ```

4. Export to a directory such as `dist/`.
5. Copy the AMY web audio support files:

   ```bash
   cp -r addons/amy/web/ dist/web_audio/
   cp addons/amy/web/enable-threads.js dist/
   ```

For local testing, serve the export directory through HTTP rather than opening the HTML file directly.

## Backend summary

| Platform | AMY engine location | Godot-side native AMY code | Audio owner | Control path |
|---|---|---|---|---|
| macOS/Linux/Windows | Godot process | `AmySynth` GDExtension | Godot `AudioStreamGenerator` | `Amy.gd` -> GDExtension |
| Android | separate `:amy` process | none | Oboe/AAudio service | `Amy.gd` -> `AmyClient` -> `amy.sock` |
| Web | WASM/AudioWorklet | none | Web Audio | `Amy.gd` -> `JavaScriptBridge` |

## API reference

### `amy.send(params: Dictionary)`

Sends an AMY message using named parameters matching the Python API. Common parameters include `osc`, `wave`, `freq`, `note`, `vel`, `amp`, `duty`, `pan`, `patch`, `filter_freq`, `filter_type`, `resonance`, `feedback`, `ratio`, `algorithm`, `bp0`, `bp1`, `volume`, `tempo`, `chorus`, `reverb`, and `echo`.

See the full [AMY API reference](api.md) for all fields.

### `amy.message(params: Dictionary)`

Builds the AMY wire string without sending it. This is the shared encoder used by `send()` on desktop, Android, and web.

### `amy.send_raw(msg: String)`

Sends an already-built AMY wire message, for example `"v0w0f440l1"`.

### `amy.panic()`

Sends AMY's immediate stop command.

### Signals

- `backend_ready`: emitted when the selected backend is ready for messages.
- `backend_error(message)`: emitted when backend initialization or Android transport fails.

### Constants

Wave types include `Amy.SINE`, `Amy.PULSE`, `Amy.SAW_DOWN`, `Amy.SAW_UP`, `Amy.TRIANGLE`, `Amy.NOISE`, `Amy.KS`, `Amy.PCM`, `Amy.ALGO`, `Amy.PARTIAL`, `Amy.WAVETABLE`, `Amy.CUSTOM`, and `Amy.WAVE_OFF`.

Filter types include `Amy.FILTER_NONE`, `Amy.FILTER_LPF`, `Amy.FILTER_BPF`, `Amy.FILTER_HPF`, `Amy.FILTER_LPF24`, `Amy.FILTER_NOTCH`, and `Amy.FILTER_PHASER`.
