// interp_partials - AMY kernel-side implementation of the interpolated partials-based synthesis originally implemented in tulip_piano.py.

#include "amy.h"
#include <assert.h>
#include <stdbool.h>

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

#define MAX_NUM_HARMONICS 40

// Map to drop out some higher harmonics, namely the 2x and 3x overtones above 16th harmonic
const bool use_this_partial_map[MAX_NUM_HARMONICS] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 1-10
    1, 1, 1, 1, 1, 1, 1, 0, 1, 0,  // 11-20
    0, 0, 1, 0, 1, 0, 0, 0, 1, 0,  // 21-30
    1, 0, 0, 0, 1, 0, 1, 0, 0, 0,  // 31-40
};


// choose a preset from the .h file
void partials_note_on(uint16_t osc) {
    int num_partials = synth[osc]->preset;
    // Never reserve or render past the end of the osc pool (see render_partials).
    if (osc + 1 + num_partials > AMY_OSCS)
        num_partials = (osc + 1 < AMY_OSCS) ? (AMY_OSCS - osc - 1) : 0;
    for (int i = 0; i < num_partials; ++i) {
        int o = osc + 1 + i;
        // On OOM this partial stays silent.
        if (!ensure_osc_allocd(o, NULL)) continue;
        // Mark this PARTIAL as part of a build-your own with a flag value in its preset field.
        // This is used I think only at envelope.c:121 to avoid the normal partial preset special-case for PARTIALs.
        synth[o]->preset = synth[osc]->preset;
        synth[o]->logfreq_coefs[COEF_BEND] = 0;  // Each PARTIAL will receive pitch bend via the midi_note modulation from the parent osc, don't add it twice.
        synth[o]->role = SYNTH_IS_ALGO_SOURCE;
        synth[o]->note_on_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
        AMY_UNSET(synth[o]->note_off_clock);
        msynth[o]->logfreq = synth[o]->logfreq_coefs[COEF_CONST] + msynth[osc]->logfreq;
        partial_note_on(o);
    }
    // Squirrel away num_oscs -- the count we actually reserved, which the pool
    // clamp above may have reduced below preset.
    synth[osc]->last_two[0] = num_partials;
}

void partials_note_off(uint16_t osc) {
    int num_oscs = synth[osc]->preset;
    for(uint16_t i = osc + 1; i < osc + 1 + num_oscs; i++) {
        uint16_t o = i % AMY_OSCS;
        // The partial may have failed to alloc at note-on.
        if (synth[o] == NULL) continue;
        AMY_UNSET(synth[o]->note_on_clock);
        synth[o]->note_off_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
    }
}


static inline float p_combine_controls(float *controls, float *coefs) {
    float result = 0;
    for (int i = 0; i <= COEF_EG0; ++i)
        result += coefs[i] * controls[i];
    return result;
}

static inline float p_amp_combine_controls(float *controls, float *coefs) {
    // Linear combination of amp coefs is then mapped so that 0 -> 0.001 and 1 -> 1 exponentially.
    // For partials, amp_coefs = {const:1, vel:1, eg0:1, rest:0}
    //float result = 0;
    //for (int i = 0; i <= COEF_EG0; ++i)  {
    //    float coef = coefs[i];
    //    // A zero coef contributes nothing (0 * val == 0), so skip map_60db_to_01f
    //    if (coef == 0)  continue;
    //    float val = controls[i];
    //    if (i == COEF_CONST)  {val = coef; coef = 1.0f;}   // coef[CONST] is always 1.0f, so swap them.  We're going to map the val.
    //    if (i != COEF_MOD) {
    //        val = map_60dB_to_01f(MAX(0, val)) - 1.0;    // const, vel, eg0, eg1 get log-compressed.
            // make 0 mean "no amp" and 1 mean "regular (full) amp".
    //    }
    //    result += coef * val;
    //}
    //result = 3.0f * (result);
    //if (log_amp < -2.0f) {
    //    // Double the slope below 0.01.
    //    log_amp = -2.0f + 2.0f * (log_amp + 2.0f);
    //}
    // Avoid powf with fxpt exp2
    //result = S2F(exp2_lut(F2S(result * 3.321928094887362f)));
    float result = coefs[COEF_CONST] * controls[COEF_VEL] * controls[COEF_EG0];
    if (result <= AMP_THRESH_PLUS)  result = 0;
    return result;
}

AMY_IRAM_ATTR void partials_hold_and_modify(uint16_t osc) {
    // Version of hold_and_modify local to partials to allow speedup
    float ctrl_inputs[NUM_COMBO_COEFS];
    ctrl_inputs[COEF_CONST] = 1.0f;
    ctrl_inputs[COEF_NOTE] = (AMY_IS_SET(synth[osc]->midi_note)) ? logfreq_for_midi_note(synth[osc]->midi_note) : 0;
    ctrl_inputs[COEF_VEL] = synth[osc]->velocity;
    ctrl_inputs[COEF_EG0] = S2F(compute_breakpoint_scale(osc, 0, 0));
    //ctrl_inputs[COEF_EG1] = S2F(compute_breakpoint_scale(osc, 1, 0));
    //ctrl_inputs[COEF_MOD] = S2F(compute_mod_scale(osc));
    //ctrl_inputs[COEF_BEND] = amy_global.pitch_bend;
    //ctrl_inputs[COEF_EXT0] = cv_inputs[0];
    //ctrl_inputs[COEF_EXT1] = cv_inputs[1];

    // copy all the modifier variables
    float logfreq = p_combine_controls(ctrl_inputs, synth[osc]->logfreq_coefs);
    if (synth[osc]->portamento_alpha == 0) {
        msynth[osc]->logfreq = logfreq;
    } else {
        msynth[osc]->logfreq = logfreq + synth[osc]->portamento_alpha * (msynth[osc]->last_logfreq - logfreq);
    }
    msynth[osc]->last_logfreq = msynth[osc]->logfreq;
    //float filter_logfreq = p_combine_controls(ctrl_inputs, synth[osc]->filter_logfreq_coefs);
    //if (filter_logfreq < MIN_FILTER_LOGFREQ)  filter_logfreq = MIN_FILTER_LOGFREQ;
    //if (AMY_IS_SET(msynth[osc]->last_filter_logfreq)) {
    //    #define MAX_DELTA_FILTER_LOGFREQ_DOWN 3.0f
    //    float last_logfreq = msynth[osc]->last_filter_logfreq;
    //    if (filter_logfreq < (last_logfreq - (MAX_DELTA_FILTER_LOGFREQ_DOWN / synth[osc]->resonance))) {
    //        // Filter cutoff downward slew-rate limit.
    //        // See https://github.com/shorepine/amy/issues/126
    //        filter_logfreq = last_logfreq - (MAX_DELTA_FILTER_LOGFREQ_DOWN / synth[osc]->resonance);
    //    }
    //}
    msynth[osc]->last_filter_logfreq = 0; //filter_logfreq;
    msynth[osc]->filter_logfreq = 0; //filter_logfreq;
    //msynth[osc]->duty = p_combine_controls(ctrl_inputs, synth[osc]->duty_coefs);

    //msynth[osc]->last_pan = msynth[osc]->pan;
    //msynth[osc]->pan = p_combine_controls(ctrl_inputs, synth[osc]->pan_coefs);
    // Don't smear the pan on first frame of new note
    ///if (synth[osc]->note_on_clock == amy_global.total_samples) {
    //    //fprintf(stderr, "time %.3f osc %d note on\n", amy_global.time, osc);
    //    // First frame for this osc since note-on, don't smooth-over the pan.
    //    // (showed up when panning drum sounds).
    //    msynth[osc]->last_pan = msynth[osc]->pan;
    //}

    // amp is a special case - coeffs apply in log domain.
    float new_amp = p_amp_combine_controls(ctrl_inputs, synth[osc]->amp_coefs);
    // Also, we advance one frame by writing both last_amp and amp (=next amp)
    // *Except* for partials, where we allow one frame of ramp-on.
    //if (synth[osc]->wave == PARTIAL) {
        msynth[osc]->last_amp = msynth[osc]->amp;
        msynth[osc]->amp = new_amp;
    //} else {
    //    // Prevent hard-off on transition to release by updating last_amp only for nonzero new_last_amp.
    //    //if (new_amp > msynth[osc]->last_amp) {   // was > 0
    //    if (msynth[osc]->last_amp == 0) {
    //        msynth[osc]->last_amp = new_amp;
    //    }
    //    // Advance the envelopes to the beginning of the next frame.
    //    ctrl_inputs[COEF_EG0] = S2F(compute_breakpoint_scale(osc, 0, AMY_BLOCK_SIZE));
    //    ctrl_inputs[COEF_EG1] = S2F(compute_breakpoint_scale(osc, 1, AMY_BLOCK_SIZE));
    //    msynth[osc]->amp = amp_combine_controls(ctrl_inputs, synth[osc]->amp_coefs);
    //}
    // synth[osc]->feedback is copied to msynth in pcm_note_on, then used to track note-off for looping PCM.
    // For PCM, don't re-copy it every loop, or we'd lose track of that flag.  (This means you can't change feedback mid-playback for PCM).
    // we also check for custom, for tulips' memorypcm 
    //if (synth[osc]->wave != PCM && synth[osc]->wave != CUSTOM)  msynth[osc]->feedback = synth[osc]->feedback;
    //msynth[osc]->resonance = synth[osc]->resonance;
    //}
}

// render a full partial set at offset osc (with preset)
// freq controls pitch_ratio, amp amp_ratio, ratio controls time ratio
// do all presets have sustain point?
AMY_IRAM_ATTR SAMPLE render_partials(SAMPLE *buf, uint16_t osc) {
    SAMPLE max_value = 0;
    //uint16_t num_oscs = 0;
    // No preset partials map, we are in "build-your-own".  The max number of oscs is taken from algo_source[0].
    //num_oscs = synth[osc]->preset;
    //
    //if (synth[osc]->wave == INTERP_PARTIALS) {
    //    //const interp_partials_voice_t *partials_voice = &interp_partials_map[synth[osc]->preset % NUM_INTERP_PARTIALS_PRESETS];
    //    //num_oscs = partials_voice->num_harmonics[0];   // Assume first preset has the max #harmonics.
    //    num_oscs = interp_partials_max_partials_for_patch(synth[osc]->preset);
    //}
    uint16_t num_oscs = synth[osc]->last_two[0];  // hijack FM feedback state.

    // now, render everything, add it up
    float midi_note = midi_note_for_logfreq(msynth[osc]->logfreq);
    //fprintf(stderr, "t=%u partials o=%d msynth[osc]->logfreq=%f midi_note=%f msynth[amp]=%f\n", amy_global.total_blocks*AMY_BLOCK_SIZE, osc, msynth[osc]->logfreq, midi_note, msynth[osc]->amp);
    // Clamp to the osc pool. The assert below is compiled out in every firmware
    // build (-DNDEBUG), and this loop no longer wraps with % AMY_OSCS, so an
    // out-of-range count would read straight past synth[].
    if (osc + 1 + num_oscs > AMY_OSCS)
        num_oscs = (osc + 1 < AMY_OSCS) ? (AMY_OSCS - osc - 1) : 0;
    assert(osc < AMY_OSCS - (num_oscs + 1));  // We won't overrun.
    for(uint16_t o = osc + 1; o < osc + 1 + num_oscs; o++) {
        // A partial can be absent: partials_note_on() skips ones that failed to
        // allocate. Both note_off paths already guard this; this one didn't.
        if(synth[o] == NULL) continue;
        if(synth[o]->role == SYNTH_IS_ALGO_SOURCE) {
            // We vary each partial's "velocity" on-the-fly as the way the parent osc's amplitude envelope contributes to the partials.
            synth[o]->velocity = msynth[osc]->amp;
            // We also use dynamic, fractional note to propagate parent freq modulation.
            synth[o]->midi_note = midi_note;
            // hold_and_modify contains a special case for wave == PARTIAL so that
            // envelope value are delayed by 1 frame compared to other oscs
            // so that partials fade in over one frame from zero amp.
            partials_hold_and_modify(o);
            //printf("[%d %d] %d amp %f (%f) freq %f (%f) on %d off %d bp0 %d %f bp1 %d %f wave %d\n", amy_global.total_blocks*AMY_BLOCK_SIZE, ms_since_started, o, synth[o]->amp, msynth[o]->amp, synth[o]->freq, msynth[o]->freq, synth[o]->note_on_clock, synth[o]->note_off_clock, synth[o]->breakpoint_times[0][0], 
            //    synth[o]->breakpoint_values[0][0], synth[o]->breakpoint_times[1][0], synth[o]->breakpoint_values[1][0], synth[o]->wave);
            SAMPLE value = render_partial(buf, o);
            //fprintf(stderr, "render_partials: time %.3f osc %d ctl ampt %.6f msynth_amp %.6f max_val=%.6f\n", amy_global.time, o, msynth[osc]->amp, msynth[o]->amp, S2F(value));
            if (value > max_value) max_value = value;
        }
    }
    return max_value;
}


int _max_partials_for_partials_voice(const interp_partials_voice_t *partials_voice) {
    int max_num_partials = 0;
    for (int h = 0; h < partials_voice->num_harmonics[0]; ++h) {
        if (use_this_partial_map[h]) ++max_num_partials;
    }
    return max_num_partials;
}

int interp_partials_max_partials_for_patch(int interp_partials_patch_number) {
    const interp_partials_voice_t *partials_voice = &interp_partials_map[interp_partials_patch_number % NUM_INTERP_PARTIALS_PRESETS];
    return _max_partials_for_partials_voice(partials_voice);
}

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
    float lin =  powf(10.f, MIN(20.f, (db - 100.f)) / 20.f) - 0.001f;
    if (lin < 0)  return 0;
    return lin;
}

void _osc_on_with_harm_param(uint16_t o, float *harm_param, const interp_partials_voice_t *partials_voice) {
    // We coerce this voice into being a partial, regardless of user wishes.
    synth[o]->wave = PARTIAL;
    synth[o]->preset = 1;  // Flag that this is an envelope-based partial
    // Setup the specified frequency.
    synth[o]->logfreq_coefs[COEF_CONST] = _logfreq_of_midi_cents(harm_param[0]);
    // Setup envelope.
    //synth[o]->eg_type[0] = ENVELOPE_DB;
    synth[o]->breakpoint_times[0][0] = 0;
    synth[o]->breakpoint_values[0][0] = 0;
    int last_time = 0;
    for (int bp = 0; bp < partials_voice->num_sample_times_ms; ++bp) {
        synth[o]->breakpoint_times[0][bp + 1] = (partials_voice->sample_times_ms[bp] - last_time) * AMY_SAMPLE_RATE / 1000;
        synth[o]->breakpoint_values[0][bp + 1] = _env_lin_of_db(harm_param[bp + 1]);
        last_time = partials_voice->sample_times_ms[bp];
    }
    // Final release
    synth[o]->breakpoint_times[0][partials_voice->num_sample_times_ms + 1] = 200 * AMY_SAMPLE_RATE / 1000;
    synth[o]->breakpoint_values[0][partials_voice->num_sample_times_ms + 1] = 0;
    // Decouple osc freq and amp from note and amp.
    synth[o]->logfreq_coefs[COEF_NOTE] = 0;
    synth[o]->amp_coefs[COEF_VEL] = 1.0;  // velocity is modified on-the-fly by the control osc to vary global amplitude.
    // Other osc params.
    synth[o]->role = SYNTH_IS_ALGO_SOURCE;
    synth[o]->note_on_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
    AMY_UNSET(synth[o]->note_off_clock);
    partial_note_on(o);
}

// HOW DOES INTERP_PARTIALS (e.g. DPWE_PIANO) WORK?
// The special thing about interp_partials is that the harmonic envelopes depend on the note velocity,
// so they all have to be recomputed in response at note_on time.  Then, because their values have been
// determined to reflect velocity via the note_on calculation, the parent osc should not use velocity
// as part of its overall scaling calculation, since it would otherwise be applied twice.
// Thus, when setting up a control osc for piano, we set amp_coef[COEF_VELOCITY] = 0.

void interp_partials_note_on(uint16_t osc) {
    // Choose the interp_partials preset.
    const interp_partials_voice_t *partials_voice = &interp_partials_map[synth[osc]->preset % NUM_INTERP_PARTIALS_PRESETS];
    float midi_note = synth[osc]->midi_note;
    float midi_vel = (int)roundf(synth[osc]->velocity * 127.f);
    // Clip velocity to the range covered by the tables.  Pitch is deliberately not clipped:
    // notes outside the table range are linearly extrapolated from the edge rows (pitch_alpha
    // outside [0, 1]); the index search below is bounded so table reads stay in range.
    if (midi_vel < partials_voice->velocities[0]) midi_vel = partials_voice->velocities[0];
    if (midi_vel > partials_voice->velocities[partials_voice->num_velocities - 1]) midi_vel = partials_voice->velocities[partials_voice->num_velocities - 1];
    // Find the lower bound pitch/velocity indices.
    uint8_t pitch_index = 0, vel_index = 0;
    while(pitch_index < partials_voice->num_pitches - 2   // We're going to inspect pitch_index + 1, so make sure that's in the table.
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
    // Find the least number of harmonics across everything we're interpolating.
    int num_harmonics = MIN(MAX_NUM_HARMONICS, partials_voice->num_harmonics[note_number]);  // pl_vl note
    num_harmonics = MIN(num_harmonics, partials_voice->num_harmonics[note_number + 1]);  // pl_vh note
    num_harmonics = MIN(num_harmonics, partials_voice->num_harmonics[note_number + partials_voice->num_velocities]);  // ph_vl note
    num_harmonics = MIN(num_harmonics, partials_voice->num_harmonics[note_number + partials_voice->num_velocities + 1]);  // ph_vh note
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
    //fprintf(stderr, "interp_partials@%u: osc %d note %.1f vel %.1f pitch_x %d vel_x %d numh %d harm_bi_ll %d pitch_a %.3f vel_a %.3f alphas %.2f %.2f %.2f %.2f\n",
    //        amy_global.total_blocks*AMY_BLOCK_SIZE, osc, midi_note, midi_vel, pitch_index, vel_index, num_harmonics,
    //        harmonic_base_index_pl_vl, pitch_alpha, vel_alpha,
    //        alpha_pl_vl, alpha_pl_vh, alpha_ph_vl, alpha_ph_vh);
    // Make sure enough oscs are alloc'd in our dynamic osc alloc world.
    // This has to be enough for any note in this map.  Assume num_harmonics[0] is largest (lowest pitch).
    uint8_t max_num_partials = _max_partials_for_partials_voice(partials_voice);
    uint8_t max_num_breakpoints[MAX_BREAKPOINT_SETS] = {2 + partials_voice->num_sample_times_ms, DEFAULT_NUM_BREAKPOINTS};
    // The partials live at osc+1 .. osc+max_num_partials. If that range would
    // run off the end of the osc pool, drop the note: ensure_osc_allocd()
    // indexes synth[] without a bounds check, and render_partials() no longer
    // wraps with % AMY_OSCS.
    if (osc + 1 + max_num_partials > AMY_OSCS) {
        synth[osc]->last_two[0] = 0;
        return;
    }
    for (int o = 0; o < max_num_partials; ++o) {
        // On OOM drop the note rather than render under-sized partials. Clear the
        // stashed partial count on the way out: render_partials() reads it as its
        // loop bound, and returning without touching it leaves whatever was there
        // before -- a previous note's count, or leftover FM feedback state, since
        // last_two is a SAMPLE we're hijacking. Either makes the render walk oscs
        // this note never set up.
        if (!ensure_osc_allocd(osc + 1 + o, max_num_breakpoints)) {
            synth[osc]->last_two[0] = 0;
            return;
        }
    }
    int partial_osc = osc;
    for (int h = 0; h < num_harmonics; ++h) {
        if (use_this_partial_map[h]) {
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
            ++partial_osc;
            _osc_on_with_harm_param(partial_osc, harm_param, partials_voice);
        }
    }
    // Squirrel away num_oscs
    synth[osc]->last_two[0] = partial_osc - osc;
    // Make sure any remaining oscs are still marked as ALGO_SOURCE
    while(partial_osc < osc + 1 + max_num_partials)  { synth[partial_osc]->role = SYNTH_IS_ALGO_SOURCE; synth[partial_osc]->status = SYNTH_OFF; ++partial_osc; }
}

void interp_partials_note_off(uint16_t osc) {
    //const interp_partials_voice_t *partials_voice = &interp_partials_map[synth[osc]->preset % NUM_INTERP_PARTIALS_PRESETS];
    //int num_oscs = partials_voice->num_harmonics[0];   // Assume first preset has the max #harmonics.
    int num_oscs = 0; //MAX_NUM_HARMONICS;
    // Actual max num harmonics we may use is the number of 1s in the use_this_partial_map.
    for (int i = 0; i < MAX_NUM_HARMONICS; ++i) num_oscs += use_this_partial_map[i];
    for(uint16_t i = osc + 1; i < osc + 1 + num_oscs; i++) {
        uint16_t o = i % AMY_OSCS;
        if (synth[o]) {  // For high notes, some partials may be unused, unintialized (?)
            AMY_UNSET(synth[o]->note_on_clock);
            synth[o]->note_off_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
        }
    }
}
