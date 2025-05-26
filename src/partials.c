// partials.c
// deal with partials presets

#include "amy.h"


typedef struct {
    uint32_t bp_offset;
    uint32_t bp_length;
    uint8_t midi_note;
    uint32_t sustain_ms;
    uint16_t oscs_alloc;
} partial_breakpoint_map_t;

typedef struct {
    uint32_t ms_offset;
    uint16_t osc;
    float freq;
    float amp;
    float phase;
    uint16_t ms_delta;
    float amp_delta;
    float freq_delta;
} partial_breakpoint_t;

// Automatically generated from partials.generate_partials_header()
#include "partials.h"




// choose a preset from the .h file
void partials_note_on(uint16_t osc) {
    // just like PCM, start & end breakpoint are stored here
    if (synth[osc]->preset < 0) {
        // Preset_num < 0 means build-your-own with (-preset_num) oscs.
        int num_partials = -synth[osc]->preset;
        for (int i = 0; i < num_partials; ++i) {
            int o = osc + 1 + i;
            ensure_osc_allocd(o, NULL);
            if (synth[o]->wave == PARTIAL) {  // User has marked this as a partial.
                // Mark this PARTIAL as part of a build-your own with a flag value in its preset field.
                // This is used I think only at envelope.c:121 to avoid the normal partial preset special-case for PARTIALs.
                synth[o]->preset = synth[osc]->preset;
                // We used to reset the amp_coefs here to avoid confusing the user, but now I want to be able to
                // manipulate the amp_coefs of individual PARTIALs before I play them, so we'll hand the user more rope...
                //synth[o]->amp_coefs[COEF_CONST] = 1.0f;  // This is apparently necessary, but is now setup in reset_osc
                //synth[o]->amp_coefs[COEF_VEL] = 1.0f;
                //synth[o]->amp_coefs[COEF_EG0] = 1.0f;
                // //synth[o]->logfreq_coefs[COEF_CONST] = logfreq_of_freq((o + 1) * 220.f);
                //synth[o]->logfreq_coefs[COEF_NOTE] = 1.0;
                // //synth[o]->logfreq_coefs[COEF_EG1] = 1.0;  // Let the user turn this on if they want.
                synth[o]->logfreq_coefs[COEF_BEND] = 0;  // Each PARTIAL will receive pitch bend via the midi_note modulation from the parent osc, don't add it twice.
                synth[o]->status = SYNTH_IS_ALGO_SOURCE;
                synth[o]->note_on_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
                AMY_UNSET(synth[o]->note_off_clock);
                msynth[o]->logfreq = synth[o]->logfreq_coefs[COEF_CONST] + msynth[osc]->logfreq;
                partial_note_on(o);
            }
        }
    } else {
        // Regular preset number.
        partial_breakpoint_map_t preset = partial_breakpoint_map[synth[osc]->preset];
        // s[o]->step is used to track which breakpoint we're currently using.
        synth[osc]->step = preset.bp_offset;
        synth[osc]->substep = synth[osc]->step + preset.bp_length;
        // Now let's start the oscillators (silently)
        uint16_t oscs = preset.oscs_alloc;
        if(osc + 1 + oscs > AMY_OSCS) {
            fprintf(stderr,"Asking for more oscs than you have -- starting %d, + 1 + %d more\n", osc, oscs);
        }
        for (int i = osc + 1; i < osc + 1 + oscs; ++i)  ensure_osc_allocd(i, NULL);
    }
}

void partials_note_off(uint16_t osc) {
    if (synth[osc]->preset < 0) {
        // Build-your-own-partials: Push each partial into release.
        int num_oscs = -synth[osc]->preset;
        for(uint16_t i = osc + 1; i < osc + 1 + num_oscs; i++) {
            uint16_t o = i % AMY_OSCS;
            AMY_UNSET(synth[o]->note_on_clock);
            synth[o]->note_off_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
        }
    } else {
        // todo; finish the sustain
        synth[osc]->step = -1;
    }
}


// render a full partial set at offset osc (with preset)
// freq controls pitch_ratio, amp amp_ratio, ratio controls time ratio
// do all presets have sustain point?
SAMPLE render_partials(SAMPLE *buf, uint16_t osc) {
    SAMPLE max_value = 0;
    uint16_t num_oscs = 0;
    if (synth[osc]->preset < 0) {
        // No preset partials map, we are in "build-your-own".  The max number of oscs is taken from algo_source[0].
        num_oscs = -synth[osc]->preset;
    } else if (synth[osc]->wave == INTERP_PARTIALS) {
        //const interp_partials_voice_t *partials_voice = &interp_partials_map[synth[osc]->preset % NUM_INTERP_PARTIALS_PRESETS];
        //num_oscs = partials_voice->num_harmonics[0];   // Assume first preset has the max #harmonics.
        num_oscs = interp_partials_max_partials_for_patch(synth[osc]->preset);
    } else {
        // Set up from partials map.
        partial_breakpoint_map_t preset = partial_breakpoint_map[synth[osc]->preset % PARTIALS_PRESETS];
        // If ratio is set (not 0 or -1), use it for a time stretch
        float time_ratio = 1;
        if(AMY_IS_SET(synth[osc]->logratio)) time_ratio = exp2f(synth[osc]->logratio);
        uint32_t ms_since_started = (uint32_t) ((((amy_global.total_blocks*AMY_BLOCK_SIZE - synth[osc]->note_on_clock) / (float)AMY_SAMPLE_RATE)*1000.0)*time_ratio);
        num_oscs = preset.oscs_alloc;
        if(synth[osc]->step >= 0) {
            // do we either have no sustain, or are we past sustain?
            // TODO: sustain is likely more complicated --we want to bounce between the closest bps for loopstart & loopend
            uint32_t sustain_ms = preset.sustain_ms;
            if((sustain_ms > 0 && (ms_since_started < sustain_ms)) || sustain_ms == 0) {
                partial_breakpoint_t pb = partial_breakpoints[(uint32_t)synth[osc]->step];
                while (ms_since_started >= pb.ms_offset) {
                    // It's time for the next breakpoint!
                    uint16_t o = (pb.osc + 1 + osc) % AMY_OSCS; // just in case
    
                    //printf("time %.3f rel %f: freqlogratio %f new pb: osc %d t_ms %d amp %f freq %f phase %f logfreq %f\n", amy_global.total_blocks*AMY_BLOCK_SIZE / (float)AMY_SAMPLE_RATE, ms_since_started / (float)AMY_SAMPLE_RATE, freq_logratio, pb.osc, pb.ms_offset, pb.amp, pb.freq, pb.phase, logfreq_of_freq(pb.freq));

                    // All the types share these params or are overwritten
                    synth[o]->wave = PARTIAL;
                    synth[o]->status = SYNTH_IS_ALGO_SOURCE;
                    // Copy the preset number here just as a way to tell what kind of preset is employed when looking only at osc.
                    synth[o]->preset = synth[osc]->preset;
                    //synth[o]->velocity = synth[osc]->velocity;
                    // velocity is set directly before rendering each partial according to the parent osc's amplitude envelope.
                    synth[o]->note_on_clock = amy_global.total_blocks*AMY_BLOCK_SIZE; // start breakpoints
                    synth[o]->amp_coefs[COEF_CONST] = pb.amp;
                    // logfreq coef_const combines both the freq of this partial and the expected note.  Actual note will be added via 'note' field.
                    synth[o]->logfreq_coefs[COEF_CONST] = logfreq_of_freq(pb.freq) - logfreq_for_midi_note(preset.midi_note);

                    synth[o]->breakpoint_times[0][0] = 0;
                    synth[o]->breakpoint_values[0][0] = 1.0f;
                    synth[o]->breakpoint_times[0][1] = ms_to_samples((int)((float)pb.ms_delta/time_ratio));
                    synth[o]->breakpoint_values[0][1] = pb.amp_delta;  // Proportional delta i.e. amp_final = amp_delta * amp_initial
                    synth[o]->breakpoint_times[0][2] = 0;
                    synth[o]->breakpoint_values[0][2] = 0.0;  // Release amp target value
                    AMY_UNSET(synth[o]->breakpoint_times[0][3]);
                    synth[o]->eg_type[0] = ENVELOPE_LINEAR;
                    synth[o]->amp_coefs[COEF_VEL] = 1.0;
                    synth[o]->amp_coefs[COEF_EG0] = 1.0;

                    synth[o]->breakpoint_times[1][0] = 0;
                    synth[o]->breakpoint_values[1][0] = 0.0;
                    synth[o]->breakpoint_times[1][1] = ms_to_samples((int)((float)pb.ms_delta/time_ratio));
                    if (pb.freq_delta <= 0)
                        synth[o]->breakpoint_values[1][1] = 0.0;
                    else
                        synth[o]->breakpoint_values[1][1] = S2F(log2_lut(F2S(pb.freq_delta)));  // f_end = freq_delta * f_start.
                    synth[o]->breakpoint_times[1][2] = 0;
                    synth[o]->breakpoint_values[1][2] = 0.0;  // Release freq mod target value.
                    AMY_UNSET(synth[o]->breakpoint_times[1][3]);
                    synth[o]->eg_type[1] = ENVELOPE_LINEAR;
                    synth[o]->logfreq_coefs[COEF_NOTE] = 1.0;
                    synth[o]->logfreq_coefs[COEF_EG1] = 1.0;
                    synth[o]->logfreq_coefs[COEF_BEND] = 0;  // Each PARTIAL will receive pitch bend via the midi_note modulation from the parent osc, don't add it twice.
                
                    uint8_t partial_code = 0; // control code for partial presets
                    if(pb.phase < 0) {
                        partial_code = (uint8_t)(-pb.phase);
                    } else {
                        // Only when starting a new partial.
                        synth[o]->phase = F2P(pb.phase);
                    }
                    if(partial_code==1) { // continuation
                        //synth[o]->logfreq_coefs[COEF_CONST] = logfreq_of_freq(pb.freq) + freq_logratio;
                        //printf("[%d %d] o %d continue partial\n", amy_global.total_blocks*AMY_BLOCK_SIZE, ms_since_started, o);
                    } else if(partial_code==2) { // partial is done, give it one buffer to ramp to zero.
                        synth[o]->amp_coefs[COEF_CONST] = 0.0001;
                        //partial_note_off(o);
                    } else { // start of a partial,
                        //printf("[%d %d] o %d start partial\n", amy_global.total_blocks*AMY_BLOCK_SIZE,ms_since_started, o);
                        // Simulate the work of hold_and_modify to ensure msynth logfreq is set, to guide choice of LUT length.
                        msynth[o]->logfreq = synth[o]->logfreq_coefs[COEF_CONST] + msynth[osc]->logfreq;
                        partial_note_on(o);
                    }
                    synth[osc]->step++;
                    if(synth[osc]->step == synth[osc]->substep) {
                        partials_note_off(osc);
                        break;  // for the while loop version.
                    }
                    pb = partial_breakpoints[(uint32_t)synth[osc]->step]; // for the while loop version.
                }
            }
        } else {
            // Somehow, step == -1 (note off was sent?)
            // TODO -- we should play the remainder of the breakpoints
            for(uint16_t i = osc + 1; i < osc + 1 + num_oscs; i++) {
                uint16_t o = i % AMY_OSCS;
                if(synth[o]->status ==SYNTH_IS_ALGO_SOURCE) {
                    AMY_UNSET(synth[o]->note_on_clock);
                    synth[o]->note_off_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
                    synth[o]->status = SYNTH_OFF;
                }
            }
            // osc note off, start release
            AMY_UNSET(synth[osc]->note_on_clock);
            synth[osc]->note_off_clock = amy_global.total_blocks*AMY_BLOCK_SIZE;
            synth[osc]->status = SYNTH_OFF;
        }
    }  // End of block for handling preset presets.
    // now, render everything, add it up
    float midi_note = midi_note_for_logfreq(msynth[osc]->logfreq);
    //fprintf(stderr, "t=%u partials o=%d msynth[osc]->logfreq=%f midi_note=%f msynth[amp]=%f\n", amy_global.total_blocks*AMY_BLOCK_SIZE, osc, msynth[osc]->logfreq, midi_note, msynth[osc]->amp);
    for(uint16_t i = osc + 1; i < osc + 1 + num_oscs; i++) {
        uint16_t o = i % AMY_OSCS;
        if(synth[o]->status ==SYNTH_IS_ALGO_SOURCE) {
            // We vary each partial's "velocity" on-the-fly as the way the parent osc's amplitude envelope contributes to the partials.
            synth[o]->velocity = msynth[osc]->amp;
            // We also use dynamic, fractional note to propagate parent freq modulation.
            synth[o]->midi_note = midi_note;
            // hold_and_modify contains a special case for wave == PARTIAL so that
            // envelope value are delayed by 1 frame compared to other oscs
            // so that partials fade in over one frame from zero amp.
            hold_and_modify(o);
            //printf("[%d %d] %d amp %f (%f) freq %f (%f) on %d off %d bp0 %d %f bp1 %d %f wave %d\n", amy_global.total_blocks*AMY_BLOCK_SIZE, ms_since_started, o, synth[o]->amp, msynth[o]->amp, synth[o]->freq, msynth[o]->freq, synth[o]->note_on_clock, synth[o]->note_off_clock, synth[o]->breakpoint_times[0][0], 
            //    synth[o]->breakpoint_values[0][0], synth[o]->breakpoint_times[1][0], synth[o]->breakpoint_values[1][0], synth[o]->wave);
            SAMPLE value = render_partial(buf, o);
            if (value > max_value) max_value = value;
            // Deferred termination of this partial, after final ramp-out.
            if (synth[o]->amp_coefs[COEF_CONST] == 0)  partial_note_off(o);
        }
    }
    return max_value;
}

