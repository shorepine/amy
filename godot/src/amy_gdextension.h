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
