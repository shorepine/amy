# AMY Godot Android Hello World

This example exercises the normal Godot `Amy` GDScript API on Android while AMY itself runs in the independent Android `:amy` service process.

```text
Godot game code
    |
    | amy.send(Dictionary)
    v
godot/amy.gd
    |
    | Dictionary -> ordinary AMY wire message
    v
AmyClient (pure Java transport helper)
    |
    | LocalSocket / SOCK_SEQPACKET
    v
<filesDir>/amy.sock
    |
    v
Android :amy service -> AMY -> Oboe/AAudio
```

The Godot application does not start or stop `AmyService`, does not compile AMY C source, and does not package the `AmySynth` GDExtension. The only AMY native implementation in the Android APK is `libamy_android.so` inside the service AAR. The AAR remains in the same APK/UID because `amy.sock` is intentionally app-private.

## Prepare from this source checkout

Requirements are the same as `android/README.md` plus a Godot 4 Android editor/export-template installation.

From the repository root:

```bash
bash godot/android-hello-world/prepare.sh
```

`prepare.sh` builds the Android service AARs, copies them into the example's export plugin, and copies the shared `godot/amy.gd` into the project. This source-development step is not a requirement for downstream Godot applications when a prebuilt service AAR is supplied: they only package the AAR and GDScript API.

Open the project:

```bash
godot --editor --path godot/android-hello-world
```

The included export plugin adds the matching debug or release AAR to Android exports. `Android ARM64` is the normal device preset. `Android CI x86_64` is deliberately transport/audio-only: it skips the optional Canvas UI so unrelated emulator/SwiftShader renderer limitations do not obscure the AMY socket/audio regression.

## API boundary

The example uses only the public GDScript API. Connect the readiness/error signals before adding the `Amy` node to the scene tree so an immediately ready backend cannot be missed:

```gdscript
var amy: Amy

func _ready() -> void:
    amy = Amy.new()
    amy.backend_ready.connect(_on_amy_ready)
    amy.backend_error.connect(_on_amy_error)
    add_child(amy)

func _on_amy_ready() -> void:
    amy.send({"osc": 0, "wave": Amy.SINE, "volume": 10.0})
    amy.send({"osc": 0, "note": 60, "vel": 1.0})
    amy.send({"osc": 0, "vel": 0.0})

func _on_amy_error(message: String) -> void:
    push_error(message)
```

`Amy.message(Dictionary)` is the same wire generator used on the other Godot backends. On Android, `Amy.send()` hands that wire string to the pure-Java `AmyClient`, which sends one request per `SOCK_SEQPACKET` packet.

For a normal project, copy/package the shared `amy.gd`, the `amy-service` AAR, and an Android export plugin equivalent to `addons/amy_android/`. Do **not** add `godot/amy.gdextension`, `godot/bin/`, or AMY C/C++ sources to an Android export.

See `docs/godot.md` for platform differences and supported Android API surface.
