// This file is only for web-amy, but arduino will try to compile it anyway.
#ifndef ARDUINO

#include <emscripten/bind.h>
#include <string>
#include <vector>
#include <cstring> // For strncpy, memchr
#include <algorithm> // For std::min
#include <cstdio> // For fprintf if used in setters

// Ensure C linkage for amy.h contents
extern "C" {
#include "amy.h"
}

using namespace emscripten;

// --- Helper Getters and Setters for amy_event fields ---

// Existing bp0/bp1 helpers (made static for clarity within this file)
static std::string get_amy_event_bp_field(const char* bp_field_ptr) {
    const char* end = (const char*)memchr(bp_field_ptr, '\0', MAX_PARAM_LEN);
    size_t len = (end) ? (end - bp_field_ptr) : MAX_PARAM_LEN;
    return std::string(bp_field_ptr, len);
}

static void set_amy_event_bp_field(char* bp_field_ptr, const std::string& str) {
    strncpy(bp_field_ptr, str.c_str(), MAX_PARAM_LEN - 1);
    bp_field_ptr[MAX_PARAM_LEN - 1] = '\0';
}

static std::string get_bp0(const amy_event& e) { return get_amy_event_bp_field(e.bp0); }
static void set_bp0(amy_event& e, const std::string& str) { set_amy_event_bp_field(e.bp0, str); }
static std::string get_bp1(const amy_event& e) { return get_amy_event_bp_field(e.bp1); }
static void set_bp1(amy_event& e, const std::string& str) { set_amy_event_bp_field(e.bp1, str); }

// Scalar fields
static uint32_t get_time(const amy_event& e) { return e.time; }
static void set_time(amy_event& e, uint32_t val) { e.time = val; }

static uint16_t get_osc(const amy_event& e) { return e.osc; }
static void set_osc(amy_event& e, uint16_t val) { e.osc = val; }

static uint16_t get_wave(const amy_event& e) { return e.wave; }
static void set_wave(amy_event& e, uint16_t val) { e.wave = val; }

static int16_t get_preset(const amy_event& e) { return e.preset; }
static void set_preset(amy_event& e, int16_t val) { e.preset = val; }

static float get_midi_note(const amy_event& e) { return e.midi_note; }
static void set_midi_note(amy_event& e, float val) { e.midi_note = val; }

static uint16_t get_patch_number(const amy_event& e) { return e.patch_number; }
static void set_patch_number(amy_event& e, uint16_t val) { e.patch_number = val; }

static float get_feedback(const amy_event& e) { return e.feedback; }
static void set_feedback(amy_event& e, float val) { e.feedback = val; }

static float get_velocity(const amy_event& e) { return e.velocity; }
static void set_velocity(amy_event& e, float val) { e.velocity = val; }

static float get_phase(const amy_event& e) { return e.phase; }
static void set_phase(amy_event& e, float val) { e.phase = val; }

static float get_detune(const amy_event& e) { return e.detune; }
static void set_detune(amy_event& e, float val) { e.detune = val; }

static float get_volume(const amy_event& e) { return e.volume; }
static void set_volume(amy_event& e, float val) { e.volume = val; }

static float get_pitch_bend(const amy_event& e) { return e.pitch_bend; }
static void set_pitch_bend(amy_event& e, float val) { e.pitch_bend = val; }

static float get_tempo(const amy_event& e) { return e.tempo; }
static void set_tempo(amy_event& e, float val) { e.tempo = val; }

static uint16_t get_latency_ms(const amy_event& e) { return e.latency_ms; }
static void set_latency_ms(amy_event& e, uint16_t val) { e.latency_ms = val; }

static float get_ratio(const amy_event& e) { return e.ratio; }
static void set_ratio(amy_event& e, float val) { e.ratio = val; }

static float get_resonance(const amy_event& e) { return e.resonance; }
static void set_resonance(amy_event& e, float val) { e.resonance = val; }

static uint16_t get_portamento_ms(const amy_event& e) { return e.portamento_ms; }
static void set_portamento_ms(amy_event& e, uint16_t val) { e.portamento_ms = val; }

static uint16_t get_chained_osc(const amy_event& e) { return e.chained_osc; }
static void set_chained_osc(amy_event& e, uint16_t val) { e.chained_osc = val; }

static uint16_t get_mod_source(const amy_event& e) { return e.mod_source; }
static void set_mod_source(amy_event& e, uint16_t val) { e.mod_source = val; }

static uint8_t get_algorithm(const amy_event& e) { return e.algorithm; }
static void set_algorithm(amy_event& e, uint8_t val) { e.algorithm = val; }

static uint8_t get_filter_type(const amy_event& e) { return e.filter_type; }
static void set_filter_type(amy_event& e, uint8_t val) { e.filter_type = val; }

static float get_eq_l(const amy_event& e) { return e.eq_l; }
static void set_eq_l(amy_event& e, float val) { e.eq_l = val; }

static float get_eq_m(const amy_event& e) { return e.eq_m; }
static void set_eq_m(amy_event& e, float val) { e.eq_m = val; }

static float get_eq_h(const amy_event& e) { return e.eq_h; }
static void set_eq_h(amy_event& e, float val) { e.eq_h = val; }

static uint8_t get_synth(const amy_event& e) { return e.synth; }
static void set_synth(amy_event& e, uint8_t val) { e.synth = val; }

static uint32_t get_synth_flags(const amy_event& e) { return e.synth_flags; }
static void set_synth_flags(amy_event& e, uint32_t val) { e.synth_flags = val; }

static uint8_t get_to_synth(const amy_event& e) { return e.to_synth; }
static void set_to_synth(amy_event& e, uint8_t val) { e.to_synth = val; }

static uint8_t get_grab_midi_notes(const amy_event& e) { return e.grab_midi_notes; }
static void set_grab_midi_notes(amy_event& e, uint8_t val) { e.grab_midi_notes = val; }

static uint8_t get_pedal(const amy_event& e) { return e.pedal; }
static void set_pedal(amy_event& e, uint8_t val) { e.pedal = val; }

static uint16_t get_num_voices(const amy_event& e) { return e.num_voices; }
static void set_num_voices(amy_event& e, uint16_t val) { e.num_voices = val; }

static uint8_t get_status(const amy_event& e) { return e.status; }
static void set_status(amy_event& e, uint8_t val) { e.status = val; }

static uint8_t get_note_source(const amy_event& e) { return e.note_source; }
static void set_note_source(amy_event& e, uint8_t val) { e.note_source = val; }

static uint32_t get_reset_osc(const amy_event& e) { return e.reset_osc; }
static void set_reset_osc(amy_event& e, uint32_t val) { e.reset_osc = val; }


// Array fields - Generic helper for typed_memory_view getter
template<typename T, size_t N>
static emscripten::val get_array_field(const T (&array_member)[N]) {
    return emscripten::val(emscripten::typed_memory_view(N, array_member));
}

// Array fields - Generic helper for vecFromJSArray setter
template<typename T, size_t N>
static void set_array_field(T (&array_member)[N], const emscripten::val& js_array_val, const char* field_name) {
    if (!js_array_val.isArray()) {
        fprintf(stderr, "Error: %s expects an array.\\n", field_name);
        // Or: throw emscripten::binding_error((std::string(field_name) + " expects an array.").c_str());
        return;
    }
    std::vector<T> v = emscripten::vecFromJSArray<T>(js_array_val);
    for (size_t i = 0; i < N; ++i) {
        if (i < v.size()) {
            array_member[i] = v[i];
        } else {
            // Default fill if JS array is shorter. Adjust as needed.
            array_member[i] = T{}; 
        }
    }
    if (v.size() > N) {
        fprintf(stderr, "Warning: %s received array larger than %zu, truncated.\\n", field_name, N);
    }
}

// Specific array getters/setters using the generic helpers
static emscripten::val get_amp_coefs(const amy_event& e) { return get_array_field<float, NUM_COMBO_COEFS>(e.amp_coefs); }
static void set_amp_coefs(amy_event& e, const emscripten::val& val) { set_array_field<float, NUM_COMBO_COEFS>(e.amp_coefs, val, "amp_coefs"); }

static emscripten::val get_freq_coefs(const amy_event& e) { return get_array_field<float, NUM_COMBO_COEFS>(e.freq_coefs); }
static void set_freq_coefs(amy_event& e, const emscripten::val& val) { set_array_field<float, NUM_COMBO_COEFS>(e.freq_coefs, val, "freq_coefs"); }

static emscripten::val get_filter_freq_coefs(const amy_event& e) { return get_array_field<float, NUM_COMBO_COEFS>(e.filter_freq_coefs); }
static void set_filter_freq_coefs(amy_event& e, const emscripten::val& val) { set_array_field<float, NUM_COMBO_COEFS>(e.filter_freq_coefs, val, "filter_freq_coefs"); }

static emscripten::val get_duty_coefs(const amy_event& e) { return get_array_field<float, NUM_COMBO_COEFS>(e.duty_coefs); }
static void set_duty_coefs(amy_event& e, const emscripten::val& val) { set_array_field<float, NUM_COMBO_COEFS>(e.duty_coefs, val, "duty_coefs"); }

static emscripten::val get_pan_coefs(const amy_event& e) { return get_array_field<float, NUM_COMBO_COEFS>(e.pan_coefs); }
static void set_pan_coefs(amy_event& e, const emscripten::val& val) { set_array_field<float, NUM_COMBO_COEFS>(e.pan_coefs, val, "pan_coefs"); }

static emscripten::val get_bp_is_set(const amy_event& e) { return get_array_field<uint16_t, MAX_BREAKPOINT_SETS>(e.bp_is_set); }
static void set_bp_is_set(amy_event& e, const emscripten::val& val) { set_array_field<uint16_t, MAX_BREAKPOINT_SETS>(e.bp_is_set, val, "bp_is_set"); }

static emscripten::val get_algo_source(const amy_event& e) { return get_array_field<int16_t, MAX_ALGO_OPS>(e.algo_source); }
static void set_algo_source(amy_event& e, const emscripten::val& val) { set_array_field<int16_t, MAX_ALGO_OPS>(e.algo_source, val, "algo_source"); }

static emscripten::val get_voices(const amy_event& e) { return get_array_field<uint16_t, MAX_VOICES_PER_INSTRUMENT>(e.voices); }
static void set_voices(amy_event& e, const emscripten::val& val) { set_array_field<uint16_t, MAX_VOICES_PER_INSTRUMENT>(e.voices, val, "voices"); }

static emscripten::val get_eg_type(const amy_event& e) { return get_array_field<uint8_t, MAX_BREAKPOINT_SETS>(e.eg_type); }
static void set_eg_type(amy_event& e, const emscripten::val& val) { set_array_field<uint8_t, MAX_BREAKPOINT_SETS>(e.eg_type, val, "eg_type"); }

static emscripten::val get_sequence(const amy_event& e) { return get_array_field<uint32_t, 3>(e.sequence); }
static void set_sequence(amy_event& e, const emscripten::val& val) { set_array_field<uint32_t, 3>(e.sequence, val, "sequence"); }


EMSCRIPTEN_BINDINGS(amy_module) {
    class_<amy_event>("amy_event")
        .constructor<>() 
        .property("time", &get_time, &set_time)
        .property("osc", &get_osc, &set_osc)
        .property("wave", &get_wave, &set_wave)
        .property("preset", &get_preset, &set_preset)
        .property("midi_note", &get_midi_note, &set_midi_note)
        .property("patch_number", &get_patch_number, &set_patch_number)
        .property("amp_coefs", &get_amp_coefs, &set_amp_coefs)
        .property("freq_coefs", &get_freq_coefs, &set_freq_coefs)
        .property("filter_freq_coefs", &get_filter_freq_coefs, &set_filter_freq_coefs)
        .property("duty_coefs", &get_duty_coefs, &set_duty_coefs)
        .property("pan_coefs", &get_pan_coefs, &set_pan_coefs)
        .property("feedback", &get_feedback, &set_feedback)
        .property("velocity", &get_velocity, &set_velocity)
        .property("phase", &get_phase, &set_phase)
        .property("detune", &get_detune, &set_detune)
        .property("volume", &get_volume, &set_volume)
        .property("pitch_bend", &get_pitch_bend, &set_pitch_bend)
        .property("tempo", &get_tempo, &set_tempo)
        .property("latency_ms", &get_latency_ms, &set_latency_ms)
        .property("ratio", &get_ratio, &set_ratio)
        .property("resonance", &get_resonance, &set_resonance)
        .property("portamento_ms", &get_portamento_ms, &set_portamento_ms)
        .property("chained_osc", &get_chained_osc, &set_chained_osc)
        .property("mod_source", &get_mod_source, &set_mod_source)
        .property("algorithm", &get_algorithm, &set_algorithm)
        .property("filter_type", &get_filter_type, &set_filter_type)
        .property("eq_l", &get_eq_l, &set_eq_l)
        .property("eq_m", &get_eq_m, &set_eq_m)
        .property("eq_h", &get_eq_h, &set_eq_h)
        .property("bp_is_set", &get_bp_is_set, &set_bp_is_set)
        .property("algo_source", &get_algo_source, &set_algo_source)
        .property("voices", &get_voices, &set_voices)
        .property("bp0", &get_bp0, &set_bp0) 
        .property("bp1", &get_bp1, &set_bp1) 
        .property("eg_type", &get_eg_type, &set_eg_type)
        .property("synth", &get_synth, &set_synth)
        .property("synth_flags", &get_synth_flags, &set_synth_flags)
        .property("to_synth", &get_to_synth, &set_to_synth)
        .property("grab_midi_notes", &get_grab_midi_notes, &set_grab_midi_notes)
        .property("pedal", &get_pedal, &set_pedal)
        .property("num_voices", &get_num_voices, &set_num_voices)
        .property("sequence", &get_sequence, &set_sequence)
        .property("status", &get_status, &set_status)
        .property("note_source", &get_note_source, &set_note_source)
        .property("reset_osc", &get_reset_osc, &set_reset_osc);

    function("amy_default_event", &amy_default_event);
    function("amy_add_event", &amy_add_event, allow_raw_pointers());
    
    // Exposing constants that define array sizes. This is good practice.
    constant("NUM_COMBO_COEFS", NUM_COMBO_COEFS);
    constant("MAX_BREAKPOINT_SETS", MAX_BREAKPOINT_SETS);
    constant("MAX_ALGO_OPS", MAX_ALGO_OPS);
    constant("MAX_VOICES_PER_INSTRUMENT", MAX_VOICES_PER_INSTRUMENT);
    constant("MAX_PARAM_LEN", MAX_PARAM_LEN);
}
#endif // !ARDUINO
