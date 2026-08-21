// patches.c
// baked in AMY string patches (Juno-6 & DX7 for now)

#include "amy.h"
#include "patches.h"

#include <assert.h>   // for buffer overruns in sprint_event.

#define _PATCHES_FIRST_USER_PATCH 1024


uint32_t max_num_memory_patches = 0;
struct delta **memory_patch_deltas = NULL;
uint16_t *memory_patch_oscs = NULL;
// Nonzero when the slot's number was invented by patches_store_patch
// rather than chosen by the caller. Only auto-assigned slots may be
// recycled (reused on a re-store to the same instrument, freed when
// that instrument is released): an explicitly numbered patch may be
// loaded again later by anyone who knows the number, so it is never
// freed behind the caller's back.
uint8_t *memory_patch_auto = NULL;
uint16_t next_user_patch_index = 0;
uint16_t * osc_to_voice = NULL;
uint16_t *voice_to_base_osc = NULL;

void patches_deinit() {
    memory_patch_deltas = NULL;
    memory_patch_oscs = NULL;
    memory_patch_auto = NULL;
    osc_to_voice = NULL;
    voice_to_base_osc = NULL;
}

void patches_init(int max_memory_patches) {
    max_num_memory_patches = max_memory_patches;
    uint8_t *alloc_base = malloc_caps(
            max_num_memory_patches * sizeof(struct delta *)
	    + max_num_memory_patches * sizeof(uint16_t)
            + AMY_OSCS * sizeof(uint16_t)
            + amy_global.config.max_voices * sizeof(uint16_t)
            + max_num_memory_patches * sizeof(uint8_t),
	    amy_global.config.ram_caps_synth
    );
    memory_patch_deltas = (struct delta **)alloc_base;
    memory_patch_oscs = (uint16_t *)(memory_patch_deltas + max_num_memory_patches);
    osc_to_voice = (uint16_t *)(memory_patch_oscs + max_num_memory_patches);
    voice_to_base_osc = (uint16_t *)(osc_to_voice + AMY_OSCS);
    memory_patch_auto = (uint8_t *)(voice_to_base_osc + amy_global.config.max_voices);
    bzero(memory_patch_deltas, max_num_memory_patches * sizeof(struct delta *));
    bzero(memory_patch_auto, max_num_memory_patches * sizeof(uint8_t));
    patches_reset();
}

void patches_reset_patch(int patch_number) {
    int patch_index = patch_number - _PATCHES_FIRST_USER_PATCH;
    if (patch_index < 0 || patch_index >= (int)max_num_memory_patches) {
        fprintf(stderr, "reset patch number %" PRId32 " is out of range (%" PRId32 " .. %" PRId32 ")\n",
                (int32_t)(patch_index + _PATCHES_FIRST_USER_PATCH),
                (int32_t)_PATCHES_FIRST_USER_PATCH,
                (int32_t)(_PATCHES_FIRST_USER_PATCH + (int32_t)max_num_memory_patches));
        return;
    }
    if (memory_patch_deltas[patch_index] != NULL)  delta_release_list(memory_patch_deltas[patch_index]);
    memory_patch_deltas[patch_index] = NULL;
    memory_patch_oscs[patch_index] = 0;
    memory_patch_auto[patch_index] = 0;
}

void patches_reset() {
    for(uint32_t v = 0; v < amy_global.config.max_voices; v++) {
        AMY_UNSET(voice_to_base_osc[v]);
    }
    for(uint32_t i = 0; i < AMY_OSCS; i++) {
        AMY_UNSET(osc_to_voice[i]);
    }
    for(uint32_t i = 0; i < max_num_memory_patches; i++) {
        patches_reset_patch(_PATCHES_FIRST_USER_PATCH + i);
    }
    next_user_patch_index = 0;
}

void patches_debug() {
    // uint16_t counter: a uint8_t one never reaches a max_voices of 256+,
    // so this loop did not terminate.
    for(uint16_t v = 0; v < amy_global.config.max_voices; v++) {
        if (AMY_IS_SET(voice_to_base_osc[v]))
            fprintf(stderr, "voice %" PRIu16 " base osc %" PRIu16 "\n", v, voice_to_base_osc[v]);
    }
    fprintf(stderr, "osc_to_voice:\n");
    for(uint16_t i=0;i<AMY_OSCS;) {
        uint16_t j = 0;
        fprintf(stderr, "%" PRIu16 ": ", i);
        for (j=0; j < 16; ++j) {
            if ((i + j) >= AMY_OSCS)  break;
            fprintf(stderr, "%" PRIu16 " ", osc_to_voice[i + j]);
        }
        i += j;
        fprintf(stderr, "\n");
    }
    for(uint8_t i = 0; i < max_num_memory_patches; i++) {
        if(memory_patch_oscs[i])
            fprintf(stderr, "memory_patch %" PRIu16 " oscs %" PRIu16 " #deltas %" PRIi32 "\n",
                    (uint16_t)(i + _PATCHES_FIRST_USER_PATCH), memory_patch_oscs[i], delta_list_len(memory_patch_deltas[i]));
    }
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    for (uint8_t i = 0; i < 32 /* MAX_INSTRUMENTS */; ++i) {
        int num_voices = instrument_get_num_voices(i, voices);
        if (num_voices) {
            fprintf(stderr, "synth %" PRIu8 " num_voices %" PRId32 " patch_num %" PRId32 " flags %" PRIu32 " voice #s",
                    i, (int32_t)num_voices, (int32_t)instrument_get_patch_number(i), instrument_get_flags(i));
            for (int j = 0; j < num_voices; ++j)  fprintf(stderr, " %" PRIu16, voices[j]);
            fprintf(stderr, "\n");
        }
    }
}

struct delta **queue_for_patch_number(int patch_number) {
    int patch_index = patch_number - _PATCHES_FIRST_USER_PATCH;
    if (patch_index < 0 || patch_index >= (int)max_num_memory_patches) {
        fprintf(stderr, "queue for patch number %" PRId32 " is out of range (%" PRId32 " .. %" PRId32 ")\n",
                (int32_t)(patch_index + _PATCHES_FIRST_USER_PATCH),
                (int32_t)_PATCHES_FIRST_USER_PATCH,
                (int32_t)(_PATCHES_FIRST_USER_PATCH + (int32_t)max_num_memory_patches));
        return NULL;
    }
    return &memory_patch_deltas[patch_index];
}

void update_num_oscs_for_patch_number(int patch_number) {
    int patch_index = patch_number - _PATCHES_FIRST_USER_PATCH;
    if (patch_index < 0 || patch_index >= (int)max_num_memory_patches) {
        fprintf(stderr, "queue for patch number %" PRId32 " is out of range (%" PRId32 " .. %" PRId32 ")\n",
                (int32_t)(patch_index + _PATCHES_FIRST_USER_PATCH),
                (int32_t)_PATCHES_FIRST_USER_PATCH,
                (int32_t)(_PATCHES_FIRST_USER_PATCH + (int32_t)max_num_memory_patches));
        return;
    }
    int num_oscs = 0;
    struct delta *d = memory_patch_deltas[patch_index];
    while(d) {
        if (d->osc >= num_oscs)  num_oscs = d->osc + 1;
        d = d->next;
    }
    memory_patch_oscs[patch_index] = num_oscs;
}

void all_notes_off() {
    for(uint16_t i=0;i<AMY_OSCS;i++) {
        if (AMY_IS_SET(osc_to_voice[i])) {
            if(synth[i]->status == SYNTH_AUDIBLE) {
                synth[i]->status = SYNTH_INAUDIBLE;
            }
        }
    }
}

void add_deltas_to_queue_with_baseosc(struct delta *d, int base_osc, uint16_t oscs_per_voice, struct delta **queue, uint32_t time) {
    //fprintf(stderr, "add_deltas_to_queue_with_baseosc: added %d baseosc %d time %d\n", delta_list_len(d), base_osc, time);
    struct delta d_offset;
    while(d) {
        d_offset = *d;
        // These deltas are voice-relative, so they answer to the same bound as
        // a live event does (see osc_ref_within_voice): a stored patch that
        // names more oscs than the voice it is being replayed into has, or
        // points one of them outside it, must not write into the neighbours.
        if (!osc_ref_within_voice((int)d_offset.osc, oscs_per_voice, "stored")) {
            d = d->next;
            continue;
        }
        d_offset.osc += base_osc;
        if (d_offset.param == CHAINED_OSC || d_offset.param == RESET_OSC
            || (d_offset.param >= MOD_SOURCE_START && d_offset.param < MOD_SOURCE_END)
            || (d_offset.param >= ALGO_SOURCE_START && d_offset.param < ALGO_SOURCE_START + MAX_ALGO_OPS))
            if (!(AMY_IS_UNSET((int16_t)d_offset.data.i) || AMY_IS_UNSET((uint16_t)d_offset.data.i))) {  // CHAINED_OSC is uint16_t, but ALGO_SOURCE is int16_t.
                // RESET_OSC's payload is a mask of RESET_* bits as often as it
                // is an osc number, so it is not range-checked (as in
                // EVENT_TO_DELTA_WITH_BASEOSC).
                if (d_offset.param != RESET_OSC &&
                    !osc_ref_within_voice((int)d_offset.data.i, oscs_per_voice, "stored reference")) {
                    d = d->next;
                    continue;
                }
                d_offset.data.i += base_osc;
            }
        d_offset.time = time;
        // assume the d->time is 0 and that's good.
        add_delta_to_queue(&d_offset, &amy_global.delta_queue);
        d = d->next;
    }
}

void snprintcodefloat(char *s, size_t max_len, char *code, float val) {
    // Skip trailing zeros (and decimal point) when printing a float.
    float testval = val;
    char fmt[7]; // = "%s%.0f";
    fmt[0] = '%';
    fmt[1] = 's';
    fmt[2] = '%';
    fmt[3] = '.';
    fmt[4] = '0';
    fmt[5] = 'f';
    fmt[6] = 0;
    for (int dp = 0; dp < 3; ++dp) {
        if (testval == roundf(testval)) {
            snprintf(s, max_len, fmt, code, val);
            return;
        }
        testval *= 10.0f;
        ++fmt[4];  // increase the DP digit
    }
    snprintf(s, max_len, fmt, code, val);
}

void snprintfloat3dp(char *s, size_t max_len, float val) {
    // Print a single float value with up to 3 decimal places, but no more.
    snprintcodefloat(s, max_len, "", val);
}

#define _EPRINT_I(FIELD, NAME, WIRECODE) if (AMY_IS_SET(e->FIELD)) { snprintf(s, len - (size_t)(s - s_entry), "%s%" PRId32, wirecode ? WIRECODE : " " NAME ": ", (int32_t)e->FIELD); s += strlen(s); }
#define _EPRINT_F(FIELD, NAME, WIRECODE) if (AMY_IS_SET(e->FIELD)) { snprintcodefloat(s, len - (size_t)(s - s_entry), wirecode ? WIRECODE : " " NAME ": ", e->FIELD); s += strlen(s); }
#define _EPRINT_COEF(FIELD, NAME, WIRECODE) {            \
    int last_set = -1; \
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) {    \
        if (AMY_IS_SET(e->FIELD[i])) last_set = i; \
    }                                              \
    if (last_set >= 0) { \
        snprintf(s, len - (size_t)(s - s_entry), "%s", wirecode ? WIRECODE : " " NAME ": ");        \
        s += strlen(s);  \
        for (int i = 0; i <= last_set; ++i) { \
            if (i > 0) { snprintf(s, len - (size_t)(s - s_entry), ","); s += strlen(s); }      \
            if (AMY_IS_SET(e->FIELD[i])) {        \
                snprintfloat3dp(s, len - (size_t)(s - s_entry), e->FIELD[i]); \
                s += strlen(s);  \
            }   \
        } \
    }     \
}
#define _EPRINT_I_SEQ(FIELD, NAME, LEN, WIRECODE) {      \
    int last_set = -1; \
    for (int i = 0; i < LEN; ++i) {    \
        if (AMY_IS_SET(e->FIELD[i])) last_set = i; \
    }                                              \
    if (last_set >= 0) { \
        snprintf(s, len - (size_t)(s - s_entry), "%s", wirecode ? WIRECODE : " " NAME ": ");       \
        s += strlen(s);  \
        for (int i = 0; i <= last_set; ++i) { \
            if (i > 0) { snprintf(s, len - (size_t)(s - s_entry), ","); s += strlen(s); }        \
            if (AMY_IS_SET(e->FIELD[i])) { \
                snprintf(s, len - (size_t)(s - s_entry), "%" PRId32, (int32_t)e->FIELD[i]); \
                s += strlen(s); \
            } \
        } \
    }     \
}
// As _EPRINT_I_SEQ but unsigned. ticks needs this: its values are uint32_t,
// and printing one past INT32_MAX as a negative number makes the unsigned
// list parser on the other end stop at the '-' -- for a 3-value ticks that
// silently turned an (invalid, should-be-rejected) tag into a 2-value
// anonymous entry.
#define _EPRINT_U_SEQ(FIELD, NAME, LEN, WIRECODE) {      \
    int last_set = -1; \
    for (int i = 0; i < LEN; ++i) {    \
        if (AMY_IS_SET(e->FIELD[i])) last_set = i; \
    }                                              \
    if (last_set >= 0) { \
        snprintf(s, len - (size_t)(s - s_entry), "%s", wirecode ? WIRECODE : " " NAME ": ");       \
        s += strlen(s);  \
        for (int i = 0; i <= last_set; ++i) { \
            if (i > 0) { snprintf(s, len - (size_t)(s - s_entry), ","); s += strlen(s); }        \
            if (AMY_IS_SET(e->FIELD[i])) { \
                snprintf(s, len - (size_t)(s - s_entry), "%" PRIu32, (uint32_t)e->FIELD[i]); \
                s += strlen(s); \
            } \
        } \
    }     \
}
#define _EPRINT_BP(TFIELD, VFIELD, NAME, WIRECODE) {         \
    int last_set = -1;                              \
    for (int i = 0; i < MAX_BPS; ++i) {             \
        if (AMY_IS_SET(e->TFIELD[i]) || AMY_IS_SET(e->VFIELD[i])) last_set = i; \
    }                                                \
    if (last_set >= 0) {                            \
        snprintf(s, len - (size_t)(s - s_entry), "%s", wirecode ? WIRECODE : " " NAME ": ");        \
        s += strlen(s);   \
        for (int i = 0; i <= last_set; ++i) {       \
            if (i > 0) { snprintf(s, len - (size_t)(s - s_entry), ","); s += strlen(s); }        \
            if (AMY_IS_SET(e->TFIELD[i])) {                        \
                snprintf(s, len - (size_t)(s - s_entry), "%" PRIu32, e->TFIELD[i]); \
                s += strlen(s);  \
            }    \
            snprintf(s, len - (size_t)(s - s_entry), ",");                   \
            s += strlen(s);    \
            if (AMY_IS_SET(e->VFIELD[i])) {       \
                snprintfloat3dp(s, len - (size_t)(s - s_entry), e->VFIELD[i]); \
                s += strlen(s);  \
            }  \
        }                                            \
    }                                                \
}

#define _EPRINT_VALS_5(VAL1, VAL2, VAL3, VAL4, VAL5, NAME, WIRECODE)  {      \
        float vals[] = {VAL1, VAL2, VAL3, VAL4, VAL5}; \
        int n_vals = sizeof(vals) / sizeof(float); \
        int last_one = -1; \
        for (int i = 0; i < n_vals; ++i) { \
            if (AMY_IS_SET(vals[i])) last_one = i; \
        } \
        if (last_one >= 0) { \
            snprintf(s, len - (size_t)(s - s_entry), "%s", wirecode ? WIRECODE : " " NAME ": "); \
            s += strlen(s); \
            for (int j = 0; j <= last_one; ++j) {  \
                if (AMY_IS_SET(vals[j])) { \
                    snprintfloat3dp(s, len - (size_t)(s - s_entry), vals[j]); \
                    s += strlen(s); \
                } \
                if (j < last_one) { \
                    snprintf(s, len - (size_t)(s - s_entry), ","); \
                    s += strlen(s); \
                } \
            } \
        } \
    }

int sprint_event(amy_event *e, char *s, size_t len, bool wirecode) {
    // Convert an event into a string, either human-readable or wirecode.
    // s must be allocated.  len tells us how big it is.
    // Return is how many chrs written to s.  Will abort if it overruns.
    char *s_entry = s;
    if (!wirecode) {
        snprintf(s, len - (size_t)(s - s_entry), "amy_event(time=%" PRIu32 ", osc=%u, addr_osc=%d adr_syn=%d adr_bus=%d): ", e->time, (unsigned)e->osc,
                 event_addresses_oscs(e), event_addresses_synth(e), event_addresses_bus(e));
        s += strlen(s);
        _EPRINT_U_SEQ(ticks, "ticks", 3, "H"); // tick, period, tag
    } else {
        // e->time has no wire representation anymore (there's no 't' command);
        // it's only ever meaningful as this event's own near-term playback time.
        // ticks ("H") must always be the first entry in wire code if used.
        _EPRINT_U_SEQ(ticks, "ticks", 3, "H"); // tick, period, tag
        _EPRINT_I(osc, "osc", "v");
    }
    _EPRINT_I(wave, "wave", "w");
    _EPRINT_I(preset, "preset", "p");
    _EPRINT_F(midi_note, "midi_note", "n");
    _EPRINT_F(velocity, "velocity", "l");
    _EPRINT_I(patch_number, "patch_number", "K");
    _EPRINT_COEF(amp_coefs, "amp_coefs", "a");
    _EPRINT_COEF(freq_coefs, "freq_coefs", "f");
    _EPRINT_COEF(filter_freq_coefs, "filter_freq_coefs", "F");
    _EPRINT_COEF(duty_coefs, "duty_coefs", "d");
    _EPRINT_COEF(pan_coefs, "pan_coefs", "Q");
    _EPRINT_F(feedback, "feedback", "b");
    _EPRINT_F(trigger_phase, "phase", "P");
    _EPRINT_I(sample_offset, "sample_offset", "po");
    _EPRINT_F(fit_ticks, "fit", "pF");
    _EPRINT_I(fit_search, "fit_search", "pS");
    _EPRINT_F(pitch_bend, "pitch_bend", "s");  // NOT osc-dep
    _EPRINT_F(tempo, "tempo", "j");  // NOT osc-dep
    _EPRINT_I(latency_ms, "latency_ms", "N");  // NOT osc-dep
    _EPRINT_F(ratio, "ratio", "I");
    _EPRINT_F(resonance, "resonance", "R");
    _EPRINT_I(portamento_ms, "portamento_ms", "m");
    _EPRINT_I(chained_osc, "chained_osc", "c");
    _EPRINT_I_SEQ(mod_source, "mod_source", NUM_MOD_SOURCES, "L");
    _EPRINT_I(algorithm, "algorithm", "o");
    _EPRINT_I(filter_type, "filter_type", "G");
    // Distortion rides 'G' sub-commands: GC/GF/GH pick the type (a zero
    // value is the off switch, printed canonically as GC0), GD/GM carry the
    // shared drive and mix.
    if (AMY_IS_SET(e->dist_type)) {
        uint8_t dist_type = e->dist_type;
        if (wirecode && dist_type == DIST_CRUSH) {
            snprintf(s, len - (size_t)(s - s_entry), "GH"); s += strlen(s);
            if (AMY_IS_SET(e->dist_bits)) { snprintf(s, len - (size_t)(s - s_entry), "%d", e->dist_bits); s += strlen(s); }
            if (AMY_IS_SET(e->dist_rate)) {
                snprintf(s, len - (size_t)(s - s_entry), ",%d", e->dist_rate); s += strlen(s);
            }
        } else if (wirecode) {
            snprintf(s, len - (size_t)(s - s_entry), "G%c%d",
                     (dist_type == DIST_FOLD) ? 'F' : 'C', (dist_type != DIST_OFF) ? 1 : 0);
            s += strlen(s);
        } else {
            snprintf(s, len - (size_t)(s - s_entry), " dist_type: %d", dist_type); s += strlen(s);
        }
    }
    if (!wirecode) {
        // Wire mode prints bits/rate inside GH above; they mean nothing to
        // the other types, so an event carrying them without DIST_CRUSH
        // drops them from the wire form.
        _EPRINT_I(dist_bits, "dist_bits", "");
        _EPRINT_I(dist_rate, "dist_rate", "");
    }
    _EPRINT_F(dist_drive, "dist_drive", "GD");
    _EPRINT_F(dist_mix, "dist_mix", "GM");
    _EPRINT_I_SEQ(bp_is_set, "bp_is_set", MAX_BREAKPOINT_SETS, "??");
    // Convert these two at least to vectors of ints, save several hundred bytes
    _EPRINT_I_SEQ(algo_source, "algo_source", MAX_ALGO_OPS, "O");
    _EPRINT_BP(eg0_times, eg0_values, "eg0", "A");
    _EPRINT_BP(eg1_times, eg1_values, "eg1", "B");
    _EPRINT_I(eg_type[0], "eg_type[0]", "T");
    _EPRINT_I(eg_type[1], "eg_type[1]", "X");
    // Instrument-layer values.
    _EPRINT_I(synth, "synth", "i");
    _EPRINT_I(synth_flags, "synth_flags", "if");  // Special flags to set when defining instruments.
    _EPRINT_F(synth_level, "synth_level", "iV");  // Per-instrument level, scales every osc at render.
    _EPRINT_I(synth_delay_ms, "synth_delay_ms", "id");  // Extra delay added to synth note-ons to allow decay on voice-stealing.
    _EPRINT_I(to_synth, "to_synth", "it");  // For moving setup between synth numbers.
    _EPRINT_I(grab_midi_notes, "grab_midi_notes", "im");  // To enable/disable automatic MIDI note-on/off generating note-on/off.
    _EPRINT_I(note_source_channel, "note_source_channel", "iM");  // Marks synth as MIDI-driven, so note-on events aren't echo'd to output MIDI.
    _EPRINT_I(pedal, "pedal", "ip");  // MIDI pedal value.
    _EPRINT_I(num_voices, "num_voices", "iv");
    _EPRINT_I(oscs_per_voice, "oscs_per_voice", "in");
    //
    //_EPRINT_I(status, "status");
    _EPRINT_I(reset_osc, "reset_osc", "S");
    // Global effects
    _EPRINT_I(bus, "bus", "y");
    _EPRINT_F(volume, "volume", "V");
    _EPRINT_VALS_5(e->eq_l, e->eq_m, e->eq_h, AMY_UNSET_FLOAT, AMY_UNSET_FLOAT, "eq_{l,m,h}", "x");
    _EPRINT_VALS_5(e->echo_level, e->echo_delay_ms, e->echo_max_delay_ms, e->echo_feedback, e->echo_filter_coef, "echo_{level,delay,max,fb,filt}", "M");
    _EPRINT_VALS_5(e->chorus_level, e->chorus_max_delay, e->chorus_lfo_freq, e->chorus_depth, AMY_UNSET_FLOAT, "chorus_{level,delay,lfo,depth}", "k");
    _EPRINT_VALS_5(e->reverb_level, e->reverb_liveness, e->reverb_damping, e->reverb_xover_hz, AMY_UNSET_FLOAT, "reverb_{level,live,damp,xover}", "h");

    if (wirecode && (s - s_entry) > 0) { snprintf(s, len - (size_t)(s - s_entry), "Z"); s += strlen(s); }

    assert( ((size_t)(s - s_entry)) < len);  // if we corrupted memory, at least we'll abort.
    return s - s_entry;
}

void fprintf_event_stderr(amy_event *e) {
    if (e == NULL) return;
    char s[1024];
    sprint_event(e, s, 1024, /* wirecode */ false);
    fprintf(stderr, "%s\n", s);
}

#define _RET_TRUE_IF_SET(FIELD) if (AMY_IS_SET(e->FIELD)) return true;
#define _RET_TRUE_IF_SET_SEQ(FIELD, LEN)          \
    for (int i = 0; i < LEN; ++i) {               \
        _RET_TRUE_IF_SET(FIELD[i]);               \
    }
#define _RET_TRUE_IF_SET_COEF(FIELD)   _RET_TRUE_IF_SET_SEQ(FIELD, NUM_COMBO_COEFS)
#define _RET_TRUE_IF_SET_BP(TFIELD, VFIELD) {        \
        _RET_TRUE_IF_SET_SEQ(TFIELD, MAX_BPS)        \
        _RET_TRUE_IF_SET_SEQ(VFIELD, MAX_BPS)        \
    }
#define _RET_TRUE_IF_5_F_SET(VAL1, VAL2, VAL3, VAL4, VAL5)  \
    if (AMY_IS_SET((float)(e->VAL1)) || AMY_IS_SET((float)(e->VAL2)) || AMY_IS_SET((float)(e->VAL3)) || AMY_IS_SET((float)(e->VAL4)) || AMY_IS_SET((float)(e->VAL5))) return true;

#define _TRUE_IF_F_UNSET(VAL1)                  \
    AMY_IS_UNSET((float)(VAL1))
#define _TRUE_IF_5_F_UNSET(VAL1, VAL2, VAL3, VAL4, VAL5)  \
    (AMY_IS_UNSET((float)(VAL1)) && AMY_IS_UNSET((float)(VAL2)) && AMY_IS_UNSET((float)(VAL3)) && AMY_IS_UNSET((float)(VAL4)) && AMY_IS_UNSET((float)(VAL5)))


bool event_addresses_bus(amy_event *e) {
    // Global effects
    //is_empty &= AMY_IS_UNSET(e->bus);
    _RET_TRUE_IF_SET(volume);
    _RET_TRUE_IF_5_F_SET(eq_l, eq_m, eq_h, eq_h, eq_h);
    _RET_TRUE_IF_5_F_SET(echo_level, echo_delay_ms, echo_max_delay_ms, echo_feedback, echo_filter_coef);
    _RET_TRUE_IF_5_F_SET(chorus_level, chorus_max_delay, chorus_lfo_freq, chorus_depth, chorus_depth);
    _RET_TRUE_IF_5_F_SET(reverb_level, reverb_liveness, reverb_damping, reverb_xover_hz, reverb_xover_hz);
    return false;
}

bool event_addresses_synth(amy_event *e) {
    // Instrument-layer values.
    //_RET_TRUE_IF_SET(synth);
    _RET_TRUE_IF_SET(synth_flags);  // Special flags to set when defining instruments.
    _RET_TRUE_IF_SET(synth_level);  // Per-instrument level.
    _RET_TRUE_IF_SET(synth_delay_ms);  // Extra delay added to synth note-ons to allow decay on voice-stealing.
    _RET_TRUE_IF_SET(to_synth);  // For moving setup between synth numbers.
    _RET_TRUE_IF_SET(grab_midi_notes);  // To enable/disable automatic MIDI note-on/off generating note-on/off.
    _RET_TRUE_IF_SET(note_source_channel);  // Marks synth as MIDI-driven, so note-on events aren't echo'd to output MIDI.
    _RET_TRUE_IF_SET(pedal);  // MIDI pedal value.
    _RET_TRUE_IF_SET(num_voices);
    _RET_TRUE_IF_SET(oscs_per_voice);
    return false;
}

bool event_addresses_oscs(amy_event *e) {
    // We don't want to go through the rigmarole of passing events down to voices if the instruments haven't been set up yet.
    // Check to see if this event has any fields set other than time, osc, synth.
    _RET_TRUE_IF_SET(wave);
    _RET_TRUE_IF_SET(preset);
    _RET_TRUE_IF_SET(midi_note);
    _RET_TRUE_IF_SET(velocity);
    _RET_TRUE_IF_SET(patch_number);
    _RET_TRUE_IF_SET_COEF(amp_coefs);
    _RET_TRUE_IF_SET_COEF(freq_coefs);
    _RET_TRUE_IF_SET_COEF(filter_freq_coefs);
    _RET_TRUE_IF_SET_COEF(duty_coefs);
    _RET_TRUE_IF_SET_COEF(pan_coefs);
    _RET_TRUE_IF_SET(feedback);
    _RET_TRUE_IF_SET(trigger_phase);
    _RET_TRUE_IF_SET(sample_offset);
    _RET_TRUE_IF_SET(fit_ticks);
    _RET_TRUE_IF_SET(fit_search);
    _RET_TRUE_IF_SET(pitch_bend);  // NOT osc-dep
    _RET_TRUE_IF_SET(tempo);  // NOT osc-dep
    _RET_TRUE_IF_SET(latency_ms);  // NOT osc-dep
    _RET_TRUE_IF_SET(ratio);
    _RET_TRUE_IF_SET(resonance);
    _RET_TRUE_IF_SET(portamento_ms);
    _RET_TRUE_IF_SET(chained_osc);
    _RET_TRUE_IF_SET_SEQ(mod_source, NUM_MOD_SOURCES);
    _RET_TRUE_IF_SET(algorithm);
    _RET_TRUE_IF_SET(filter_type);
    // Not _RET_TRUE_IF_5_F_SET: type/bits/rate are ints, and their unset
    // sentinels cast to ordinary floats rather than NaN.
    _RET_TRUE_IF_SET(dist_type);
    _RET_TRUE_IF_SET(dist_drive);
    _RET_TRUE_IF_SET(dist_bits);
    _RET_TRUE_IF_SET(dist_rate);
    _RET_TRUE_IF_SET(dist_mix);
    _RET_TRUE_IF_SET_SEQ(bp_is_set, MAX_BREAKPOINT_SETS);
    // Convert these two at least to vectors of ints, save several hundred bytes
    _RET_TRUE_IF_SET_SEQ(algo_source, MAX_ALGO_OPS);
    _RET_TRUE_IF_SET_BP(eg0_times, eg0_values);
    _RET_TRUE_IF_SET_BP(eg1_times, eg1_values);
    _RET_TRUE_IF_SET(eg_type[0]);
    _RET_TRUE_IF_SET(eg_type[1]);
    // We don't know
    _RET_TRUE_IF_SET_SEQ(ticks, 3); // tick, period, tag
    //
    //_RET_TRUE_IF_SET(status, "status");
    _RET_TRUE_IF_SET(reset_osc);
    // Bus is an osc parameter, so if it's a bus command, we should process it.
    // Events with bus but no osc, voice, or synth are bus-directed, but they are filtered from default osc?
    _RET_TRUE_IF_SET(bus);
    return false;   // None of the osc-addressing fields were set.
}

#define _CASE_I(FIELD, PARAM) case PARAM: event->FIELD = queue->data.i; break;
#define _CASE_F(FIELD, PARAM) case PARAM: event->FIELD = queue->data.f; break;
#define _CASE_LOG(FIELD, PARAM) case PARAM: event->FIELD = exp2f(queue->data.f); break;
#define _TEST_COEFS(FIELD, PARAM)  \
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) {                          \
        if ((int)queue->param == (int)PARAM + i) event->FIELD[i] = queue->data.f; \
    }
// Const freq coef is in Hz, rest are linear.
#define _TEST_FREQ_COEFS(FIELD, PARAM) \
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) {      \
        if ((int)queue->param == (int)PARAM + i) {   \
            if (i == COEF_CONST)  \
                event->FIELD[i] = freq_of_logfreq(queue->data.f);   \
            else \
                event->FIELD[i] = queue->data.f; \
        }                                    \
    }

struct delta *deltas_to_event(struct delta *queue, struct amy_event *event) {
  // Consume deltas from queue and push into event.  Return pointer to first non-consumed delta.
  if (queue == NULL) return NULL;
  event->osc = queue->osc;
  event->time = queue->time;
  uint32_t breakpoint_times[MAX_BREAKPOINT_SETS][MAX_BREAKPOINTS];
  float breakpoint_values[MAX_BREAKPOINT_SETS][MAX_BREAKPOINTS];
  for (int i = 0; i < MAX_BREAKPOINT_SETS; ++i) {
      for (int j = 0; j < MAX_BREAKPOINTS; ++j) {
          AMY_UNSET(breakpoint_times[i][j]);
          AMY_UNSET(breakpoint_values[i][j]);
      }
  }
  int highest_breakpoint[MAX_BREAKPOINT_SETS] = {-1, -1};
  while(queue != NULL) {
    if (queue->osc != event->osc || queue->time != event->time)  break;  // delta doesn't fit this event.
    switch (queue->param) {
      _CASE_I(wave, WAVE)
      _CASE_I(preset, PRESET)
      _CASE_F(midi_note, MIDI_NOTE)
      _CASE_F(feedback, FEEDBACK)
      _CASE_F(trigger_phase, PHASE)
      _CASE_I(sample_offset, SAMPLE_OFFSET)
      _CASE_F(fit_ticks, FIT)
      _CASE_I(fit_search, FIT_SEARCH)
      _CASE_F(pitch_bend, PITCH_BEND)
      _CASE_I(latency_ms, LATENCY)
      _CASE_F(tempo, TEMPO)
      _CASE_LOG(ratio, RATIO)
      _CASE_F(resonance, RESONANCE)
      _CASE_I(portamento_ms, PORTAMENTO)
      _CASE_I(chained_osc, CHAINED_OSC)
      _CASE_I(reset_osc, RESET_OSC)
      _CASE_I(note_source_channel, NOTE_SOURCE_CHANNEL)
      _CASE_I(filter_type, FILTER_TYPE)
      _CASE_I(dist_type, DIST_TYPE)
      _CASE_F(dist_drive, DIST_DRIVE)
      _CASE_I(dist_bits, DIST_BITS)
      _CASE_I(dist_rate, DIST_RATE)
      _CASE_F(dist_mix, DIST_MIX)
      _CASE_I(algorithm, ALGORITHM)
      _CASE_F(eq_l, EQ_L)
      _CASE_F(eq_m, EQ_M)
      _CASE_F(eq_h, EQ_H)
      _CASE_F(echo_max_delay_ms, ECHO_MAX_DELAY_MS)
      _CASE_F(echo_level, ECHO_LEVEL)
      _CASE_F(echo_delay_ms, ECHO_DELAY_MS)
      _CASE_F(echo_feedback, ECHO_FEEDBACK)
      _CASE_F(echo_filter_coef, ECHO_FILTER_COEF)
      _CASE_F(chorus_max_delay, CHORUS_MAX_DELAY)
      _CASE_F(chorus_level, CHORUS_LEVEL)
      _CASE_F(chorus_lfo_freq, CHORUS_LFO_FREQ)
      _CASE_F(chorus_depth, CHORUS_DEPTH)
      _CASE_F(reverb_level, REVERB_LEVEL)
      _CASE_F(reverb_liveness, REVERB_LIVENESS)
      _CASE_F(reverb_damping, REVERB_DAMPING)
      _CASE_F(reverb_xover_hz, REVERB_XOVER_HZ)
      _CASE_I(eg_type[0], EG0_TYPE)
      _CASE_I(eg_type[1], EG1_TYPE)
      _CASE_F(velocity, VELOCITY)
      _CASE_I(bus, BUS)
      // A VOLUME delta names its bus in osc, like the other bus-directed
      // params, so it comes back as "this bus, one volume".
      case VOLUME: event->bus = queue->osc; event->volume = queue->data.f; break;
    default:  // blocks, not handled by case
      _TEST_COEFS(amp_coefs, AMP)
      _TEST_FREQ_COEFS(freq_coefs, FREQ)
      _TEST_FREQ_COEFS(filter_freq_coefs, FILTER_FREQ)
      _TEST_COEFS(duty_coefs, DUTY)
      _TEST_COEFS(pan_coefs, PAN)
      for (int i = 0; i < MAX_ALGO_OPS; ++i) {
          if ((int)queue->param == (int)ALGO_SOURCE_START + i)
              event->algo_source[i] = queue->data.i;
      }
      for (int i = 0; i < NUM_MOD_SOURCES; ++i) {
          if ((int)queue->param == (int)MOD_SOURCE_START + i)
              event->mod_source[i] = queue->data.i;
      }
      for (int i = 0; i < MAX_BREAKPOINT_SETS; ++i) {
          for (int j = 0; j < MAX_BREAKPOINTS; ++j) {
              if ((int)queue->param == (int)BP_START + (j * 2) + (i * MAX_BREAKPOINTS * 2)) {
                  //event->bp[i].time[j] = queue->data.i;
                  breakpoint_times[i][j] = queue->data.i;
                  if (j > highest_breakpoint[i]) highest_breakpoint[i] = j;
              }
              else if ((int)queue->param == (int)BP_START + (j * 2 + 1) + (i * MAX_BREAKPOINTS * 2)) {
                  //event->bp[i].value[j] = queue->data.f;
                  breakpoint_values[i][j] = queue->data.f;
                  if (j > highest_breakpoint[i]) highest_breakpoint[i] = j;
              }
          }
      }
      break;
    }
    queue = queue->next;
  }
  uint32_t *bp_times_ms[MAX_BREAKPOINT_SETS] = {event->eg0_times, event->eg1_times};
  float *bp_values[MAX_BREAKPOINT_SETS] = {event->eg0_values, event->eg1_values};
  for (int i = 0; i < MAX_BREAKPOINT_SETS; ++i) {
      if (highest_breakpoint[i] >= 0) {
          event->bp_is_set[i] = 1;
          for (int j = 0; j <= highest_breakpoint[i]; ++j) {
              if (AMY_IS_SET(breakpoint_times[i][j])) {
                  float t_ms = ((float)breakpoint_times[i][j]) * 1000.0f / (float)AMY_SAMPLE_RATE;
                  int32_t t_rounded = (int32_t)roundf(t_ms);
                  if (t_rounded < 0) t_rounded = 0;
                  if (t_rounded >= SHRT_MAX) t_rounded = SHRT_MAX - 1;
                  bp_times_ms[i][j] = (int16_t)t_rounded;
              }
              if (AMY_IS_SET(breakpoint_values[i][j])) {
                  bp_values[i][j] = breakpoint_values[i][j];
              }
          }
      }
  }
  return queue;
}

#define EVENT_FROM_OSC(FIELD)  \
    if (synth[osc]->FIELD != empty_synth.FIELD)  \
        event->FIELD = synth[osc]->FIELD;

#define EVENT_FROM_OSC_BASEOSC(FIELD)  \
    if (synth[osc]->FIELD != empty_synth.FIELD)  \
        event->FIELD = synth[osc]->FIELD - base_osc;

#define EVENT_FROM_OSC_MAPPED(SYNTH_FIELD, EVENT_FIELD, MAP_FN)        \
    if (synth[osc]->SYNTH_FIELD != empty_synth.SYNTH_FIELD)            \
        event->EVENT_FIELD = MAP_FN(synth[osc]->SYNTH_FIELD);

#define EVENT_FROM_OSC_ARRAY(FIELD, NUM_ELS)                           \
    for (int i = 0; i < NUM_ELS; ++i) {                                \
        if (synth[osc]->FIELD[i] != empty_synth.FIELD[i])              \
            event->FIELD[i] = synth[osc]->FIELD[i];                    \
    }

#define EVENT_FROM_OSC_ARRAY_BASEOSC(FIELD, NUM_ELS)                   \
    for (int i = 0; i < NUM_ELS; ++i) {                                \
        if (synth[osc]->FIELD[i] != empty_synth.FIELD[i])              \
            event->FIELD[i] = synth[osc]->FIELD[i] - base_osc;          \
    }

#define EVENT_FROM_OSC_ARRAY2(SYNTH_FIELD, EVENT_FIELD, NUM_ELS)       \
    for (int i = 0; i < NUM_ELS; ++i) {                                \
        if (synth[osc]->SYNTH_FIELD[i] != empty_synth.SYNTH_FIELD[i])  \
            event->EVENT_FIELD[i] = synth[osc]->SYNTH_FIELD[i];        \
    }

#define EVENT_FROM_OSC_ARRAY_T(SYNTH_FIELD, EVENT_FIELD, NUM_ELS)      \
    for (int i = 0; i < NUM_ELS; ++i) {                                \
        if (synth[osc]->SYNTH_FIELD[i] != empty_synth.SYNTH_FIELD[i])  \
            event->EVENT_FIELD[i] = lroundf(((float)synth[osc]->SYNTH_FIELD[i]) / (AMY_SAMPLE_RATE / 1000.0f)); \
    }

#define EVENT_FROM_OSC_ARRAY_FREQ(SYNTH_FIELD, EVENT_FIELD, NUM_ELS)      \
    for (int i = 0; i < NUM_ELS; ++i) {                                   \
        if (synth[osc]->SYNTH_FIELD[i] != empty_synth.SYNTH_FIELD[i]) {   \
            if (i == COEF_CONST)                                          \
                event->EVENT_FIELD[i] = freq_of_logfreq(synth[osc]->SYNTH_FIELD[i]); \
            else                                                          \
                event->EVENT_FIELD[i] = synth[osc]->SYNTH_FIELD[i];       \
        }                                                                 \
    }

void set_event_for_osc(int base_osc, int rel_osc, struct amy_event *event) {
    // Set fields in the event to configure the osc away from default.
    // We assume event has already been cleared.
    // We do not set the osc field of the event.
    int osc = base_osc + rel_osc;
    // synth[] entries are NULL until ensure_osc_allocd() allocates them, and a
    // voice can own oscs that were never touched -- a synth configured with
    // oscs_per_voice= and no patch reserves the oscs without allocating any of
    // them.  Reading such an osc's config is not an error: it is at its
    // defaults, so there is nothing to emit.  Dereferencing it, which is what
    // this did, segfaulted amy_get_synth_commands()/amy_dump_state() for every
    // such synth.
    if (osc < 0 || osc >= AMY_OSCS || synth[osc] == NULL)  return;
    // Generate the reference "empty synth".
    struct synthinfo empty_synth;
    // We need to have space for the breakpoints.
    uint32_t times[MAX_BREAKPOINT_SETS * MAX_BREAKPOINTS];
    float values[MAX_BREAKPOINT_SETS * MAX_BREAKPOINTS];
    for (int i = 0; i < MAX_BREAKPOINT_SETS; ++i) {
        empty_synth.max_num_breakpoints[i] = synth[osc]->max_num_breakpoints[i];
        empty_synth.breakpoint_times[i] = times + i * MAX_BREAKPOINTS;
        empty_synth.breakpoint_values[i] = values + i * MAX_BREAKPOINTS;
   }
    reset_osc_by_pointer(&empty_synth, /* msynth */ NULL);
    // Go through parameter fields picking out the ones that are nondefault.
    EVENT_FROM_OSC(wave);
    EVENT_FROM_OSC(preset);
    // Note and velocity are special for "note on" events, don't reflect them in config.
    //EVENT_FROM_OSC(midi_note);
    //EVENT_FROM_OSC(velocity);
    EVENT_FROM_OSC_ARRAY(amp_coefs, NUM_COMBO_COEFS);
    EVENT_FROM_OSC_ARRAY_FREQ(logfreq_coefs, freq_coefs, NUM_COMBO_COEFS);
    EVENT_FROM_OSC_ARRAY_FREQ(filter_logfreq_coefs, filter_freq_coefs, NUM_COMBO_COEFS);
    EVENT_FROM_OSC_ARRAY(duty_coefs, NUM_COMBO_COEFS);
    EVENT_FROM_OSC_ARRAY(pan_coefs, NUM_COMBO_COEFS);
    EVENT_FROM_OSC(feedback);
    EVENT_FROM_OSC(trigger_phase);
    EVENT_FROM_OSC(sample_offset);
    EVENT_FROM_OSC(fit_ticks);
    EVENT_FROM_OSC(fit_search);
    EVENT_FROM_OSC_MAPPED(logratio, ratio, exp2f);
    EVENT_FROM_OSC(resonance);
    EVENT_FROM_OSC_MAPPED(portamento_alpha, portamento_ms, alpha_to_portamento_ms);
    EVENT_FROM_OSC_BASEOSC(chained_osc);
    EVENT_FROM_OSC_ARRAY_BASEOSC(mod_source, NUM_MOD_SOURCES);
    EVENT_FROM_OSC(algorithm);
    EVENT_FROM_OSC(filter_type);
    EVENT_FROM_OSC_ARRAY_BASEOSC(algo_source, MAX_ALGO_OPS);
    EVENT_FROM_OSC_ARRAY_T(breakpoint_times[0], eg0_times, synth[osc]->max_num_breakpoints[0]);
    EVENT_FROM_OSC_ARRAY2(breakpoint_values[0], eg0_values, synth[osc]->max_num_breakpoints[0]);
    EVENT_FROM_OSC_ARRAY_T(breakpoint_times[1], eg1_times, synth[osc]->max_num_breakpoints[1]);
    EVENT_FROM_OSC_ARRAY2(breakpoint_values[1], eg1_values, synth[osc]->max_num_breakpoints[1]);
    EVENT_FROM_OSC_ARRAY(eg_type, MAX_BREAKPOINT_SETS);
}

float lin_to_db(float lin) {
    return 20.0f * log10f(lin);
}

void set_event_for_bus_fx(amy_event *event, uint16_t bus, global_state_t *state) {
    // Always emit all FX fields so saved patches are fully self-describing.
    // Set the bus
    event->bus = bus;
    event->volume = state->volume[bus];
    // EQ
    event->eq_l = lin_to_db(S2F(state->bus[bus]->eq.eq[0]));
    event->eq_m = lin_to_db(S2F(state->bus[bus]->eq.eq[1]));
    event->eq_h = lin_to_db(S2F(state->bus[bus]->eq.eq[2]));
    // Reverb
    event->reverb_level = S2F(state->bus[bus]->reverb.level);
    event->reverb_liveness = state->bus[bus]->reverb.liveness;
    event->reverb_damping = state->bus[bus]->reverb.damping;
    event->reverb_xover_hz = state->bus[bus]->reverb.xover_hz;
    // Chorus
    event->chorus_level = S2F(state->bus[bus]->chorus.level);
    event->chorus_max_delay = state->bus[bus]->chorus.max_delay;
    event->chorus_lfo_freq = state->bus[bus]->chorus.lfo_freq;
    event->chorus_depth = state->bus[bus]->chorus.depth;
    // Echo
    event->echo_level = S2F(state->bus[bus]->echo.level);
    event->echo_delay_ms = state->bus[bus]->echo.delay_samples * 1000.f / AMY_SAMPLE_RATE;
    if (state->bus[bus]->echo.max_delay_samples != (uint32_t)(ECHO_DEFAULT_MAX_DELAY_MS / 1000.f * AMY_SAMPLE_RATE))
        event->echo_max_delay_ms = state->bus[bus]->echo.max_delay_samples * 1000.f / AMY_SAMPLE_RATE;
    event->echo_feedback = S2F(state->bus[bus]->echo.feedback);
    event->echo_filter_coef = S2F(state->bus[bus]->echo.filter_coef);
}


int num_oscs_for_voice(int voice) {
    uint16_t osc = voice_to_base_osc[voice];
    int num_oscs = 0;
    while(osc < amy_global.config.max_oscs
          && osc_to_voice[osc] == voice) {
        ++num_oscs;
        ++osc;
    }
    return num_oscs;
}

void *yield_synth_events(uint8_t instr_num, struct amy_event *event, bool include_fx, void *state) {
    // Return a sequence of events defining a synth.
    // state = NULL on first call and it returns state to be passed on next call.  Returns NULL when event sequence is finished.
   // Find oscs for synth.
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    int num_voices = instrument_get_num_voices(instr_num, voices);
    if (num_voices < 1) {
        fprintf(stderr, "yield_synth_events: synth %" PRId32" has no voices.\n", (int32_t)instr_num);
        return NULL;  // instrument not allocated.
    }
    uint32_t flags = instrument_get_flags(instr_num);
    uint16_t bus = instrument_get_bus(instr_num);
    uint16_t voice = voices[0];
    uint16_t base_osc = voice_to_base_osc[voice];
    int num_oscs = num_oscs_for_voice(voice);
    // The "state" indicates which osc within the voice we're going to report for.
    int state_val = (intptr_t)state;
    //fprintf(stderr, "yield_synth_events(%d) voice=%d num_oscs=%d state_val=%d\n", instr_num, voice, num_oscs, (int)state_val);
    amy_clear_event(event);
    // Always use first output (state val 0) as preamble.
    int first_osc_state_val = 1;
    int last_osc_state_val = num_oscs + 1;
    if (state_val == 0) {
    // Always emit a preamble with num_voices and oscs_per_voice
    // We could include the patch but we don't, because we're going to set the values by hand.
        event->num_voices = instrument_get_num_voices(instr_num, NULL);
        event->oscs_per_voice = instrument_get_oscs_per_voice(instr_num);
        if (flags != 0)  event->synth_flags = flags;
        float level = instrument_get_level(instr_num);
        if (level != 1.0f)  event->synth_level = level;
        // Bus is specified in set_event_for_bus_fx, no need to include here.
        if (!include_fx && bus != 0)  event->bus = bus;
    } else if (state_val >= first_osc_state_val && state_val < last_osc_state_val) {
        event->osc = state_val - first_osc_state_val;
        //fprintf(stderr, "2 base_osc %d, event->osc %d, state_val %d first_osc_state_val %d last_osc_state_val %d\n",
        //    base_osc, event->osc, state_val, first_osc_state_val, last_osc_state_val);
        set_event_for_osc(base_osc, event->osc, event);
    } else if (include_fx && (state_val == last_osc_state_val)) {
        // optional final event contains the global/bus settings (volume, eq, chorus, echo, reverb).
        // The bus is determined by the bus value on the base_osc, probably OK.
        set_event_for_bus_fx(event, instrument_get_bus(instr_num), &amy_global);
    }
    ++state_val;
    if (state_val == last_osc_state_val + (include_fx ? 1 : 0))  state_val = 0;  // Indicate this is the final event.
    return (void *)((intptr_t)state_val);
}

#define STATE_START_OF_MIDI_TPLT_CMDS 1024

void *yield_synth_commands(uint8_t instr_num, char *s, size_t len, bool include_fx, void *state) {
    // Generator to return multiple wirecode strings to reconfigure a synth.
    int state_val = (intptr_t)state;
    //fprintf(stderr, "yield_synth_commands: synth %d state %d\n", instr_num, state_val);
    s[0] = '\0';  // By default, return an empty string.
    if (state_val < STATE_START_OF_MIDI_TPLT_CMDS) {
        amy_event event = amy_default_event();
        state_val = (intptr_t)yield_synth_events(instr_num, &event, include_fx, (void *)(intptr_t)state_val);
        sprint_event(&event, s, len, /* wirecode= */ true);
        if (state_val == 0) {
            // Push the state machine on to the MIDI codes
            state_val = STATE_START_OF_MIDI_TPLT_CMDS;
        }
    } else {
        // MIDI CC part
        bool found = false;
        int type = MIDI_MAP_TYPE_CC;
        int starting_code = state_val - STATE_START_OF_MIDI_TPLT_CMDS;
        for (int next_code = starting_code; next_code < 256; ++next_code) {
            int next_midi_code = next_code;
            if (next_midi_code >= 128) {
                next_midi_code -= 128;
                type = MIDI_MAP_TYPE_NOTE;
            }
            if (midi_fetch_mapping_command(instr_num, type, next_midi_code, s, len) == true) {
                state_val = STATE_START_OF_MIDI_TPLT_CMDS + next_code + 1;
                found = true;
                break;
            }
        }
        if (found == false) {
            // We hit the bottom of the MIDI CCs
            state_val = 0;  // Will terminate the yield cycle.
        }
    }
    return (void *)(intptr_t)state_val;
}


void *yield_bus_commands(char *s, size_t len, void *state) {
    // Like yield_synth_commands, returns just the commands for the FX
    int state_val = (intptr_t)state;
    if (state_val > amy_global.highest_bus) {
        state_val = 0;
    } else {
        // Return a wire command to set up a bus.
        uint16_t bus = state_val;
        amy_event e = amy_default_event();
        set_event_for_bus_fx(&e, bus, &amy_global);
        sprint_event(&e, s, len, /* wirecode= */ true);
        ++state_val;
    }
    return (void *)(intptr_t)state_val;
}


void parse_patch_string_to_queue(char *message, int base_osc, uint16_t oscs_per_voice, struct delta **queue, uint8_t synth, uint32_t time, bool is_first_voice) {
    // Work though the patch string and send to voices.
    // Now actually initialize the newly-allocated osc blocks with the patch
    //fprintf(stderr, "parse_patch_string: message %s base_osc %d synth %d time %d is_first_voice %d\n", message, base_osc, synth, time, is_first_voice);
    amy_event e;
    size_t pos = 0;
    // A patch string can re-shape the voice it is being loaded into, partway
    // through itself: the drum kits open with `if3iv1in38Z`, which turns the
    // 1-osc voice patch_oscs promised into a 38-osc one before the per-drum
    // commands that follow. So the bound the rest of the string is held to is
    // whatever the string last said, not what we were told before it ran.
    uint16_t voice_oscs = oscs_per_voice;
    do {
        amy_clear_event(&e);
        e.time = time;
        if (message[pos] == 'i') {
            // It's a synth-layer message, it needs a synth defined.
            e.synth = synth;
        }
        pos = yield_event_from_message(message, &e, pos);
        if (pos > 0) {
            // This event re-shapes the voice; everything after it answers to
            // the new size. (Taking effect from this event on, not the next
            // one, is right: the event that carries oscs_per_voice is
            // synth-level and names no oscs of its own.)
            if (AMY_IS_SET(e.oscs_per_voice))  voice_oscs = e.oscs_per_voice;
            // A patch's global FX phrase (a Juno patch's trailing
            // "x...k1..." -- 127 of the ROM Juno patches carry one) is
            // bus-directed but has no synth attached, so its params fell
            // to the default bus 0.  Give it the synth being loaded and
            // the bus resolves from the synth, exactly as if the synth
            // had sent it itself.
            if (event_addresses_bus(&e))  e.synth = synth;
            if (event_addresses_oscs(&e) || is_first_voice)
                amy_event_to_deltas_queue(&e, base_osc, voice_oscs, queue);
        }
    } while (pos > 0);
}

// So emscripten knows how big to make this struct.
int size_of_amy_event(void) {
    return sizeof(amy_event);
}

void patches_store_patch(amy_event *e, char * patch_string) {
    peek_stack("store_patch");
    // amy patch string. Either pull patch_number from e, or allocate a new one and write it to e.
    // Patch is stored in ram.
    //fprintf(stderr, "store_patch: synth %d patch_num %d patch '%s'\n", e->synth, e->patch, patch_string);
    bool auto_assigned = !AMY_IS_SET(e->patch_number);
    if (auto_assigned) {
        // We need a number, and it only has to live long enough to carry this
        // patch_string into patches_load_patch, which frees the slot as soon as
        // it has loaded it. So: take any free slot (never used, or handed back
        // by an earlier load), else refuse — the deliberately out-of-range
        // number falls to the range check below, which prints and drops the
        // store.
        for (uint32_t i = 0; i < max_num_memory_patches; ++i) {
            if (memory_patch_deltas[i] == NULL && memory_patch_oscs[i] == 0) {
                e->patch_number = i + _PATCHES_FIRST_USER_PATCH;
                break;
            }
        }
        if (!AMY_IS_SET(e->patch_number))
            e->patch_number = _PATCHES_FIRST_USER_PATCH + max_num_memory_patches;
        //fprintf(stderr, "store_patch: auto-assigning patch number %d for '%s'\n", e->patch_number, patch_string);
    }
    int patch_index = (int)e->patch_number - _PATCHES_FIRST_USER_PATCH;
    if (patch_index < 0 || patch_index >= (int)max_num_memory_patches) {
        fprintf(stderr, "patch number %" PRId32 " is out of range (%" PRId32 " .. %" PRId32 ")\n",
                (int32_t)(patch_index + _PATCHES_FIRST_USER_PATCH),
                (int32_t)_PATCHES_FIRST_USER_PATCH,
                (int32_t)(_PATCHES_FIRST_USER_PATCH + (int32_t)max_num_memory_patches));
        // Don't leave the rejected (possibly auto-assigned) number in the
        // event, or the rest of event handling would try to load it.
        AMY_UNSET(e->patch_number);
        return;
    }
    if (patch_index >= next_user_patch_index)  next_user_patch_index = patch_index + 1;
    // A re-store REPLACES the slot's contents. parse_patch_string_to_queue
    // appends to whatever list is standing, so without this a second store
    // to the same number merged two patches into one slot.
    if (memory_patch_deltas[patch_index] != NULL || memory_patch_oscs[patch_index])
        patches_reset_patch(e->patch_number);
    memory_patch_auto[patch_index] = auto_assigned ? 1 : 0;
    // Store the patch as deltas and find out how many oscs this message uses
    parse_patch_string_to_queue(patch_string, 0, /* oscs_per_voice= */ 0, &memory_patch_deltas[patch_index], e->synth, e->time, true);
    update_num_oscs_for_patch_number(patch_index + _PATCHES_FIRST_USER_PATCH);
    //fprintf(stderr, "store_patch: patch %d max_osc %d patch %s #deltas %d (e->num_vx=%d)\n", patch_index, max_osc, patch_string, delta_list_len(memory_patch_deltas[patch_index]), e->num_voices);
}

extern int32_t parse_list_uint16_t(char *message, uint16_t *vals, int32_t max_num_vals, uint16_t skipped_val);


int copy_voices(uint16_t *from, uint16_t *to) {
    // Copy voice vectors up until first unset; return how many copied.
    int num_voices = 0;
    for (int i = 0; i < MAX_VOICES_PER_INSTRUMENT; ++i) {
        if (AMY_IS_SET(from[i])) {
            to[num_voices] = from[i];
            ++num_voices;
        } else {
            break;
        }
    }
    return num_voices;
}


uint8_t patches_voices_for_note_onoff_event(amy_event *e, uint16_t voices[], uint32_t synth_flags, bool *pstolen) {
    // Identify the specific voice (or voices) for a note on/off event.
    // e->velocity is assumed to be set when this function is called.
    int num_voices = 0;
    if (AMY_IS_UNSET(e->midi_note) && AMY_IS_UNSET(e->preset) && instrument_get_num_voices(e->synth, NULL) != 1) {
        // velocity without midi_note is valid for velocity==0 => all-notes-off.
        if (e->velocity != 0) {
            // Attempted a note-on to all voices, suppress.
            fprintf(stderr, "note-on with no note for synth %" PRId32 " - ignored.\n", (int32_t)e->synth);
            return 0;
        }
        // All notes off - find out which voices are actually currently active, so we can turn them off.
        num_voices = instrument_all_notes_off(e->synth, voices);
    } else {
        // It's a note-on or note-off event, so the instrument mechanism chooses which single voice to use.
        uint16_t note = 0;
        if (AMY_IS_SET(e->midi_note))  // midi note can be unset if preset is set.
            note = (uint8_t)roundf(e->midi_note);
        if (AMY_IS_SET(e->preset)) {
            // This event includes a note *and* a preset, so it's like a drum sample note on.
            // Wrap the preset number into the note, so we don't allocate the same pitch for different drums to the same voice.
            note += 128 * e->preset;
        }
        bool is_note_off = (e->velocity == 0);
        voices[0] = instrument_voice_for_note_event(e->synth, note, is_note_off, pstolen);
        if (voices[0] == _INSTRUMENT_NO_VOICE) {
            // For now, I think this can only happen with a note-off that has no matching note-on.
            //fprintf(stderr, "synth %d did not find a voice, dropping message.\n", e->synth);
            // No, it also happens with note-offs when pedal is down.
            return 0;
        }
        num_voices = 1;
    }
    //fprintf(stderr, "instrument %d vel %d note %d voice %d\n", e->synth, (int)roundf(127.f * e->velocity), (int)roundf(e->midi_note), voices[0]);
    return num_voices;
}


void patches_grab_synth_tier(amy_event *e) {
    // Extract synth-tier parameters from the event before anything else.
    if (!AMY_IS_SET(e->synth)) {
        // No instrument
        return;
    }
    if (AMY_IS_SET(e->to_synth)) {
        // This involves moving the instrument number.
        instrument_change_number(e->synth, e->to_synth);
        e->synth = e->to_synth;
        AMY_UNSET(e->to_synth);
        // Then continue handling any other args.
    }
    if (AMY_IS_SET(e->bus)) {
        instrument_set_bus(e->synth, e->bus);
        // Set the bus for the instrument, but also for each osc of each voice (handled by the caller).
    }
    if (AMY_IS_SET(e->synth_delay_ms)) {
        // Set the synth noteon delay.
        instrument_set_noteon_delay_ms(e->synth, e->synth_delay_ms);
    }
    if (AMY_IS_SET(e->synth_flags)) {
        instrument_set_flags(e->synth, e->synth_flags);
    }
    if (AMY_IS_SET(e->synth_level)) {
        // Per-instrument level (iV): stored on the instrument, applied to
        // every one of its oscs at render time (hold_and_modify).
        instrument_set_level(e->synth, e->synth_level);
    }
    if (AMY_IS_SET(e->grab_midi_notes)) {
        // Set the grab_midi state.
        instrument_set_grab_midi_notes(e->synth, e->grab_midi_notes);
    }
}
uint8_t patches_voices_for_event(amy_event *e, uint16_t voices[]) {
    // Convert an event that may specify a synth into a number of specific voices.
    uint8_t num_voices = 0;
    uint32_t synth_flags = 0;
    // The synth may have been CONSUMED earlier in this event's processing:
    // releasing an instrument (num_voices=0) unsets e->synth precisely so
    // the rest of the event is not forwarded to it
    // (patches_voices_for_load_synth). Without this guard the unset
    // sentinel (255) reached instrument_get_num_voices below and printed
    // "instrument_number 255 is out of range" once per released synth —
    // and on a device stderr is a blocking UART line in the audio path.
    if (AMY_IS_UNSET(e->synth)) return 0;
    // We have an instrument specified - decide which of its voices are actually to be used.
        if (AMY_IS_SET(e->pedal)) {
        // Pedal events are a special case
        bool sustain = (e->pedal != 0);
        synth_flags = instrument_get_flags(e->synth);
        if (synth_flags & SYNTH_FLAGS_NEGATE_PEDAL) {
            sustain = !sustain;  // Some MIDI pedals report backwards.
        }
        // A sustain release can result in note-off events for multiple voices.
        num_voices = instrument_sustain(e->synth, sustain, voices);
        if (num_voices) {
            e->velocity = 0;
        }
        //fprintf(stderr, "synth %d pedal %d num_voices %d\n", e->synth, e->pedal, num_voices);
    } else if (AMY_IS_SET(e->velocity)) {
        bool stolen = false;
        synth_flags = instrument_get_flags(e->synth);
        num_voices = patches_voices_for_note_onoff_event(e, voices, synth_flags, &stolen);
        if (stolen) {
            // Here, we issue a quick note-off for the stolen note, to support short decay.  Kind of an abstraction violation.
            struct delta d = {
                .time = e->time,
                .osc = voice_to_base_osc[voices[0]],
                .param = VELOCITY,
                .data.i = 0,
                .next = NULL,
            };
            add_delta_to_queue(&d, &amy_global.delta_queue);
            //fprintf(stderr, "synth %d note %d: voice %d stolen, osc %d time %d added note-off\n", e->synth, (int)roundf(e->midi_note), voices[0], d.osc, d.time);
        }
        // Apply noteon_delay_ms to note-on events.
        if (instrument_noteon_delay_ms(e->synth)) {
            uint32_t playback_time = amy_sysclock();
            if(AMY_IS_SET(e->time)) playback_time = e->time;
            playback_time += instrument_noteon_delay_ms(e->synth);
            // See amy_process_event(): dodge the u32 "unset" sentinel.
            if(AMY_IS_UNSET(playback_time)) playback_time++;
            e->time = playback_time;
            //fprintf(stderr, "synth %d note %d delay %d time %d\n", e->synth, (int)roundf(e->midi_note), instrument_noteon_delay_ms(e->synth), e->time);
        }
    } else {
        // Not note on/off, treat the synth as a shorthand for *all* the voices.
        num_voices = instrument_get_num_voices(e->synth, voices);
    }
    if (AMY_IS_SET(e->velocity) && e->velocity == 0 && (synth_flags & SYNTH_FLAGS_IGNORE_NOTE_OFFS)) {
        return 0;  // Ignore the note off, as requested.
    }
    return num_voices;
}

// This is called when i get an event with synth set.
// If the event also has synth and patch_number specified (a "load patch"), those are handled before this is called.
// So i know that the patch / voice alloc already exists and the patch has already been set!
void patches_event_has_voices(amy_event *e, struct delta **queue) {
    peek_stack("has_voices");
    //fprintf(stderr, "patches_event_has_voices:\n");
    //fprintf_event_stderr(e);

    patches_grab_synth_tier(e);
    // If this event is only changing bus parameters (FX), those parts have already been handled before we see it.
    // A pure pedal event doesn't strictly address oscs, but it can result in note-off events.
    if (!event_addresses_oscs(e) && AMY_IS_UNSET(e->pedal))
        return;  // Early exit.

    uint8_t synth = e->synth;
    int synth_flags = 0;
    if (AMY_IS_SET(e->synth)) {
        synth_flags = instrument_get_flags(e->synth);
    }
    // Should we invoke MIDI note-on cmd rules?
    if (synth_flags & SYNTH_FLAGS_NOTES_VIA_MIDI
        && AMY_IS_SET(e->midi_note)
        && (e->note_source_channel != e->synth)) {
        // Route note-on event via MIDI to invoke midi_note_cmds.  We pass the synth
        // number straight through as the channel rather than packing it into a MIDI
        // status byte, so synths above 16 reach their own mappings.
        uint8_t data[2];
        data[0] = 0x7F & (uint8_t)(e->midi_note);
        uint8_t velocity = 255;   // fake-note-on magic value.
        if (AMY_IS_SET(e->velocity)) velocity = (uint8_t) MIN(127, 127.1f * e->velocity);
        data[1] = velocity;
        //fprintf(stderr, "time %.3f synth %d flags %d note %.1f vel %.3f: MIDI cmd 0x90 chan %d 0x%02x 0x%02x\n", amy_global.time, synth, synth_flags, e->midi_note, e->velocity, e->synth, data[0], data[1]);
        // Remove the note and vel that we've put in the MIDI event, but keep any other event flags.
        AMY_UNSET(e->midi_note);
        AMY_UNSET(e->velocity);
        // Pass the target queue through: if this event is being stored (e.g. into
        // a patch), its deltas must land in that queue, not play now.
        // Note: nonzero synth_flags means e->synth was set.
        midi_message_handler_to_queue(0x90, e->synth, data, 2, e->time, e, queue);
    } else {
        uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
        uint8_t num_voices = patches_voices_for_event(e, voices);
        if (num_voices == 0) {
            // No voices to process, somehow event is to be ignored.
            return;
        }
        // Clear out the instrument, voices, patch from the event. If we didn't, we'd keep calling this over and over
        AMY_UNSET(e->patch_number);
        AMY_UNSET(e->synth);
        // for each voice, send the event to the base osc (+ e->osc if given)
        for(uint8_t i = 0; i < num_voices; i++) {
            if(AMY_IS_SET(voice_to_base_osc[voices[i]])) {
                uint16_t target_osc = voice_to_base_osc[voices[i]];
                uint16_t voice_oscs = (uint16_t)num_oscs_for_voice(voices[i]);
                if (AMY_IS_SET(e->osc) || !event_addresses_oscs(e)) {
                    // Osc is specified, or not osc-relevant so only needs writing once.
                    amy_event_to_deltas_queue(e, target_osc, voice_oscs, queue);
                } else {
                    // Osc-related but osc is not specified - send to all oscs in voice.
                    int num_oscs = num_oscs_for_voice(voices[i]);
                    for (int osc = 0; osc < num_oscs; ++osc) {
                        e->osc = osc;
                        //if (osc != 1)
                        amy_event_to_deltas_queue(e, target_osc, voice_oscs, queue);
                    }
                    AMY_UNSET(e->osc);
                }
                //fprintf(stderr, "patches: synth %d voice %d osc %d wav %d note %d vel %d\n", synth, voices[i], target_osc, e->wave, (int)e->midi_note, (int)(127.f * e->velocity));
            }
        }
    }
    // Restore the instrument in case this event is re-used.
    e->synth = synth;
}

void schedule_osc_reset(uint32_t time, uint16_t osc, struct delta **queue) {
    if (queue == NULL)  queue = &amy_global.delta_queue;
    // Use event mechanism to post osc resets, to ensure they are done in sequence.
    // This was important because for testing the patch reassignment, we were running the default
    // osc setup, then changing the patch, which reset the oscs here, but then the osc config
    // from the default setup was applied *after* the reset, so the osc state was not reset.
    //amy_event reset_event = amy_default_event();
    //reset_event.reset_osc = osc + j;
    //amy_event_to_deltas_queue(&reset_event, 0, &amy_global.delta_queue);
    // That's a lot of stack usage to add a single delta.  Let's cut to the chase.
    struct delta d = {
        .time = time,
        .osc = 0,
        .param = RESET_OSC,
        .data.i = osc,
        .next = NULL,
    };
    add_delta_to_queue(&d, queue);
}

// Like schedule_osc_reset, but the executed delta also returns the osc's
// storage to the heap. Only for oscs whose *ownership* is ending: release
// already discards the state (the reset wiped it in place), so freeing is
// behaviorally identical -- an unallocated osc reads as defaults, and the
// next touch re-allocates lazily -- it just gives the memory back.
void schedule_osc_free(uint32_t time, uint16_t osc, struct delta **queue) {
    if (queue == NULL)  queue = &amy_global.delta_queue;
    struct delta d = {
        .time = time,
        .osc = 0,
        .param = FREE_OSC,
        .data.i = osc,
        .next = NULL,
    };
    add_delta_to_queue(&d, queue);
}

void release_voice_oscs(int32_t voice, uint32_t time) {
    if(AMY_IS_SET(voice_to_base_osc[voice])) {
        //fprintf(stderr, "Already set voice %d, removing it\n", voice);
        // Remove the oscs for this old voice
        for(uint16_t i=0;i<AMY_OSCS;i++) {
            if(osc_to_voice[i]==voice) {
                //fprintf(stderr, "Already set voice %d osc %d, removing it\n", voices[v], i);
                AMY_UNSET(osc_to_voice[i]);
                // Ownership ends here: clear the osc and return its storage.
                schedule_osc_free(time, i, NULL);
            }
        }
        AMY_UNSET(voice_to_base_osc[voice]);
    }
}

uint16_t snapshot_voice_config(uint8_t instr_num, struct delta **queue, uint32_t time) {
    // Capture the CURRENT per-osc config of an instrument's first voice into a
    // base-osc-relative delta list, in the same form as a stored patch, so it
    // can be replayed into any voice with add_deltas_to_queue_with_baseosc().
    // Returns the number of oscs captured (0 if there was nothing to capture);
    // the caller owns the list and must delta_release_list() it.
    // Note and velocity are deliberately not part of an osc's config (see
    // set_event_for_osc), so snapshotting a voice that is mid-note captures the
    // sound, not the note being played.
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    int num_voices = instrument_get_num_voices(instr_num, voices);
    if (num_voices < 1)  return 0;
    uint16_t voice = voices[0];
    if (AMY_IS_UNSET(voice_to_base_osc[voice]))  return 0;
    uint16_t base_osc = voice_to_base_osc[voice];
    int num_oscs = num_oscs_for_voice(voice);
    for (int rel_osc = 0; rel_osc < num_oscs; ++rel_osc) {
        amy_event e;
        amy_clear_event(&e);
        e.time = time;
        e.osc = rel_osc;
        set_event_for_osc(base_osc, rel_osc, &e);
        amy_event_to_deltas_queue(&e, 0, /* oscs_per_voice= */ 0, queue);
    }
    return (uint16_t)num_oscs;
}

uint8_t patches_voices_for_load_synth(amy_event *e, uint16_t voices[]) {
    // When load_patch specifies a synth, convert that into voices.
    // e->synth is assumed to be set.
    uint16_t requested_voices = e->num_voices;
    // The voices[] array (and instrument_init, which aborts) can only take
    // MAX_VOICES_PER_INSTRUMENT; the wire can ask for anything.
    if (AMY_IS_SET(requested_voices) && requested_voices > MAX_VOICES_PER_INSTRUMENT) {
        fprintf(stderr, "synth %" PRId32 ": num_voices %" PRIu16 " clamped to %d\n",
                (int32_t)e->synth, requested_voices, MAX_VOICES_PER_INSTRUMENT);
        requested_voices = MAX_VOICES_PER_INSTRUMENT;
    }
    // If the instrument is alread initialized, copy the voice numbers.
    int num_voices = instrument_get_num_voices(e->synth, voices);
    //fprintf(stderr, "patches_voices_for_load: e->num_voices %d num_voices %d\n", e->num_voices, num_voices);
    if (num_voices == 0 && AMY_IS_UNSET(requested_voices)) {
        // New alloc without specifying num_voices - default to 1
        requested_voices = 1;
    }
    if (AMY_IS_SET(requested_voices) && requested_voices != num_voices) {
        // If we did already have voice oscs, release them.
        for (int32_t i = 0; i < num_voices; ++i) {
            release_voice_oscs(voices[i], e->time);
        }
        num_voices = 0;
        // Find avaliable voices with a single pass through voice_to_base_osc.
        uint32_t v = 0;
        for (int32_t i = 0; i < requested_voices; ++i) {
            while (v < amy_global.config.max_voices) {
                if (AMY_IS_UNSET(voice_to_base_osc[v])) break;
                ++v;
            }
            if (v == amy_global.config.max_voices)  {
                fprintf(stderr, "ran out of voices allocating %" PRId32 " voices to synth %" PRId32 ", ignoring.",
                        (int32_t)requested_voices, (int32_t)e->synth);
                patches_debug();
                return 0;
            }
            voices[i] = v;
            ++v;
            ++num_voices;
        }
        // Was this deleting the instrument?  i.e. was e->num_voices set but setting the num_voices to zero?
        if (num_voices == 0) {
            // Clear all the midi mappings.
            int type = MIDI_MAP_TYPE_ANY;
            midi_clear_channel_mappings(e->synth, type);
            // No memory patch to free here: an auto-assigned slot is released
            // by patches_load_patch as soon as it has been loaded, so an
            // instrument never holds one, and a patch the caller numbered
            // explicitly is never freed behind their back.
            // Delete the instrument
            instrument_release(e->synth);
            // Delete the instrument number so we don't forward the 'rest' of the event to it.
            AMY_UNSET(e->synth);
            return 0;
        }
        //fprintf(stderr, "Allocated %d voices to instrument %d\n", num_voices, e->synth);
    }
    //for (int i = 0; i < num_voices; ++i) {
    //    fprintf(stderr, "%d; ", voices[i]);
    //}
    //fprintf(stderr, "\n");
    return num_voices;
}

void patches_load_patch(amy_event *e) {
    // Given an event with a synth (instrument) set up a synth.
    // Common case is to call with a patch to load, but can also have just
    // a oscs_per_voice value.
    // (also called if instrument & num_voices even if no patch specified, to change #voices).
    // This means to set/reset the voices and load the messages (from ROM or memory) and set them.
    peek_stack("load_patch");
    // A RE-PATCH KEEPS THE INSTRUMENT'S BUS. Loading a patch rebuilds the
    // voices' oscs (reset to bus 0) and re-registers the instrument, so a
    // patch change that didn't re-state the bus dropped the synth out of
    // its own mix and onto the default bus, into everybody else's FX.
    // Inheriting the standing bus here makes the rest of the pipeline
    // (instrument_add_new below, the per-osc BUS deltas fanned out by
    // patches_event_has_voices) re-route exactly as an explicit bus would.
    if (AMY_IS_UNSET(e->bus) && instrument_number_exists(e->synth, NULL)) {
        int prev_bus = instrument_get_bus(e->synth);
        if (prev_bus > 0) e->bus = (uint16_t)prev_bus;
    }
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    uint8_t num_voices = 0;
    uint16_t oscs_per_voice = 0;
    uint16_t patch_number = e->patch_number;   // Need to match type of e->patch_number so AMY_IS_UNSET(patch_number) will work.
    //fprintf(stderr, "load_patch synth %d patch_number %d num_voices %d oscs_per_voice %d\n", e->synth, e->patch_number, e->num_voices, e->oscs_per_voice);
    // CLONE ON GROW. Changing num_voices on a synth that already has voices
    // rebuilds every voice, and the only config to rebuild them from used to be
    // the synth's stored patch -- so growing a synth silently threw away every
    // edit made since the patch was loaded, and reverted voice 0 to the pristine
    // patch. Snapshot the live voice instead, BEFORE the reallocation below
    // resets its oscs, and use that as the source for all the new voices. A
    // synth that was configured osc-by-osc (or with oscs_per_voice= and no patch
    // at all) can now grow too; before, it had no usable patch to reload and the
    // whole event was dropped.
    struct delta *clone_deltas = NULL;
    uint16_t clone_oscs_per_voice = 0;
    // num_voices=0 is a deletion: nothing will ever replay the snapshot, so
    // don't spend delta-pool churn capturing one.
    if (AMY_IS_UNSET(patch_number) && AMY_IS_UNSET(e->oscs_per_voice)
        && (AMY_IS_UNSET(e->num_voices) || e->num_voices > 0)
        && AMY_IS_SET(e->synth) && instrument_number_exists(e->synth, NULL))
        clone_oscs_per_voice = snapshot_voice_config(e->synth, &clone_deltas, e->time);
    // An AUTO-ASSIGNED memory patch is a courier, not a patch. Its number was
    // invented by patches_store_patch so a patch_string could be handed to this
    // load; the caller never saw it and can never ask for it again. So it is
    // consumed here: the number is not stored on the instrument (the voices
    // themselves are the synth's config now that growth clones them), and the
    // slot goes back to the pool on the way out, whether this load worked or
    // not.
    bool auto_patch = false;
    if (AMY_IS_SET(patch_number)) {
        int patch_index = (int)patch_number - _PATCHES_FIRST_USER_PATCH;
        auto_patch = (patch_index >= 0 && patch_index < (int)max_num_memory_patches
                      && memory_patch_auto[patch_index]);
    }
    num_voices = patches_voices_for_load_synth(e, voices);
    if (num_voices == 0) {
        if (AMY_IS_UNSET(e->num_voices)) {
            // Print a warning unless we deliberately set the voices to zero to release the synth.
            fprintf(stderr, "synth %" PRId32 ": no voices selected, ignored (e->num_voices %" PRId32 "...)\n",
                    (int32_t)e->synth, (int32_t)e->num_voices);
        }
        goto done;
    } else {
        // Reset every osc belonging to each voice so stale state doesn't persist
        // before we reallocate and reinitialize them below.
        for (uint8_t v = 0; v < num_voices; v++) {
            if (AMY_IS_SET(voice_to_base_osc[voices[v]])) {
                uint16_t base = voice_to_base_osc[voices[v]];
                for (uint16_t j = 0; j < AMY_OSCS - base; j++) {
                    if (osc_to_voice[base + j] != voices[v]) break;
                    schedule_osc_reset(e->time, base + j, NULL);
                }
            }
        }
    }

    // At this point, we have the voices[] array and num_voices set up to be initialized.
    char *message = NULL;
    struct delta *deltas = NULL;
    // Figure out the #oscs per voice, setup message or deltas if available.
    if (AMY_IS_SET(e->oscs_per_voice)) {
        oscs_per_voice = e->oscs_per_voice;
        if (AMY_IS_SET(patch_number)) {
            fprintf(stderr, "WARN: synth %" PRId32 ": oscs_per_voice %" PRIu16 " made me ignore patch number %" PRIu16 "\n",
                    (int32_t)e->synth, e->oscs_per_voice, patch_number);
        }
    } else if (clone_oscs_per_voice > 0) {
        // Reshaping a live synth: every voice comes from the snapshot of the
        // existing voice taken above, not from the stored patch. The synth keeps
        // whatever patch number it had (nothing here changes it) -- it is no
        // longer what defines the sound.
        oscs_per_voice = clone_oscs_per_voice;
        deltas = clone_deltas;
        patch_number = instrument_get_patch_number(e->synth);
    } else {
        if (AMY_IS_UNSET(patch_number))
            patch_number = instrument_get_patch_number(e->synth);
        if(patch_number < _PATCHES_FIRST_USER_PATCH) {
            // Built-in patch. The table has reserved (NULL) gaps -- e.g. 259-383
            // before the drum kit bank at 384 -- and ends well before the user
            // range, so check both before dereferencing.
            if (patch_number >= _PATCHES_NUM_BUILTIN || patch_commands[patch_number] == NULL) {
                fprintf(stderr, "patch_number %" PRIu16 " is not a defined built-in patch (synth %" PRId32 "), ignored\n",
                        patch_number, (int32_t)e->synth);
                goto done;
            }
            message = (char*)patch_commands[patch_number];
            oscs_per_voice = patch_oscs[patch_number];
        } else {
            // User-defined patch.  patch_number can be out of range here (an
            // unset instrument patch reads back as UNSET/0xffff, and a wire
            // message can name any number), so bounds-check before indexing.
            int32_t patch_index = patch_number - _PATCHES_FIRST_USER_PATCH;
            // Range-check before indexing. patches_store_patch refuses a
            // number past the pool, but the event still carries it and
            // arrives here — and indexing the tables with it read past
            // their ends and walked a garbage delta pointer (a segfault
            // on the desktop, a Guru Meditation on the device).
            if (patch_index >= (int32_t)max_num_memory_patches) {
                fprintf(stderr, "load patch_number %" PRIu16 " is out of range (%" PRId32 " .. %" PRId32 ") (synth %" PRId32 "), ignored\n",
                        patch_number, (int32_t)_PATCHES_FIRST_USER_PATCH,
                        (int32_t)(_PATCHES_FIRST_USER_PATCH + (int32_t)max_num_memory_patches - 1),
                        (int32_t)e->synth);
                goto done;
            }
            oscs_per_voice = memory_patch_oscs[patch_index];
            if(oscs_per_voice > 0){
                deltas = memory_patch_deltas[patch_index];
            } else {
                fprintf(stderr, "patch_number %" PRIu16 " has %" PRIu16 " num_deltas %" PRIi32 " (synth %" PRId32 " num_voices %" PRId32 "), ignored\n",
                        patch_number, oscs_per_voice, delta_list_len(memory_patch_deltas[patch_index]),
                        (int32_t)e->synth, (int32_t)e->num_voices);
                goto done;
            }
        }
    }

    for(uint8_t v=0;v<num_voices;v++)  {
        // Release all the oscs of any voices we're re-using before we start re-allocating oscs.
        release_voice_oscs(voices[v], e->time);
    }

    for(uint8_t v=0;v<num_voices;v++)  {
        // Find the first osc with oscs_per_voice free oscs.
        bool found = false;
        uint16_t osc_start = (AMY_OSCS/2);

        #ifdef TULIP
        // On tulip. core 0 (oscs0-60) is shared with the display, who does GDMA a lot. so we favor core1 (oscs60-120)
        if(voices[v]%3==2) osc_start = 0;
        #else
        if(voices[v]%2==1) osc_start = 0;
        #endif

        for(uint16_t i = 0; !found && i < AMY_OSCS; i++) {
            uint16_t base_osc = (osc_start + i) % AMY_OSCS;
            // Don't scan if the block would wrap AMY_OSCS.
            if (base_osc > AMY_OSCS - oscs_per_voice)  continue;
            int available_oscs = 0;
            // Are there num_voices x oscs_per_voice free oscs after this one?
            for(uint16_t osc = base_osc; osc < base_osc + oscs_per_voice; ++osc) {
                if (AMY_IS_SET(osc_to_voice[osc])) {
                    break;
                }
                ++available_oscs;
            }
            if(available_oscs == oscs_per_voice) {
                //fprintf(stderr, "found %d consecutive oscs starting at %d for voice %d\n", oscs_per_voice, base_osc, voices[v]);
                voice_to_base_osc[voices[v]] = base_osc;
                for(uint16_t osc = base_osc; osc < base_osc + oscs_per_voice; ++osc) {
                    //fprintf(stderr, "setting osc %d for voice %d to amy osc %d\n", osc - base_osc, voices[v], osc);
                    osc_to_voice[osc] = voices[v];
                    schedule_osc_reset(e->time, osc, NULL);
                }
                // exit the loop
                found = true;
            }
        }
        if(!found) {
            fprintf(stderr, "cannot find %" PRIu16 " oscs for patch %" PRIu16 " for voice %" PRIu16 ". not setting this voice\n",
                    oscs_per_voice, patch_number, voices[v]);
        }
    }  // end of loop setting up voice_to_base_osc for all voices[v]

    // Finally, store as an instrument if instrument number is specified.
    if (AMY_IS_SET(e->synth)) {
        uint32_t flags = 0;
        if (AMY_IS_SET(e->synth_flags)) flags = e->synth_flags;
        uint16_t bus = 0;
        if (AMY_IS_SET(e->bus)) bus = e->bus;
        // An auto-assigned number is an allocator artifact, so the instrument
        // records no patch at all rather than a number nobody can use.
        uint16_t stored_patch_number = patch_number;
        if (auto_patch)  AMY_UNSET(stored_patch_number);
        instrument_add_new(e->synth, num_voices, voices, stored_patch_number, oscs_per_voice, bus, flags);
    }
    // Now actually initialize the newly-allocated osc blocks with the patch
    bool is_first_voice = true;  // flag for once-only messages.
    for(uint8_t v = 0; v < num_voices; v++) {
        if(AMY_IS_SET(voice_to_base_osc[voices[v]])) {
            if (deltas) {
                add_deltas_to_queue_with_baseosc(deltas, voice_to_base_osc[voices[v]], oscs_per_voice, &amy_global.delta_queue, e->time);
            } else if (message) {
                parse_patch_string_to_queue(message, voice_to_base_osc[voices[v]], oscs_per_voice, &amy_global.delta_queue, e->synth, e->time, is_first_voice);
            }
            // Or maybe there's no deltas and no message, in which case we just set oscs_per_voice, waiting for config.
            is_first_voice = false;
        }
    }
done:
    // add_deltas_to_queue_with_baseosc copies, so the snapshot is ours to free.
    if (clone_deltas)  delta_release_list(clone_deltas);
    // Likewise the courier slot: its deltas have been copied into the queue (or
    // this load failed), and nothing can ask for it by number, so hand it back.
    if (auto_patch)  patches_reset_patch(e->patch_number);
}
