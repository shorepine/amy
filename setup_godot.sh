#!/usr/bin/env bash
#
# setup_godot.sh — Build and install the AMY GDExtension addon into a Godot project.
#
# Usage:
#   ./setup_godot.sh /path/to/your/godot/project
#
# Environment variables:
#   GODOT_CPP_PATH  Path to godot-cpp checkout (default: ../godot-cpp)
#   JOBS            Parallel build jobs (default: 8)
#
# This script:
#   1. Builds the native GDExtension library using scons
#   2. Creates addons/amy/ in your Godot project with:
#      - The GDScript Amy class (amy.gd)
#      - The GDExtension config and compiled library
#      - Web export support files
#      - The install.gd editor script for web setup

set -euo pipefail

AMY_DIR="$(cd "$(dirname "$0")" && pwd)"
GODOT_CPP_PATH="${GODOT_CPP_PATH:-${AMY_DIR}/../godot-cpp}"
JOBS="${JOBS:-8}"

# ── Check arguments ──────────────────────────────────────────────
if [ $# -lt 1 ]; then
    echo "Usage: $0 <godot-project-path>"
    echo ""
    echo "  Builds the AMY GDExtension and installs it into a Godot project."
    echo ""
    echo "  Environment variables:"
    echo "    GODOT_CPP_PATH  Path to godot-cpp (default: ../godot-cpp)"
    echo "    JOBS            Parallel jobs (default: 8)"
    exit 1
fi

PROJECT_DIR="$(cd "$1" && pwd)"
ADDON_DIR="${PROJECT_DIR}/addons/amy"

if [ ! -f "${PROJECT_DIR}/project.godot" ]; then
    echo "ERROR: ${PROJECT_DIR} does not look like a Godot project (no project.godot found)"
    exit 1
fi

if [ ! -d "${GODOT_CPP_PATH}" ]; then
    echo "ERROR: godot-cpp not found at ${GODOT_CPP_PATH}"
    echo "  Clone it:  git clone --branch godot-4.4-stable https://github.com/godotengine/godot-cpp.git ${GODOT_CPP_PATH}"
    echo "  Or set:    GODOT_CPP_PATH=/path/to/godot-cpp $0 $1"
    exit 1
fi

echo "=== AMY Godot Addon Setup ==="
echo "  AMY source:    ${AMY_DIR}/src"
echo "  godot-cpp:     ${GODOT_CPP_PATH}"
echo "  Godot project: ${PROJECT_DIR}"
echo "  Addon output:  ${ADDON_DIR}"
echo ""

# ── Create addon directory structure ─────────────────────────────
echo "Creating addon directory structure..."
mkdir -p "${ADDON_DIR}/src"
mkdir -p "${ADDON_DIR}/bin"
mkdir -p "${ADDON_DIR}/web"

# ── Copy AMY C source (symlink-free, for scons) ─────────────────
echo "Copying AMY source..."
mkdir -p "${ADDON_DIR}/amy_src"
cp "${AMY_DIR}"/src/*.c "${ADDON_DIR}/amy_src/" 2>/dev/null || true
cp "${AMY_DIR}"/src/*.h "${ADDON_DIR}/amy_src/" 2>/dev/null || true
# Godot shouldn't try to import C files
touch "${ADDON_DIR}/amy_src/.gdignore"

# ── Copy GDExtension wrapper source ─────────────────────────────
echo "Copying GDExtension source..."
cat > "${ADDON_DIR}/src/amy_gdextension.h" << 'HEADER_EOF'
#ifndef AMY_GDEXTENSION_H
#define AMY_GDEXTENSION_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/audio_stream_generator_playback.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

namespace godot {

class AmySynth : public Node {
	GDCLASS(AmySynth, Node)

private:
	bool initialized = false;
	int sample_rate = 44100;
	int block_size = 256;

	// Config properties — set these before calling start()
	bool chorus = true;
	bool reverb = true;
	bool echo = true;
	bool default_synths = false;
	bool partials = true;
	bool custom = true;
	bool startup_bleep = false;
	bool audio_in = false;
	int max_oscs = 180;
	int max_voices = 64;
	int max_synths = 64;

protected:
	static void _bind_methods();

public:
	AmySynth();
	~AmySynth();

	void start();
	void stop();
	void send(const String &message);
	void send_note(int osc, int midi_note, float velocity, int duration_ms);
	PackedVector2Array fill_buffer();
	int get_sample_rate() const;
	int get_block_size() const;
	uint64_t get_sysclock() const;
	bool is_running() const;

	// Config setters/getters
	void set_chorus(bool p_val);
	bool get_chorus() const;
	void set_reverb(bool p_val);
	bool get_reverb() const;
	void set_echo(bool p_val);
	bool get_echo() const;
	void set_default_synths(bool p_val);
	bool get_default_synths() const;
	void set_partials(bool p_val);
	bool get_partials() const;
	void set_custom(bool p_val);
	bool get_custom() const;
	void set_startup_bleep(bool p_val);
	bool get_startup_bleep() const;
	void set_audio_in(bool p_val);
	bool get_audio_in() const;
	void set_max_oscs(int p_val);
	int get_max_oscs() const;
	void set_max_voices(int p_val);
	int get_max_voices() const;
	void set_max_synths(int p_val);
	int get_max_synths() const;
};

} // namespace godot

#endif // AMY_GDEXTENSION_H
HEADER_EOF

cat > "${ADDON_DIR}/src/amy_gdextension.cpp" << 'CPP_EOF'
#include "amy_gdextension.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// AMY is a C library
extern "C" {
#include "amy.h"
}

using namespace godot;

AmySynth::AmySynth() {}

AmySynth::~AmySynth() {
	if (initialized) {
		amy_stop();
		initialized = false;
	}
}

void AmySynth::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start"), &AmySynth::start);
	ClassDB::bind_method(D_METHOD("stop"), &AmySynth::stop);
	ClassDB::bind_method(D_METHOD("send", "message"), &AmySynth::send);
	ClassDB::bind_method(D_METHOD("send_note", "osc", "midi_note", "velocity", "duration_ms"), &AmySynth::send_note);
	ClassDB::bind_method(D_METHOD("fill_buffer"), &AmySynth::fill_buffer);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &AmySynth::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_block_size"), &AmySynth::get_block_size);
	ClassDB::bind_method(D_METHOD("get_sysclock"), &AmySynth::get_sysclock);
	ClassDB::bind_method(D_METHOD("is_running"), &AmySynth::is_running);

	// Config property bindings — set before calling start()
	ClassDB::bind_method(D_METHOD("set_chorus", "enabled"), &AmySynth::set_chorus);
	ClassDB::bind_method(D_METHOD("get_chorus"), &AmySynth::get_chorus);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "chorus"), "set_chorus", "get_chorus");

	ClassDB::bind_method(D_METHOD("set_reverb", "enabled"), &AmySynth::set_reverb);
	ClassDB::bind_method(D_METHOD("get_reverb"), &AmySynth::get_reverb);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "reverb"), "set_reverb", "get_reverb");

	ClassDB::bind_method(D_METHOD("set_echo", "enabled"), &AmySynth::set_echo);
	ClassDB::bind_method(D_METHOD("get_echo"), &AmySynth::get_echo);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "echo"), "set_echo", "get_echo");

	ClassDB::bind_method(D_METHOD("set_default_synths", "enabled"), &AmySynth::set_default_synths);
	ClassDB::bind_method(D_METHOD("get_default_synths"), &AmySynth::get_default_synths);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "default_synths"), "set_default_synths", "get_default_synths");

	ClassDB::bind_method(D_METHOD("set_partials", "enabled"), &AmySynth::set_partials);
	ClassDB::bind_method(D_METHOD("get_partials"), &AmySynth::get_partials);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "partials"), "set_partials", "get_partials");

	ClassDB::bind_method(D_METHOD("set_custom", "enabled"), &AmySynth::set_custom);
	ClassDB::bind_method(D_METHOD("get_custom"), &AmySynth::get_custom);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "custom"), "set_custom", "get_custom");

	ClassDB::bind_method(D_METHOD("set_startup_bleep", "enabled"), &AmySynth::set_startup_bleep);
	ClassDB::bind_method(D_METHOD("get_startup_bleep"), &AmySynth::get_startup_bleep);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "startup_bleep"), "set_startup_bleep", "get_startup_bleep");

	ClassDB::bind_method(D_METHOD("set_audio_in", "enabled"), &AmySynth::set_audio_in);
	ClassDB::bind_method(D_METHOD("get_audio_in"), &AmySynth::get_audio_in);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "audio_in"), "set_audio_in", "get_audio_in");

	ClassDB::bind_method(D_METHOD("set_max_oscs", "count"), &AmySynth::set_max_oscs);
	ClassDB::bind_method(D_METHOD("get_max_oscs"), &AmySynth::get_max_oscs);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_oscs"), "set_max_oscs", "get_max_oscs");

	ClassDB::bind_method(D_METHOD("set_max_voices", "count"), &AmySynth::set_max_voices);
	ClassDB::bind_method(D_METHOD("get_max_voices"), &AmySynth::get_max_voices);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_voices"), "set_max_voices", "get_max_voices");

	ClassDB::bind_method(D_METHOD("set_max_synths", "count"), &AmySynth::set_max_synths);
	ClassDB::bind_method(D_METHOD("get_max_synths"), &AmySynth::get_max_synths);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_synths"), "set_max_synths", "get_max_synths");
}

void AmySynth::start() {
	if (initialized) return;

	amy_config_t config = amy_default_config();
	config.audio = AMY_AUDIO_IS_NONE;  // We handle audio output ourselves via Godot
	config.features.chorus = chorus ? 1 : 0;
	config.features.reverb = reverb ? 1 : 0;
	config.features.echo = echo ? 1 : 0;
	config.features.default_synths = default_synths ? 1 : 0;
	config.features.partials = partials ? 1 : 0;
	config.features.custom = custom ? 1 : 0;
	config.features.startup_bleep = startup_bleep ? 1 : 0;
	config.features.audio_in = audio_in ? 1 : 0;
	config.max_oscs = max_oscs;
	config.max_voices = max_voices;
	config.max_synths = max_synths;
	amy_start(config);
	initialized = true;

	sample_rate = AMY_SAMPLE_RATE;
	block_size = AMY_BLOCK_SIZE;

	UtilityFunctions::print("AMY synth started (", sample_rate, " Hz, block size ", block_size, ")");
}

void AmySynth::stop() {
	if (!initialized) return;
	amy_stop();
	initialized = false;
	UtilityFunctions::print("AMY synth stopped");
}

void AmySynth::send(const String &message) {
	if (!initialized) return;
	CharString cs = message.utf8();
	amy_add_message(const_cast<char*>(cs.get_data()));
}

void AmySynth::send_note(int osc, int midi_note, float velocity, int duration_ms) {
	if (!initialized) return;
	char msg[128];
	if (duration_ms > 0) {
		snprintf(msg, sizeof(msg), "v%dw0n%dl%fd%d", osc, midi_note, velocity, duration_ms);
	} else {
		snprintf(msg, sizeof(msg), "v%dw0n%dl%f", osc, midi_note, velocity);
	}
	amy_add_message(msg);
}

PackedVector2Array AmySynth::fill_buffer() {
	PackedVector2Array buffer;

	if (!initialized) {
		buffer.resize(block_size);
		buffer.fill(Vector2(0, 0));
		return buffer;
	}

	int16_t *samples = amy_simple_fill_buffer();

	buffer.resize(block_size);
	const float scale = 1.0f / 32768.0f;

	for (int i = 0; i < block_size; i++) {
		float left = static_cast<float>(samples[i * 2]) * scale;
		float right = static_cast<float>(samples[i * 2 + 1]) * scale;
		buffer.set(i, Vector2(left, right));
	}

	return buffer;
}

int AmySynth::get_sample_rate() const {
	return sample_rate;
}

int AmySynth::get_block_size() const {
	return block_size;
}

uint64_t AmySynth::get_sysclock() const {
	if (!initialized) return 0;
	return amy_sysclock();
}

bool AmySynth::is_running() const {
	return initialized;
}

// Config setters/getters
void AmySynth::set_chorus(bool p_val) { chorus = p_val; }
bool AmySynth::get_chorus() const { return chorus; }
void AmySynth::set_reverb(bool p_val) { reverb = p_val; }
bool AmySynth::get_reverb() const { return reverb; }
void AmySynth::set_echo(bool p_val) { echo = p_val; }
bool AmySynth::get_echo() const { return echo; }
void AmySynth::set_default_synths(bool p_val) { default_synths = p_val; }
bool AmySynth::get_default_synths() const { return default_synths; }
void AmySynth::set_partials(bool p_val) { partials = p_val; }
bool AmySynth::get_partials() const { return partials; }
void AmySynth::set_custom(bool p_val) { custom = p_val; }
bool AmySynth::get_custom() const { return custom; }
void AmySynth::set_startup_bleep(bool p_val) { startup_bleep = p_val; }
bool AmySynth::get_startup_bleep() const { return startup_bleep; }
void AmySynth::set_audio_in(bool p_val) { audio_in = p_val; }
bool AmySynth::get_audio_in() const { return audio_in; }
void AmySynth::set_max_oscs(int p_val) { max_oscs = p_val; }
int AmySynth::get_max_oscs() const { return max_oscs; }
void AmySynth::set_max_voices(int p_val) { max_voices = p_val; }
int AmySynth::get_max_voices() const { return max_voices; }
void AmySynth::set_max_synths(int p_val) { max_synths = p_val; }
int AmySynth::get_max_synths() const { return max_synths; }
CPP_EOF

cat > "${ADDON_DIR}/src/register_types.h" << 'RTHEOF'
#ifndef AMY_REGISTER_TYPES_H
#define AMY_REGISTER_TYPES_H

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void initialize_amy_module(ModuleInitializationLevel p_level);
void uninitialize_amy_module(ModuleInitializationLevel p_level);

#endif // AMY_REGISTER_TYPES_H
RTHEOF

cat > "${ADDON_DIR}/src/register_types.cpp" << 'RTCEOF'
#include "register_types.h"
#include "amy_gdextension.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void initialize_amy_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(AmySynth);
}

void uninitialize_amy_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {

GDExtensionBool GDE_EXPORT amy_library_init(
	GDExtensionInterfaceGetProcAddress p_get_proc_address,
	const GDExtensionClassLibraryPtr p_library,
	GDExtensionInitialization *r_initialization
) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_amy_module);
	init_obj.register_terminator(uninitialize_amy_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}

} // extern "C"
RTCEOF

cat > "${ADDON_DIR}/src/amy_platform_stubs.c" << 'STUBEOF'
// Stub implementations for AMY platform/hardware functions.
// We don't need I2S or MIDI hardware - Godot handles all audio.
// miniaudio stubs are no longer needed thanks to -DAMY_NO_MINIAUDIO.

#include <stdint.h>
#include <stddef.h>

// --- i2s.c stubs ---
void amy_platform_init(void) {}
void amy_platform_deinit(void) {}
void amy_update_tasks(void) {}
int16_t *amy_render_audio(void) { return (int16_t *)0; }
size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) {
    (void)buffer; (void)nbytes; return nbytes;
}

// --- MIDI stubs (amy_midi.c / macos_midi.m) ---
void midi_out(uint8_t *bytes, uint16_t len) { (void)bytes; (void)len; }
void midi_local(uint8_t *bytes, uint16_t len) { (void)bytes; (void)len; }
void run_midi(void) {}
void stop_midi(void) {}
void *run_midi_macos(void *vargp) { (void)vargp; return (void *)0; }
void check_tusb_midi(void) {}
void init_tusb_midi(void) {}
STUBEOF

# ── Copy SConstruct ──────────────────────────────────────────────
echo "Copying SConstruct..."
cat > "${ADDON_DIR}/SConstruct" << 'SCONSEOF'
#!/usr/bin/env python
"""
Build script for the AMY Synthesizer GDExtension.

AMY C source lookup order:
  1. AMY_SRC_PATH environment variable (if set)
  2. ./amy_src/  (vendored source, included in this addon)
"""
import os
import sys

# Find godot-cpp (two levels up from addons/amy/)
godot_cpp_path = os.environ.get("GODOT_CPP_PATH", "../../godot-cpp")

# Find AMY C source: env var > vendored copy
amy_src_path = os.environ.get("AMY_SRC_PATH", "amy_src")
if not os.path.isdir(amy_src_path):
    print("ERROR: AMY source not found at '{}'. Either:")
    print("  - Place AMY source in addons/amy/amy_src/")
    print("  - Or set AMY_SRC_PATH environment variable")
    Exit(1)

env = SConscript(os.path.join(godot_cpp_path, "SConstruct"))

# AMY C source files (core synthesis - no platform-specific I2S, no example mains)
amy_sources = [
    "algorithms.c",
    "amy.c",
    "api.c",
    "custom.c",
    "delay.c",
    "envelope.c",
    "examples.c",
    "filters.c",
    "instrument.c",
    "interp_partials.c",
    "log2_exp2.c",
    "midi_mappings.c",
    "oscillators.c",
    "parse.c",
    "patches.c",
    "pcm.c",
    "sequencer.c",
    "transfer.c",
]

# Add AMY include path
env.Append(CPPPATH=[amy_src_path])

# AMY compile flags - suppress warnings from AMY's C code
env.Append(CFLAGS=["-Wno-unused-parameter", "-Wno-sign-compare", "-Wno-missing-field-initializers"])
env.Append(CCFLAGS=["-DAMY_WAVETABLE", "-DAMY_NO_MINIAUDIO"])

# On macOS, we need some frameworks for threading
if sys.platform == "darwin":
    env.Append(CCFLAGS=["-DMACOS"])
    env.Append(LINKFLAGS=["-framework", "CoreFoundation"])

# Build all sources together - AMY C files + GDExtension C++ files
all_sources = []

for src in amy_sources:
    src_path = os.path.join(amy_src_path, src)
    if os.path.exists(src_path):
        all_sources.append(src_path)

# GDExtension C++ sources + platform stubs
all_sources += [
    "src/amy_platform_stubs.c",
    "src/amy_gdextension.cpp",
    "src/register_types.cpp",
]

# Add our source include path
env.Append(CPPPATH=["src"])

# Build shared library directly from all sources
if sys.platform == "darwin":
    # Build as framework for macOS
    lib_name = "libamy{}".format(env["suffix"])
    framework_dir = "bin/{}.framework".format(lib_name)

    library = env.SharedLibrary(
        target=os.path.join(framework_dir, lib_name),
        source=all_sources,
    )
else:
    library = env.SharedLibrary(
        target="bin/libamy{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=all_sources,
    )

Default(library)
SCONSEOF

# ── Copy GDExtension config ─────────────────────────────────────
echo "Copying GDExtension config..."
cat > "${ADDON_DIR}/amy.gdextension" << 'GDEXTEOF'
[configuration]

entry_symbol = "amy_library_init"
compatibility_minimum = "4.3"

[libraries]

macos.debug = "res://addons/amy/bin/libamy.macos.template_debug.universal.framework"
macos.release = "res://addons/amy/bin/libamy.macos.template_debug.universal.framework"
linux.debug.x86_64 = "res://addons/amy/bin/libamy.linux.template_debug.x86_64.so"
linux.release.x86_64 = "res://addons/amy/bin/libamy.linux.template_release.x86_64.so"
windows.debug.x86_64 = "res://addons/amy/bin/libamy.windows.template_debug.x86_64.dll"
windows.release.x86_64 = "res://addons/amy/bin/libamy.windows.template_release.x86_64.dll"
GDEXTEOF

# ── Build native library ────────────────────────────────────────
echo ""
echo "Building native GDExtension library..."
cd "${ADDON_DIR}"
GODOT_CPP_PATH="${GODOT_CPP_PATH}" scons -j"${JOBS}" 2>&1 | tail -5
BUILD_STATUS=${PIPESTATUS[0]}

if [ ${BUILD_STATUS} -ne 0 ]; then
    echo ""
    echo "ERROR: Build failed. Check the output above."
    exit 1
fi

echo ""
echo "Build successful!"

# ── Copy GDScript wrapper (amy.gd) ──────────────────────────────
echo "Copying GDScript files..."
cat > "${ADDON_DIR}/amy.gd" << 'GDEOF'
class_name Amy
extends Node
## AMY Synthesizer for Godot.
##
## High-level GDScript API that mirrors AMY's Python interface.
## Works on native (GDExtension) and web (WASM via JavaScriptBridge).
##
## Usage:
##   var amy := Amy.new()
##   add_child(amy)
##   await get_tree().process_frame
##   amy.send({"osc": 0, "wave": Amy.SINE, "freq": 440, "vel": 1.0})
##
## Or use wire protocol directly:
##   amy.send_raw("v0w0f440l1")

# ============================================================
#  Wave types
# ============================================================
const SINE: int = 0
const PULSE: int = 1
const SAW_DOWN: int = 2
const SAW_UP: int = 3
const TRIANGLE: int = 4
const NOISE: int = 5
const KS: int = 6
const PCM: int = 7
const ALGO: int = 8
const PARTIAL: int = 9
const BYO_PARTIALS: int = 10
const INTERP_PARTIALS: int = 11
const AUDIO_IN0: int = 12
const AUDIO_IN1: int = 13
const WAVETABLE: int = 19
const CUSTOM: int = 20
const WAVE_OFF: int = 21

# ============================================================
#  Filter types
# ============================================================
const FILTER_NONE: int = 0
const FILTER_LPF: int = 1
const FILTER_BPF: int = 2
const FILTER_HPF: int = 3
const FILTER_LPF24: int = 4

# ============================================================
#  Envelope types
# ============================================================
const ENVELOPE_NORMAL: int = 0
const ENVELOPE_LINEAR: int = 1
const ENVELOPE_DX7: int = 2
const ENVELOPE_TRUE_EXPONENTIAL: int = 3

# ============================================================
#  Config — set these before adding to the tree (before _ready)
# ============================================================
## Enable chorus effect.
@export var chorus: bool = true
## Enable reverb effect.
@export var reverb: bool = true
## Enable echo/delay effect.
@export var echo: bool = true
## Load default GM synth patches on startup.
@export var default_synths: bool = false
## Enable partial synthesis.
@export var partials: bool = true
## Enable custom oscillator type.
@export var custom: bool = true
## Play a short bleep on startup.
@export var startup_bleep: bool = false
## Enable audio input.
@export var audio_in: bool = false
## Maximum number of oscillators.
@export var max_oscs: int = 180
## Maximum number of voices.
@export var max_voices: int = 64
## Maximum number of synths.
@export var max_synths: int = 64

# ============================================================
#  Internals — audio bridge
# ============================================================
var _synth: Node = null
var _stream_player: AudioStreamPlayer = null
var _playback: AudioStreamGeneratorPlayback = null
var _started: bool = false
var _is_web: bool = false

func _ready() -> void:
	_is_web = OS.get_name() == "Web"
	if _is_web:
		_init_web()
	else:
		_init_native()

func _init_native() -> void:
	if ClassDB.class_exists(&"AmySynth"):
		_synth = ClassDB.instantiate(&"AmySynth")
		add_child(_synth)
	else:
		push_warning("AmySynth GDExtension not loaded — audio disabled")
		return

	# Apply config before starting
	_synth.set("chorus", chorus)
	_synth.set("reverb", reverb)
	_synth.set("echo", echo)
	_synth.set("default_synths", default_synths)
	_synth.set("partials", partials)
	_synth.set("custom", custom)
	_synth.set("startup_bleep", startup_bleep)
	_synth.set("audio_in", audio_in)
	_synth.set("max_oscs", max_oscs)
	_synth.set("max_voices", max_voices)
	_synth.set("max_synths", max_synths)

	_stream_player = AudioStreamPlayer.new()
	var stream := AudioStreamGenerator.new()
	stream.mix_rate = 44100.0
	stream.buffer_length = 0.1
	_stream_player.stream = stream
	_stream_player.bus = "Master"
	add_child(_stream_player)

	_synth.call("start")
	_stream_player.play()
	_playback = _stream_player.get_stream_playback() as AudioStreamGeneratorPlayback
	_started = true

func _init_web() -> void:
	for i in range(100):
		var ready: Variant = JavaScriptBridge.eval("godot_amy_is_ready()", true)
		if ready:
			_started = true
			print("AMY web synth ready")
			return
		await get_tree().create_timer(0.1).timeout
	push_warning("AMY web module failed to load after 10 s")

func _process(_delta: float) -> void:
	if _started and not _is_web:
		_fill_audio()

func _fill_audio() -> void:
	if _playback == null or _synth == null:
		return
	var block_size: Variant = _synth.call("get_block_size")
	var bs: int = block_size as int
	while _playback.can_push_buffer(bs):
		var buffer: Variant = _synth.call("fill_buffer")
		_playback.push_buffer(buffer as PackedVector2Array)

func _exit_tree() -> void:
	if not _is_web and _synth:
		_synth.call("stop")

# ============================================================
#  Public API
# ============================================================

## Send an AMY message using keyword-style parameters.
## Example:
##   amy.send({"osc": 0, "wave": Amy.SINE, "freq": 440, "vel": 1})
##   amy.send({"osc": 0, "vel": 0})  # note off
func send(params: Dictionary = {}) -> void:
	send_raw(message(params))

## Build an AMY wire message from a dictionary without sending it.
## Returns the wire string (e.g. "v0w0f440l1").
func message(params: Dictionary) -> String:
	var wire: String = ""
	for key: Variant in params:
		var key_str: String = str(key)
		if not _KW_MAP.has(key_str):
			push_warning("AMY: unknown parameter '%s'" % key_str)
			continue
		var mapping: Variant = _KW_MAP[key_str]
		var m: Array = mapping as Array
		var code: String = str(m[0])
		var type: String = str(m[1])
		var val: Variant = params[key]
		wire += code + _format_value(val, type)
	return wire

## Send a raw AMY wire-protocol string.
## Example:  amy.send_raw("v0w0f440l1")
func send_raw(msg: String) -> void:
	if not _started or msg.is_empty():
		return
	if _is_web:
		var safe: String = msg.replace("\\", "\\\\").replace("'", "\\'")
		JavaScriptBridge.eval("godot_amy_send('%s')" % safe)
	else:
		if _synth:
			_synth.call("send", msg)

## Stop all sound immediately.
func panic() -> void:
	send_raw("S")

# ============================================================
#  Value formatting helpers
# ============================================================
func _format_value(val: Variant, type: String) -> String:
	match type:
		"I":
			return str(int(val))
		"F":
			return _trunc(float(val))
		"S":
			return str(val)
		"L":
			return _format_list(val)
		"C":
			return _format_ctrl(val)
	return str(val)

## Truncate a float to 4 decimal places, strip trailing zeros.
func _trunc(f: float) -> String:
	if f == int(f):
		return str(int(f))
	var s: String = "%.4f" % f
	while s.ends_with("0"):
		s = s.substr(0, s.length() - 1)
	if s.ends_with("."):
		s = s.substr(0, s.length() - 1)
	return s

## Format a list/array as comma-separated values.
func _format_list(val: Variant) -> String:
	if val is String:
		return str(val)
	if val is Array:
		var parts: PackedStringArray = PackedStringArray()
		for item: Variant in val:
			parts.append(_trunc(float(item)))
		return ",".join(parts)
	return str(val)

## Format a control coefficient value.
## Can be a number (treated as const), a string, or an array.
func _format_ctrl(val: Variant) -> String:
	if val is float or val is int:
		return _trunc(float(val))
	if val is String:
		return str(val)
	if val is Array:
		var parts: PackedStringArray = PackedStringArray()
		for item: Variant in val:
			parts.append(_trunc(float(item)))
		return ",".join(parts)
	return str(val)

# ============================================================
#  Parameter → wire-code mapping (matches AMY Python API)
# ============================================================
var _KW_MAP: Dictionary = {
	"osc":              ["v", "I"],
	"wave":             ["w", "I"],
	"note":             ["n", "F"],
	"vel":              ["l", "F"],
	"amp":              ["a", "C"],
	"freq":             ["f", "C"],
	"duty":             ["d", "C"],
	"feedback":         ["b", "F"],
	"time":             ["t", "I"],
	"reset":            ["S", "I"],
	"phase":            ["P", "F"],
	"pan":              ["Q", "C"],
	"client":           ["g", "I"],
	"volume":           ["V", "F"],
	"pitch_bend":       ["s", "F"],
	"filter_freq":      ["F", "C"],
	"resonance":        ["R", "F"],
	"bp0":              ["A", "L"],
	"bp1":              ["B", "L"],
	"eg0_type":         ["T", "I"],
	"eg1_type":         ["X", "I"],
	"debug":            ["D", "I"],
	"chained_osc":      ["c", "I"],
	"mod_source":       ["L", "I"],
	"eq":               ["x", "L"],
	"filter_type":      ["G", "I"],
	"ratio":            ["I", "F"],
	"latency_ms":       ["N", "I"],
	"algo_source":      ["O", "L"],
	"algorithm":        ["o", "I"],
	"chorus":           ["k", "L"],
	"reverb":           ["h", "L"],
	"echo":             ["M", "L"],
	"patch":            ["K", "I"],
	"voices":           ["r", "L"],
	"external_channel": ["W", "I"],
	"portamento":       ["m", "I"],
	"sequence":         ["H", "L"],
	"tempo":            ["j", "F"],
	"synth":            ["i", "I"],
	"num_voices":       ["iv", "I"],
	"preset":           ["p", "I"],
	"num_partials":     ["p", "I"],
}
GDEOF

# ── Copy web export files ────────────────────────────────────────
echo "Copying web export files..."
WEB_SRC="${AMY_DIR}/docs"
cp "${WEB_SRC}/amy.js"    "${ADDON_DIR}/web/"
cp "${WEB_SRC}/amy.wasm"  "${ADDON_DIR}/web/"
cp "${WEB_SRC}/amy.aw.js" "${ADDON_DIR}/web/"
cp "${WEB_SRC}/amy.ww.js" "${ADDON_DIR}/web/"
cp "${WEB_SRC}/enable-threads.js" "${ADDON_DIR}/web/"

# A copy for project root (service worker needs root scope)
cp "${WEB_SRC}/enable-threads.js" "${ADDON_DIR}/web/enable-threads-root.js"

# Godot<->AMY bridge
cat > "${ADDON_DIR}/web/godot_amy_bridge.js" << 'BRIDGEEOF'
// godot_amy_bridge.js
// Thin bridge between Godot's JavaScriptBridge and AMY's WASM module.
// AMY handles its own audio output via AudioWorklet on web —
// this bridge just provides init/send functions for GDScript to call.

(function() {
    var _audio_started = false;

    // Start AMY's AudioWorklet (must be called from user gesture context)
    async function _try_start_audio() {
        if (_audio_started) return;
        if (typeof amy_live_start_web !== 'function') return;
        try {
            await amy_live_start_web();
            _audio_started = true;
            console.log("[AMY Bridge] Audio started via user gesture");
            // Remove listeners once started
            document.removeEventListener('click', _try_start_audio);
            document.removeEventListener('keydown', _try_start_audio);
            document.removeEventListener('touchstart', _try_start_audio);
        } catch(e) {
            console.error("[AMY Bridge] Audio start failed:", e);
        }
    }

    // Register for user gesture events (Web Audio API requirement)
    document.addEventListener('click', _try_start_audio);
    document.addEventListener('keydown', _try_start_audio);
    document.addEventListener('touchstart', _try_start_audio);

    // -- Functions called from GDScript via JavaScriptBridge.eval() --

    // Check if AMY WASM module is initialized and ready
    window.godot_amy_is_ready = function() {
        return typeof amy_add_message === 'function';
    };

    // Check if AudioWorklet is running
    window.godot_amy_audio_started = function() {
        return _audio_started;
    };

    // Send a wire message to AMY (e.g. "v0w0f440l1")
    window.godot_amy_send = function(msg) {
        if (typeof amy_add_message === 'function') {
            amy_add_message(msg);
        }
    };
})();
BRIDGEEOF

# Custom HTML shell for web export
cat > "${ADDON_DIR}/web/custom_shell.html" << 'SHELLEOF'
<!DOCTYPE html>
<html lang="en">
	<head>
		<meta charset="utf-8">
		<meta name="viewport" content="width=device-width, user-scalable=no, initial-scale=1.0">
		<title>$GODOT_PROJECT_NAME</title>
		<style>
html, body, #canvas {
	margin: 0;
	padding: 0;
	border: 0;
}

body {
	color: white;
	background-color: black;
	overflow: hidden;
	touch-action: none;
}

#canvas {
	display: block;
}

#canvas:focus {
	outline: none;
}

#status, #status-splash, #status-progress {
	position: absolute;
	left: 0;
	right: 0;
}

#status, #status-splash {
	top: 0;
	bottom: 0;
}

#status {
	background-color: $GODOT_SPLASH_COLOR;
	display: flex;
	flex-direction: column;
	justify-content: center;
	align-items: center;
	visibility: hidden;
}

#status-splash {
	max-height: 100%;
	max-width: 100%;
	margin: auto;
}

#status-splash.show-image--false {
	display: none;
}

#status-splash.fullsize--true {
	height: 100%;
	width: 100%;
	object-fit: contain;
}

#status-splash.use-filter--false {
	image-rendering: pixelated;
}

#status-progress, #status-notice {
	display: none;
}

#status-progress {
	bottom: 10%;
	width: 50%;
	margin: 0 auto;
}

#status-notice {
	background-color: #5b3943;
	border-radius: 0.5rem;
	border: 1px solid #9b3943;
	color: #e0e0e0;
	font-family: 'Noto Sans', 'Droid Sans', Arial, sans-serif;
	line-height: 1.3;
	margin: 0 2rem;
	overflow: hidden;
	padding: 1rem;
	text-align: center;
	z-index: 1;
}
		</style>
		<!-- AMY Synthesizer: COOP/COEP service worker for SharedArrayBuffer -->
		<script src="enable-threads.js"></script>
		<!-- AMY Synthesizer: WASM module + AudioWorklet audio engine -->
		<script src="web_audio/amy.js"></script>
		<!-- AMY Synthesizer: Godot<->AMY bridge (auto-starts audio on user gesture) -->
		<script src="web_audio/godot_amy_bridge.js"></script>
		$GODOT_HEAD_INCLUDE
	</head>
	<body>
		<canvas id="canvas">
			Your browser does not support the canvas tag.
		</canvas>

		<noscript>
			Your browser does not support JavaScript.
		</noscript>

		<div id="status">
			<img id="status-splash" class="$GODOT_SPLASH_CLASSES" src="$GODOT_SPLASH" alt="">
			<progress id="status-progress"></progress>
			<div id="status-notice"></div>
		</div>

		<script src="$GODOT_URL"></script>
		<script>
const GODOT_CONFIG = $GODOT_CONFIG;
const GODOT_THREADS_ENABLED = $GODOT_THREADS_ENABLED;
const engine = new Engine(GODOT_CONFIG);

(function () {
	const statusOverlay = document.getElementById('status');
	const statusProgress = document.getElementById('status-progress');
	const statusNotice = document.getElementById('status-notice');

	let initializing = true;
	let statusMode = '';

	function setStatusMode(mode) {
		if (statusMode === mode || !initializing) {
			return;
		}
		if (mode === 'hidden') {
			statusOverlay.remove();
			initializing = false;
			return;
		}
		statusOverlay.style.visibility = 'visible';
		statusProgress.style.display = mode === 'progress' ? 'block' : 'none';
		statusNotice.style.display = mode === 'notice' ? 'block' : 'none';
		statusMode = mode;
	}

	function setStatusNotice(text) {
		while (statusNotice.lastChild) {
			statusNotice.removeChild(statusNotice.lastChild);
		}
		const lines = text.split('\n');
		lines.forEach((line) => {
			statusNotice.appendChild(document.createTextNode(line));
			statusNotice.appendChild(document.createElement('br'));
		});
	}

	function displayFailureNotice(err) {
		console.error(err);
		if (err instanceof Error) {
			setStatusNotice(err.message);
		} else if (typeof err === 'string') {
			setStatusNotice(err);
		} else {
			setStatusNotice('An unknown error occurred.');
		}
		setStatusMode('notice');
		initializing = false;
	}

	const missing = Engine.getMissingFeatures({
		threads: GODOT_THREADS_ENABLED,
	});

	if (missing.length !== 0) {
		if (GODOT_CONFIG['serviceWorker'] && GODOT_CONFIG['ensureCrossOriginIsolationHeaders'] && 'serviceWorker' in navigator) {
			let serviceWorkerRegistrationPromise;
			try {
				serviceWorkerRegistrationPromise = navigator.serviceWorker.getRegistration();
			} catch (err) {
				serviceWorkerRegistrationPromise = Promise.reject(new Error('Service worker registration failed.'));
			}
			Promise.race([
				serviceWorkerRegistrationPromise.then((registration) => {
					if (registration != null) {
						return Promise.reject(new Error('Service worker already exists.'));
					}
					return registration;
				}).then(() => engine.installServiceWorker()),
				new Promise((resolve) => {
					setTimeout(() => resolve(), 2000);
				}),
			]).then(() => {
				window.location.reload();
			}).catch((err) => {
				console.error('Error while registering service worker:', err);
			});
		} else {
			const missingMsg = 'Error\nThe following features required to run Godot projects on the Web are missing:\n';
			displayFailureNotice(missingMsg + missing.join('\n'));
		}
	} else {
		setStatusMode('progress');
		engine.startGame({
			'onProgress': function (current, total) {
				if (current > 0 && total > 0) {
					statusProgress.value = current;
					statusProgress.max = total;
				} else {
					statusProgress.removeAttribute('value');
					statusProgress.removeAttribute('max');
				}
			},
		}).then(() => {
			setStatusMode('hidden');
		}, displayFailureNotice);
	}
}());
		</script>
	</body>
</html>
SHELLEOF

# ── Copy install.gd editor script ───────────────────────────────
echo "Copying install.gd..."
cat > "${ADDON_DIR}/install.gd" << 'INSTALLEOF'
@tool
extends EditorScript
## AMY Synthesizer Addon - Web Export Setup
##
## Run this script once (via File > Run) to copy web export files
## to the correct locations in your project.
##
## It copies:
##   - enable-threads.js  → project root (service worker needs root scope)
##   - web_audio/          → project root (referenced by custom HTML shell)
##   - Sets up export preset if one doesn't exist

func _run() -> void:
	var addon_dir: String = "res://addons/amy/web/"
	var dst_web: String = "res://web_audio/"
	var dst_root: String = "res://"

	print("=== AMY Web Export Setup ===")

	# Copy enable-threads.js to project root
	_copy_file(addon_dir + "enable-threads-root.js", dst_root + "enable-threads.js")

	# Copy web audio files
	var web_files: Array[String] = [
		"amy.js", "amy.wasm", "amy.aw.js", "amy.ww.js",
		"godot_amy_bridge.js", "enable-threads.js"
	]

	# Create web_audio directory
	DirAccess.make_dir_absolute(ProjectSettings.globalize_path(dst_web))

	# Create .gdignore in web_audio
	var gdignore: FileAccess = FileAccess.open(dst_web + ".gdignore", FileAccess.WRITE)
	if gdignore:
		gdignore.close()
		print("  Created web_audio/.gdignore")

	for fname in web_files:
		_copy_file(addon_dir + fname, dst_web + fname)

	# Copy custom HTML shell to export/
	DirAccess.make_dir_absolute(ProjectSettings.globalize_path("res://export/"))
	_copy_file(addon_dir + "custom_shell.html", "res://export/custom_shell.html")

	print("")
	print("=== Setup Complete ===")
	print("")
	print("Next steps:")
	print("  1. In Export dialog, set Custom HTML Shell to: res://export/custom_shell.html")
	print("  2. Set Exclude Filter to: addons/amy/bin/*,addons/amy/src/*,addons/amy/amy_src/*,addons/amy/web/*,addons/amy/SConstruct,addons/amy/install.gd,addons/amy/amy.gdextension")
	print("  3. Export for Web as usual!")
	print("")

func _copy_file(src: String, dst: String) -> void:
	var src_path: String = ProjectSettings.globalize_path(src)
	var dst_path: String = ProjectSettings.globalize_path(dst)

	var file_in: FileAccess = FileAccess.open(src, FileAccess.READ)
	if file_in == null:
		push_warning("Could not read: " + src)
		return

	var content: PackedByteArray = file_in.get_buffer(file_in.get_length())
	file_in.close()

	var file_out: FileAccess = FileAccess.open(dst, FileAccess.WRITE)
	if file_out == null:
		push_warning("Could not write: " + dst)
		return

	file_out.store_buffer(content)
	file_out.close()
	print("  Copied: " + src.get_file() + " -> " + dst)
INSTALLEOF

# ── Clean up build artifacts from addon ──────────────────────────
echo "Cleaning build artifacts..."
find "${ADDON_DIR}/amy_src" -name "*.os" -delete 2>/dev/null || true
find "${ADDON_DIR}/src" -name "*.os" -delete 2>/dev/null || true

# ── Done ─────────────────────────────────────────────────────────
echo ""
echo "=== Done! ==="
echo ""
echo "The AMY addon has been installed to: ${ADDON_DIR}"
echo ""
echo "Next steps:"
echo "  1. Open the project in the Godot editor"
echo "  2. Use Amy in your scripts:"
echo "       var amy := Amy.new()"
echo "       add_child(amy)"
echo "       await get_tree().process_frame"
echo "       amy.send({\"osc\": 0, \"wave\": Amy.SINE, \"freq\": 440, \"vel\": 1.0})"
echo ""
echo "  For web export setup, see: https://github.com/shorepine/amy/blob/main/docs/godot.md"
