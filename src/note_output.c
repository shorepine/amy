// note_output.c
//
// Send a synth's NOTE EVENTS somewhere other than its oscillators:
// out to CV/gate jacks as 1V/oct plus a gate, or out of a MIDI port as
// note-on/note-off with velocity.
//
// This is the mirror of cv_trigger.c. That turns a CV INPUT crossing a
// threshold into an AMY event; this turns an AMY note event into a CV
// output, using the same pitch scale and offset spelled the same way --
// so a host that patches an output back into an input with matching
// numbers gets the note back unchanged.
//
// WHY THIS IS A SYNTH PROPERTY AND NOT A WAVE. It used to be a wave
// (AMY_MIDI, wave 16): an oscillator with that wave sent a note-on out
// the MIDI port. That could not hold a channel -- the status byte was a
// hardcoded 0x90, so channel 1 always -- because the thing carrying the
// setting was an OSCILLATOR, and an oscillator has no channel. It also
// spent an osc to make no sound. "Where do this synth's notes go" is a
// question about a synth, so it lives on the synth.
//
// IT IS AN ECHO. The synth's own voices play as they always did and the
// note ALSO goes out; a synth that should be silent inside AMY is simply
// one with no voices, which costs no oscillators and needs no flag for
// it. Both of the surprising arrangements -- a configured synth going
// quiet because you named an output, and two synths answering one note
// when you wanted one -- are then things you have to ask for.
//
// (SYNTH_FLAGS_NOTES_VIA_MIDI is NOT the precedent for this, though an
// earlier draft of these comments claimed it was. That flag exists so
// that mappings set up for notes arriving over MIDI also apply to notes
// generated inside AMY, and the reinterpreted notes usually still reach
// the synth's own oscillators. It is about what a note MEANS, not about
// where it goes.)

#include "amy.h"

// How many held notes we track for mono last-note priority.  A player
// with more than this many fingers down gets the oldest forgotten,
// which is a better failure than a stuck gate.
#define NOTE_OUTPUT_HELD 8

typedef struct note_output {
    struct note_output *next;
    uint8_t synth;
    uint8_t mode;            // NOTE_OUTPUT_*
    // CV_GATE
    uint8_t pitch_cv;        // control output carrying 1V/oct
    uint8_t gate_cv;         // ...the gate
    uint8_t vel_cv;          // ...velocity, optional
    float pitch_scale;       // semitones per volt
    float pitch_offset;      // MIDI note at 0 V
    float gate_volts;        // what "gate high" means
    // MIDI_OUT
    uint8_t midi_channel;    // 1..16 as every DAW shows it
    uint8_t forward_midi_in; // forward notes that arrived over MIDI?
    // Mono state (CV_GATE only; MIDI is polyphonic and needs none)
    uint8_t held[NOTE_OUTPUT_HELD];
    uint8_t num_held;
} note_output_t;

static note_output_t *note_output_root = NULL;

static note_output_t *note_output_find(uint8_t synth) {
    note_output_t *n = note_output_root;
    while (n) {
        if (n->synth == synth) return n;
        n = n->next;
    }
    return NULL;
}

uint8_t note_output_mode_for(uint8_t synth) {
    note_output_t *n = note_output_find(synth);
    return n ? n->mode : NOTE_OUTPUT_OFF;
}

// ---------------------------------------------------------------- output

// AMY has NO OPINION about what a control output physically is. It hands
// the host an index and a voltage; whether that index is a DAC channel,
// a GPIO driven to 0 or 5 V, or something else entirely is the host's
// business. That is what makes "use a CV output as a gate" free rather
// than a feature -- a gate IS a control output driven to a voltage.
static void cv_output(uint8_t channel, float volts) {
    if (AMY_IS_UNSET(channel)) return;
    if (channel >= AMY_MAX_CV_OUT) {
        fprintf(stderr, "note_output: cv channel %d out of range 0..%d\n",
                channel, AMY_MAX_CV_OUT - 1);
        return;
    }
    if (amy_global.config.amy_external_cv_output_hook)
        amy_global.config.amy_external_cv_output_hook(channel, volts);
}

static void midi_note_output(note_output_t *n, uint8_t note, uint8_t velocity) {
    uint8_t bytes[3];
    // Note-off goes out as a note-on with velocity 0, which is what the
    // wave-type implementation did and what most keyboards send.
    bytes[0] = 0x90 | ((n->midi_channel - 1) & 0x0F);
    bytes[1] = note & 0x7F;
    bytes[2] = velocity & 0x7F;
    midi_out(bytes, 3);
}

// ---------------------------------------------------------------- notes

static void cv_pitch_for(note_output_t *n, uint8_t note) {
    // volts = (note - offset) / scale, in SEMITONES. NOTE THAT cv_trigger
    // is not spelled the same way: its scale and offset are in
    // log-frequency units (octaves, 0 at ZERO_MIDI_NOTE), since it feeds
    // them through midi_note_for_logfreq -- a host that wants a loopback
    // to round-trip converts (scale / 12, (offset - ZERO_MIDI_NOTE) / 12)
    // on the trigger side.
    float volts = (n->pitch_scale != 0)
        ? ((float)note - n->pitch_offset) / n->pitch_scale : 0;
    cv_output(n->pitch_cv, volts);
}

static void note_output_on(note_output_t *n, uint8_t note, float velocity) {
    if (n->mode == NOTE_OUTPUT_MIDI_OUT) {
        midi_note_output(n, note, (uint8_t)MIN(127, 127.1f * velocity));
        return;
    }
    // CV_GATE is monophonic with LAST-NOTE PRIORITY: one pitch output
    // carries one note. A note-on while another is held moves the pitch
    // and LEAVES THE GATE HIGH, which is how a mono synth gives you a
    // slide for free; the gate falls only when the last held note goes.
    // A REPEATED NOTE-ON RETRIGGERS, it does not stack. Two note-ons for
    // one note would otherwise need two note-offs to let the gate fall,
    // so a doubled note-on -- which a re-sent sequencer slot or a
    // stuttering controller produces easily -- would strand the gate
    // high. Real synths retrigger; so does this.
    for (uint8_t i = 0; i < n->num_held; ++i) {
        if (n->held[i] != note) continue;
        for (uint8_t j = i + 1; j < n->num_held; ++j) n->held[j - 1] = n->held[j];
        n->num_held--;
        break;
    }
    bool was_held = (n->num_held > 0);
    if (n->num_held == NOTE_OUTPUT_HELD) {
        // Drop the oldest rather than refusing the newest: a lost
        // note-off is a stuck gate, and a stuck gate is the worse of the
        // two failures by a long way.
        for (uint8_t i = 1; i < NOTE_OUTPUT_HELD; ++i) n->held[i - 1] = n->held[i];
        n->num_held--;
    }
    n->held[n->num_held++] = note;
    // PITCH FIRST, THEN THE GATE, so nothing downstream is ever told to
    // look at a voltage that is still moving.
    cv_pitch_for(n, note);
    if (AMY_IS_SET(n->vel_cv))
        cv_output(n->vel_cv, velocity * n->gate_volts);
    if (!was_held)
        cv_output(n->gate_cv, n->gate_volts);
}

static void note_output_off(note_output_t *n, uint8_t note) {
    if (n->mode == NOTE_OUTPUT_MIDI_OUT) {
        midi_note_output(n, note, 0);
        return;
    }
    // Remove it from the stack wherever it is. A note-off for something
    // that is not the sounding note changes nothing audible -- it just
    // stops that note being fallen back to.
    uint8_t found = 0;
    for (uint8_t i = 0; i < n->num_held; ++i) {
        if (n->held[i] == note && !found) { found = 1; continue; }
        n->held[i - found] = n->held[i];
    }
    if (!found) return;
    n->num_held--;
    if (n->num_held) {
        // Fall back to the note under it, gate still high: legato.
        cv_pitch_for(n, n->held[n->num_held - 1]);
    } else {
        // THE PITCH IS HELD, deliberately. Dropping it to zero would put
        // a click on every release and would make a slide into the next
        // note impossible; holding it is what hardware does.
        cv_output(n->gate_cv, 0);
    }
}

void note_output_all_off(uint8_t synth) {
    note_output_t *n = note_output_find(synth);
    if (n == NULL) return;
    n->num_held = 0;
    if (n->mode == NOTE_OUTPUT_CV_GATE) cv_output(n->gate_cv, 0);
}

// Called from patches_event_has_voices. Returns true if this event was a
// note for a note-output synth and has been dealt with.
bool note_output_handle_event(amy_event *e) {
    if (AMY_IS_UNSET(e->synth)) return false;
    note_output_t *n = note_output_find(e->synth);
    if (n == NULL || n->mode == NOTE_OUTPUT_OFF) return false;
    // ALL NOTES OFF -- a velocity of 0 with no note, patches.c's own
    // convention -- MUST reach a note output, and this is the fix for the
    // worst failure this feature has. A note-on whose note-off never
    // arrives (a sequencer wiped mid-note, a pattern rewritten under a
    // sounding step) leaves the note on the held stack for ever: the gate
    // stays high, every later note-on sees a held note and so raises no
    // edge at all, and on a modular that is a stuck note that nothing in
    // the API could clear. Panic has to reach here or it is not panic.
    if (AMY_IS_UNSET(e->midi_note) && AMY_IS_SET(e->velocity)
        && e->velocity == 0) {
        note_output_all_off(e->synth);
        return false;   // ...and the synth's own voices still get it
    }
    // ONLY NOTE EVENTS ARE CLAIMED. Anything else addressed to this synth
    // -- a level, a bus, a patch that redefines it as an ordinary synth --
    // carries on down the normal path, because swallowing it here would
    // make a note-output synth a synth you cannot change.
    if (AMY_IS_UNSET(e->midi_note)) return false;
    // A note that arrived over MIDI is not sent back out over MIDI by
    // default: without this a thru-patched port is a feedback loop. The
    // wave-type implementation guarded on the same thing. A CV/gate
    // output has no such loop to fall into, so a MIDI keyboard drives
    // the rack through it exactly as a sequenced note does -- the first
    // cut guarded both modes and a MIDI note never moved a jack.
    if (AMY_IS_SET(e->note_source_channel) && n->mode == NOTE_OUTPUT_MIDI_OUT
        && !n->forward_midi_in) return true;
    float velocity = AMY_IS_SET(e->velocity) ? e->velocity : 1.0f;
    uint8_t note = (uint8_t)(0x7F & (int)roundf(e->midi_note));
    if (velocity > 0) note_output_on(n, note, velocity);
    else              note_output_off(n, note);
    return true;
}

// ---------------------------------------------------------------- config

// iG<mode>[,...]:  CV_GATE  1,<pitch>,<gate>[,<vel>[,<scale>[,<offset>[,<gate_volts>]]]]
//                  MIDI_OUT 2,<channel 1..16>[,<forward_midi_in>]
//                  OFF      0
void note_output_config(uint8_t synth, int mode, float *args, int num_args) {
    note_output_t *n = note_output_find(synth);
    if (mode == NOTE_OUTPUT_OFF) {
        if (n) { note_output_all_off(synth); n->mode = NOTE_OUTPUT_OFF; }
        return;
    }
    if (mode != NOTE_OUTPUT_CV_GATE && mode != NOTE_OUTPUT_MIDI_OUT) {
        fprintf(stderr, "note_output: unknown mode %d\n", mode);
        return;
    }
    if (n == NULL) {
        n = (note_output_t *)malloc_caps(sizeof(note_output_t), amy_global.config.ram_caps_synth);
        if (n == NULL) { amy_oom("note_output: out of memory\n"); return; }
        n->next = note_output_root;
        note_output_root = n;
        n->synth = synth;
    } else {
        // Changing an output takes the old one down first, or a gate
        // left high on the previous channel is high for ever.
        note_output_all_off(synth);
    }
    n->num_held = 0;
    n->mode = mode;
    n->forward_midi_in = 0;
    if (mode == NOTE_OUTPUT_CV_GATE) {
        AMY_UNSET(n->vel_cv);
        n->pitch_cv = (num_args > 0) ? (uint8_t)args[0] : 0;
        n->gate_cv  = (num_args > 1) ? (uint8_t)args[1] : 1;
        // an EMPTY field ("1,0,2,,12,24") arrives unset and must stay
        // so: cast to a uint8 it read as channel 0, and every note-on
        // then wrote its velocity over the pitch output
        if (num_args > 2 && AMY_IS_SET(args[2])) n->vel_cv = (uint8_t)args[2];
        n->pitch_scale  = (num_args > 3) ? args[3] : 12.0f;
        n->pitch_offset = (num_args > 4) ? args[4] : 24.0f;
        n->gate_volts   = (num_args > 5) ? args[5] : 5.0f;
        n->midi_channel = 1;
    } else {
        n->midi_channel = (num_args > 0) ? (uint8_t)args[0] : 1;
        if (n->midi_channel < 1 || n->midi_channel > 16) {
            fprintf(stderr, "note_output: midi channel %d out of range 1..16\n", n->midi_channel);
            n->midi_channel = 1;
        }
        if (num_args > 1) n->forward_midi_in = (args[1] != 0);
        n->pitch_scale = 12.0f; n->pitch_offset = 24.0f; n->gate_volts = 5.0f;
        n->pitch_cv = 0; n->gate_cv = 1; AMY_UNSET(n->vel_cv);
    }
}

/* Every note output's gate down, whoever owns it.
 *
 * all_notes_off()'s half of the panic. A host reaching for
 * RESET_ALL_NOTES means "stop everything", and a gate is the one thing
 * here that can stay stuck without being audible on this machine at
 * all -- the noise it makes is in somebody's rack. */
void note_output_all_gates_off(void) {
    note_output_t *n = note_output_root;
    while (n) {
        n->num_held = 0;
        if (n->mode == NOTE_OUTPUT_CV_GATE) cv_output(n->gate_cv, 0);
        n = n->next;
    }
}

void note_output_reset(void) {
    note_output_t *n = note_output_root;
    while (n) {
        note_output_t *next = n->next;
        if (n->mode == NOTE_OUTPUT_CV_GATE) cv_output(n->gate_cv, 0);
        free(n);
        n = next;
    }
    note_output_root = NULL;
}

// The wire command that reconstructs this synth's note output, for
// amy_dump_state -- a note output that did not survive a state round
// trip would be a synth that came back silently pointed at oscillators.
int note_output_emit_command(uint8_t synth, char *buf, size_t len) {
    note_output_t *n = note_output_find(synth);
    if (n == NULL || n->mode == NOTE_OUTPUT_OFF) return 0;
    if (n->mode == NOTE_OUTPUT_MIDI_OUT)
        return snprintf(buf, len, "i%diG%d,%d,%d", synth, n->mode,
                        n->midi_channel, n->forward_midi_in);
    return snprintf(buf, len, "i%diG%d,%d,%d,%d,%g,%g,%g", synth, n->mode,
                    n->pitch_cv, n->gate_cv,
                    AMY_IS_SET(n->vel_cv) ? n->vel_cv : 0,
                    n->pitch_scale, n->pitch_offset, n->gate_volts);
}
