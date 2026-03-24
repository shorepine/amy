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
	# Pass config to JS bridge before AMY starts
	var ds := "true" if default_synths else "false"
	var sb := "true" if startup_bleep else "false"
	JavaScriptBridge.eval("godot_amy_configure(%s, %s)" % [ds, sb])

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
