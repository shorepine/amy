// interp_partials - AMY kernel-side implementation of the interpolated partials-based synthesis originally implemented in tulip_piano.py.

#include "amy.h"

typedef struct {
    // How many sample_times_ms are there?
    uint16_t num_sample_times_ms;
    // Pointer to an array of the sample_times_ms
    const uint16_t *sample_times_ms;
    // How many velocities are defined for this voice (same for all notes)
    uint16_t num_velocities;
    // Pointer to a array of the MIDI velocities.
    const uint8_t *velocities;
    // How many different pitches do we define?  (All velocities are provided for each)
    uint16_t num_pitches;
    // Pointer to array of structures defining each note (pitch + velocity) entry.
    const uint8_t *pitches;
    // How many harmonics are allocated for each of the num_velocities * num_pitches notes.
    const uint8_t *num_harmonics;
    // MIDI Cents freqs for each harmonic.
    const uint16_t *harmonics_freq;
    // num_sample_times_ms uint8_t dB envelope values for each harmonic.
    const uint8_t *harmonics_mags;
} interp_partials_voice_t;

#include "interp_partials.h"

#define MAX_NUM_MAGNITUDES 24

void _cumulate_scaled_harmonic_params(float *harm_param, int harmonic_index, float alpha, const interp_partials_voice_t *partials_voice) {
    int num_bps = partials_voice->num_sample_times_ms;
    // Pitch
    harm_param[0] += alpha * partials_voice->harmonics_freq[harmonic_index];
    // Envelope magnitudes
    for (int i = 0; i < num_bps; ++i)
        harm_param[1 + i] += alpha * partials_voice->harmonics_mags[harmonic_index * num_bps + i];
}

int _harmonic_base_index_for_pitch_vel(int pitch_index, int vel_index, const interp_partials_voice_t *partials_voice) {
    int note_number = partials_voice->num_velocities * pitch_index + vel_index;
    int harmonic_index = 0;
    for (int i = 0; i < note_number; ++i)
        harmonic_index += partials_voice->num_harmonics[i];
    return harmonic_index;
}

float _logfreq_of_midi_cents(float midi_cents) {
    // Frequency is already log scaled, but need to re-center and change from 1200/oct to 1.0/oct.
    return (midi_cents - (100 * ZERO_MIDI_NOTE)) / 1200.f;
}

float _env_lin_of_db(float db) {
    float lin =  powf(10.f, (db - 100) / 20.f) - 0.001;
    if (lin < 0)  return 0;
    return lin;
}

void _osc_on_with_harm_param(uint16_t o, float *harm_param, const interp_partials_voice_t *partials_voice) {
    // We coerce this voice into being a partial, regardless of user wishes.
    synth[o].wave = PARTIAL;
    synth[o].patch = -1;  // Flag that this is an envelope-based partial
    // Setup the specified frequency.
    synth[o].logfreq_coefs[COEF_CONST] = _logfreq_of_midi_cents(harm_param[0]);
    // Setup envelope.
    synth[o].breakpoint_times[0][0] = 0;
    synth[o].breakpoint_values[0][0] = 0;
    int last_time = 0;
    for (int bp = 0; bp < partials_voice->num_sample_times_ms; ++bp) {
        synth[o].breakpoint_times[0][bp + 1] = (partials_voice->sample_times_ms[bp] - last_time) * AMY_SAMPLE_RATE / 1000;
        synth[o].breakpoint_values[0][bp + 1] = _env_lin_of_db(harm_param[bp + 1]);
        last_time = partials_voice->sample_times_ms[bp];
    }
    // Final release
    synth[o].breakpoint_times[0][partials_voice->num_sample_times_ms + 1] = 200 * AMY_SAMPLE_RATE / 1000;
    synth[o].breakpoint_values[0][partials_voice->num_sample_times_ms + 1] = 0;
    // Decouple osc freq and amp from note and amp.
    synth[o].logfreq_coefs[COEF_NOTE] = 0;
    synth[o].amp_coefs[COEF_VEL] = 0;
    // Other osc params.
    synth[o].status = SYNTH_IS_ALGO_SOURCE;
    synth[o].note_on_clock = total_samples;
    AMY_UNSET(synth[o].note_off_clock);
    partial_note_on(o);
}

void interp_partials_note_on(uint16_t osc) {
    // Choose the interp_partials patch.
    const interp_partials_voice_t *partials_voice = &interp_partials_map[synth[osc].patch % NUM_INTERP_PARTIALS_PATCHES];
    float midi_note = synth[osc].midi_note;
    float midi_vel = (int)roundf(synth[osc].velocity * 127.f);
    // Clip
    if (midi_vel < partials_voice->velocities[0]) midi_vel = partials_voice->velocities[0];
    if (midi_vel > partials_voice->velocities[partials_voice->num_velocities - 1]) midi_vel = partials_voice->velocities[partials_voice->num_velocities - 1];
    // Find the lower bound pitch/velocity indices.
    uint8_t pitch_index = 0, vel_index = 0;
    while(pitch_index < partials_voice->num_pitches - 1
          && partials_voice->pitches[pitch_index + 1] < midi_note)
        ++pitch_index;
    while(vel_index < partials_voice->num_velocities - 1
          && partials_voice->velocities[vel_index + 1] < midi_vel)
        ++vel_index;
    // Interp weights
    float pitch_alpha = (midi_note - partials_voice->pitches[pitch_index])
        / (float)(partials_voice->pitches[pitch_index + 1] - partials_voice->pitches[pitch_index]);
    float vel_alpha = (midi_vel - partials_voice->velocities[vel_index])
        / (float)(partials_voice->velocities[vel_index + 1] - partials_voice->velocities[vel_index]);
    float harm_param[MAX_NUM_MAGNITUDES + 1];  // frequency + harmonic magnitudes.
    int note_number = partials_voice->num_velocities * pitch_index + vel_index;
    int num_harmonics = partials_voice->num_harmonics[note_number];
    // Interpolate the 4 notes.
    int harmonic_base_index_pl_vl =
        _harmonic_base_index_for_pitch_vel(pitch_index, vel_index, partials_voice);
    float alpha_pl_vl = (1.f - pitch_alpha) * (1.f - vel_alpha);
    int harmonic_base_index_pl_vh =
        _harmonic_base_index_for_pitch_vel(pitch_index, vel_index + 1, partials_voice);
    float alpha_pl_vh = (1.f - pitch_alpha) * (vel_alpha);
    int harmonic_base_index_ph_vl =
        _harmonic_base_index_for_pitch_vel(pitch_index + 1, vel_index, partials_voice);
    float alpha_ph_vl = (pitch_alpha) * (1.f - vel_alpha);
    int harmonic_base_index_ph_vh =
        _harmonic_base_index_for_pitch_vel(pitch_index + 1, vel_index + 1, partials_voice);
    float alpha_ph_vh = (pitch_alpha) * (vel_alpha);
    //fprintf(stderr, "interp_partials@%u: osc %d note %d vel %d pitch_x %d vel_x %d numh %d harm_bi_ll %d pitch_a %.3f vel_a %.3f alphas %.2f %.2f %.2f %.2f\n",
    //        total_samples, osc, midi_note, midi_vel, pitch_index, vel_index, num_harmonics, harmonic_base_index_pl_vl, pitch_alpha, vel_alpha,
    //        alpha_pl_vl, alpha_pl_vh, alpha_ph_vl, alpha_ph_vh);
    for (int h = 0; h < num_harmonics; ++h) {
        for (int i = 0; i < MAX_NUM_MAGNITUDES + 1; ++i)  harm_param[i] = 0;
        _cumulate_scaled_harmonic_params(harm_param, harmonic_base_index_pl_vl + h,
                                         alpha_pl_vl, partials_voice);
        _cumulate_scaled_harmonic_params(harm_param, harmonic_base_index_pl_vh + h,
                                         alpha_pl_vh, partials_voice);
        _cumulate_scaled_harmonic_params(harm_param, harmonic_base_index_ph_vl + h,
                                         alpha_ph_vl, partials_voice);
        _cumulate_scaled_harmonic_params(harm_param, harmonic_base_index_ph_vh + h,
                                         alpha_ph_vh, partials_voice);
        //fprintf(stderr, "harm %d freq %.2f bps %.3f %.3f %.3f %.3f\n", h, harm_param[0], harm_param[1], harm_param[2], harm_param[3], harm_param[4]);
        _osc_on_with_harm_param(osc + 1 + h, harm_param, partials_voice);
    }
}

void interp_partials_note_off(uint16_t osc) {
    const interp_partials_voice_t *partials_voice = &interp_partials_map[synth[osc].patch % NUM_INTERP_PARTIALS_PATCHES];
    int num_oscs = partials_voice->num_harmonics[0];   // Assume first patch has the max #harmonics.
    for(uint16_t i = osc + 1; i < osc + 1 + num_oscs; i++) {
        uint16_t o = i % AMY_OSCS;
        AMY_UNSET(synth[o].note_on_clock);
        synth[o].note_off_clock = total_samples;
    }
}