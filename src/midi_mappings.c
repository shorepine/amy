// midi_mappings.c
// example midi mappings for controllers

#include "amy.h"

void juno_filter_midi_handler(uint8_t * bytes, uint16_t len, uint8_t is_sysex) {
    // An example of adding a handler for MIDI CCs.  Can't really build this in because it depends on your synth/patch config, controllers, wishes...
    // Here, we use MIDI CC 70 to modify the Juno VCF center freq, and 71 for resonance.
    amy_event e;
    if (bytes[0] == 0xB0) {  // Channel 1 CC
	if (bytes[1] == 70) {
	    // Modify Synth 0 filter_freq.
/* def to_filter_freq(val): */
/*   # filter_freq goes from ? 100 to 6400 Hz with 18 steps/octave */
/*   return float("%.3f" % (13 * exp2(0.0938 * val * 127))) */
	    e = amy_default_event();
	    e.synth = 1;
	    e.filter_freq_coefs[COEF_CONST] = exp2f(0.0938f * (float)bytes[2]);
	    amy_add_event(&e);
	} else if (bytes[1] == 71) {
/* def to_resonance(val): */
/*   # Q goes from 0.5 to 16 exponentially */
/*   return float("%.3f" % (0.7 * exp2(4.0 * val))) */
	    e = amy_default_event();
	    e.synth = 1;
	    e.resonance = 0.7f * exp2f(0.03125f * (float)bytes[2]);
	    amy_add_event(&e);
	}
    }
}

