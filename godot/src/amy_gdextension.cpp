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
