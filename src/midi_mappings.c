// midi_mappings.c
// example midi mappings for controllers

#include "amy.h"

#include <assert.h>   // for buffer overruns in midi_fetch_control_code_command.

void juno_filter_midi_handler(uint8_t * bytes, uint16_t len, uint8_t is_sysex_unused) {
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

struct midi_mapping {
    struct midi_mapping *next;
    int channel;
    int type;        //
    int code;        // For note-ons, note number for note-specific events; MIDI_MAP_CODE_ANY for non-note-specific events.
    // Transform of value field
    int is_log;
    float min_val;
    float max_val;
    float offset_val;
    // What we actually do: either a wire command template, or (when the
    // payload after O began with a digit) a list of AMY parameters to set
    // directly -- see midi_parse_param_targets().  message_template keeps
    // the payload text in both cases, which is what the state dump emits.
    char *message_template;
    uint8_t num_targets;
    uint8_t last_sent;  // midi_cc_output: last CC value sent, 0xFF = none yet
    struct midi_param_target targets[MIDI_MAP_MAX_TARGETS];
};

// ---------------------------------------------------------------------------
// AMY parameters by id (enum params), for mappings that name a parameter
// directly instead of carrying a wire command (issue #1175).
//
// A direct mapping does not build deltas itself: it sets the matching
// amy_event field and sends the event down the ordinary path, so the value
// is in the same units as the corresponding amy.send() keyword (Hz for
// freq and filter_freq, linear drive for dist_drive, ...), the synth's
// voices and bus are resolved exactly as for any other synth event, and
// every conversion (logfreq, log2 ratio, logdrive) happens in one place.
//
// Only params with a plain one-value event field are here.  Note-shaped
// params (MIDI_NOTE, VELOCITY), osc references (CHAINED_OSC, MOD_SOURCE_*,
// ALGO_SOURCE_*), breakpoints, resets and global config (LATENCY, BUS) are
// deliberately absent: a CC sweeping any of those is a bug, not a patch.

enum { PF_F, PF_U8, PF_U16, PF_I16 };
#define PF_OSC 0     // an osc-scope field: the event names the target osc
#define PF_BUS 1     // bus-scope or global (tempo, pitch bend): no osc named
// The distortion fields serve both scopes: naming an osc is what makes them
// osc-scope.  midi_cc_output needs to know, to tell PARAM_DIST_MIX on an osc
// from PARAM_BUS_DIST_MIX in the same event field.
#define PF_SHARED 2

typedef struct {
    uint16_t param;     // first param id
    uint8_t count;      // 1, or NUM_COMBO_COEFS for a coef vector
    uint8_t type;       // PF_*
    uint8_t scope;      // PF_OSC / PF_BUS, | PF_SHARED
    uint16_t offset;    // offsetof the (first) field in amy_event
} param_field_t;

#define PFIELD(P, N, T, S, FIELD) { P, N, T, S, (uint16_t)offsetof(amy_event, FIELD) }
static const param_field_t param_fields[] = {
    PFIELD(WAVE,             1,               PF_U16, PF_OSC, wave),
    PFIELD(PRESET,           1,               PF_I16, PF_OSC, preset),
    PFIELD(AMP,              NUM_COMBO_COEFS, PF_F,   PF_OSC, amp_coefs),
    PFIELD(DUTY,             NUM_COMBO_COEFS, PF_F,   PF_OSC, duty_coefs),
    PFIELD(FEEDBACK,         1,               PF_F,   PF_OSC, feedback),
    PFIELD(FREQ,             NUM_COMBO_COEFS, PF_F,   PF_OSC, freq_coefs),
    PFIELD(PHASE,            1,               PF_F,   PF_OSC, trigger_phase),
    PFIELD(PITCH_BEND,       1,               PF_F,   PF_BUS, pitch_bend),
    PFIELD(PAN,              NUM_COMBO_COEFS, PF_F,   PF_OSC, pan_coefs),
    PFIELD(FILTER_FREQ,      NUM_COMBO_COEFS, PF_F,   PF_OSC, filter_freq_coefs),
    PFIELD(RATIO,            1,               PF_F,   PF_OSC, ratio),
    PFIELD(RESONANCE,        1,               PF_F,   PF_OSC, resonance),
    PFIELD(PORTAMENTO,       1,               PF_U16, PF_OSC, portamento_ms),
    PFIELD(FILTER_TYPE,      1,               PF_U8,  PF_OSC, filter_type),
    PFIELD(ALGORITHM,        1,               PF_U8,  PF_OSC, algorithm),
    PFIELD(DIST_CLIP_EN,     1,               PF_U8,  PF_OSC|PF_SHARED, dist_clip),
    PFIELD(DIST_FOLD_EN,     1,               PF_U8,  PF_OSC|PF_SHARED, dist_fold),
    PFIELD(DIST_CRUSH_EN,    1,               PF_U8,  PF_OSC|PF_SHARED, dist_crush),
    PFIELD(DIST_BITS,        1,               PF_U8,  PF_OSC|PF_SHARED, dist_bits),
    PFIELD(DIST_RATE,        1,               PF_U16, PF_OSC|PF_SHARED, dist_rate),
    PFIELD(DIST_LOGDRIVE,    NUM_COMBO_COEFS, PF_F,   PF_OSC|PF_SHARED, dist_drive_coefs),
    PFIELD(DIST_MIX,         NUM_COMBO_COEFS, PF_F,   PF_OSC|PF_SHARED, dist_mix_coefs),
    PFIELD(EG0_TYPE,         1,               PF_U8,  PF_OSC, eg_type[0]),
    PFIELD(EG1_TYPE,         1,               PF_U8,  PF_OSC, eg_type[1]),
    PFIELD(TEMPO,            1,               PF_F,   PF_BUS, tempo),
    PFIELD(VOLUME,           1,               PF_F,   PF_BUS, volume),
    PFIELD(EQ_L,             1,               PF_F,   PF_BUS, eq_l),
    PFIELD(EQ_M,             1,               PF_F,   PF_BUS, eq_m),
    PFIELD(EQ_H,             1,               PF_F,   PF_BUS, eq_h),
    PFIELD(ECHO_LEVEL,       1,               PF_F,   PF_BUS, echo_level),
    PFIELD(ECHO_DELAY_MS,    1,               PF_F,   PF_BUS, echo_delay_ms),
    PFIELD(ECHO_MAX_DELAY_MS,1,               PF_F,   PF_BUS, echo_max_delay_ms),
    PFIELD(ECHO_FEEDBACK,    1,               PF_F,   PF_BUS, echo_feedback),
    PFIELD(ECHO_FILTER_COEF, 1,               PF_F,   PF_BUS, echo_filter_coef),
    PFIELD(CHORUS_LEVEL,     1,               PF_F,   PF_BUS, chorus_level),
    PFIELD(CHORUS_MAX_DELAY, 1,               PF_F,   PF_BUS, chorus_max_delay),
    PFIELD(CHORUS_LFO_FREQ,  1,               PF_F,   PF_BUS, chorus_lfo_freq),
    PFIELD(CHORUS_DEPTH,     1,               PF_F,   PF_BUS, chorus_depth),
    PFIELD(REVERB_LEVEL,     1,               PF_F,   PF_BUS, reverb_level),
    PFIELD(REVERB_LIVENESS,  1,               PF_F,   PF_BUS, reverb_liveness),
    PFIELD(REVERB_DAMPING,   1,               PF_F,   PF_BUS, reverb_damping),
    PFIELD(REVERB_XOVER_HZ,  1,               PF_F,   PF_BUS, reverb_xover_hz),
    // The bus distortion stage shares its event fields with the osc stage;
    // which one an event reaches is decided by whether it names an osc.
    PFIELD(BUS_DIST_CLIP_EN, 1,               PF_U8,  PF_BUS|PF_SHARED, dist_clip),
    PFIELD(BUS_DIST_FOLD_EN, 1,               PF_U8,  PF_BUS|PF_SHARED, dist_fold),
    PFIELD(BUS_DIST_CRUSH_EN,1,               PF_U8,  PF_BUS|PF_SHARED, dist_crush),
    PFIELD(BUS_DIST_BITS,    1,               PF_U8,  PF_BUS|PF_SHARED, dist_bits),
    PFIELD(BUS_DIST_RATE,    1,               PF_U16, PF_BUS|PF_SHARED, dist_rate),
    PFIELD(BUS_DIST_DRIVE,   1,               PF_F,   PF_BUS|PF_SHARED, dist_drive_coefs[COEF_CONST]),
    PFIELD(BUS_DIST_MIX,     1,               PF_F,   PF_BUS|PF_SHARED, dist_mix_coefs[COEF_CONST]),
};
#undef PFIELD

static const param_field_t *param_field_for(int param, int *p_index) {
    for (size_t i = 0; i < sizeof(param_fields) / sizeof(param_fields[0]); ++i) {
        const param_field_t *f = &param_fields[i];
        if (param >= f->param && param < f->param + f->count) {
            if (p_index) *p_index = param - f->param;
            return f;
        }
    }
    return NULL;
}

bool amy_param_is_settable(int param) {
    return param_field_for(param, NULL) != NULL;
}

// Set the event field for param to value.  Integer fields are rounded, so a
// CC scaled onto 0..6 steps through filter types cleanly.  An osc-scope
// param also names the osc (voice-relative when the event names a synth);
// a bus-scope one leaves osc alone, since naming an osc would make the
// shared distortion fields osc-scope.
bool amy_event_set_param(amy_event *e, int param, uint16_t osc, float value) {
    int index = 0;
    const param_field_t *f = param_field_for(param, &index);
    if (f == NULL)  return false;
    char *base = (char *)e + f->offset;
    switch (f->type) {
        case PF_F:   ((float *)base)[index] = value; break;
        case PF_U8:  ((uint8_t *)base)[index] = (uint8_t)MAX(0, MIN(255, (int)lrintf(value))); break;
        case PF_U16: ((uint16_t *)base)[index] = (uint16_t)MAX(0, MIN(65535, (int)lrintf(value))); break;
        case PF_I16: ((int16_t *)base)[index] = (int16_t)MAX(-32768, MIN(32767, (int)lrintf(value))); break;
    }
    if ((f->scope & PF_BUS) == 0)  e->osc = osc;
    return true;
}

// The converse, for midi_cc_output: if this event changes `param` at the
// voice-relative osc `osc`, put the new value (in amy.send() units, exactly
// what amy_event_set_param would have written) in *value.
static bool amy_event_get_param(amy_event *e, int param, uint16_t osc, float *value) {
    int index = 0;
    const param_field_t *f = param_field_for(param, &index);
    if (f == NULL)  return false;
    if (f->scope & PF_SHARED) {
        // A shared field is osc-scope exactly when the event names an osc.
        if (AMY_IS_SET(e->osc) != ((f->scope & PF_BUS) == 0))  return false;
    }
    // An osc-scope change with no osc named goes to every osc of the voice
    // (patches_event_has_voices), so it reaches this target too.
    if ((f->scope & PF_BUS) == 0 && AMY_IS_SET(e->osc) && e->osc != osc)  return false;
    char *base = (char *)e + f->offset;
    switch (f->type) {
        case PF_F:   { float v = ((float *)base)[index]; if (AMY_IS_UNSET(v)) return false; *value = v; break; }
        case PF_U8:  { uint8_t v = ((uint8_t *)base)[index]; if (AMY_IS_UNSET(v)) return false; *value = v; break; }
        case PF_U16: { uint16_t v = ((uint16_t *)base)[index]; if (AMY_IS_UNSET(v)) return false; *value = v; break; }
        case PF_I16: { int16_t v = ((int16_t *)base)[index]; if (AMY_IS_UNSET(v)) return false; *value = v; break; }
    }
    return true;
}

// Parse a direct-parameter payload: P, or P,OSC, or P,OSC,P,OSC,...  The
// single-P form means osc 0 of each voice (the base osc).  Returns the
// number of targets, or 0 if the payload is malformed or names a param
// that can't be driven this way.
static int midi_parse_param_targets(const char *s, size_t len, struct midi_param_target *targets) {
    int vals[2 * MIDI_MAP_MAX_TARGETS];
    int n = 0;
    size_t i = 0;
    while (i < len) {
        if (n == 2 * MIDI_MAP_MAX_TARGETS) {
            fprintf(stderr, "midi mapping: at most %d P,OSC pairs\n", MIDI_MAP_MAX_TARGETS);
            return 0;
        }
        if (s[i] < '0' || s[i] > '9') goto bad;
        int v = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9')  v = 10 * v + (s[i++] - '0');
        vals[n++] = v;
        if (i < len) {
            if (s[i] != ',') goto bad;
            if (++i == len) goto bad;  // trailing comma
        }
    }
    if (n > 1 && (n & 1)) {
        fprintf(stderr, "midi mapping: with more than one param, each needs its osc (P,OSC,P,OSC...)\n");
        return 0;
    }
    int num_targets = (n + 1) / 2;
    for (int t = 0; t < num_targets; ++t) {
        int param = vals[2 * t];
        int osc = (n > 1) ? vals[2 * t + 1] : 0;
        if (!amy_param_is_settable(param)) {
            fprintf(stderr, "midi mapping: param %d can't be set from a MIDI mapping\n", param);
            return 0;
        }
        if (osc > 0xFFFF) goto bad;
        targets[t].param = (uint16_t)param;
        targets[t].osc = (uint16_t)osc;
    }
    return num_targets;
 bad:
    fprintf(stderr, "midi mapping: bad param list '%.*s'\n", (int)len, s);
    return 0;
}

bool mappings_inited = false;

// Mappings are indexed by channel, but "channel" here means a synth number as
// well as a MIDI channel -- patches.c routes synth note-ons through the mapping
// machinery, and synth numbers run up to config.max_synths, well past the 16
// channels a MIDI cable can carry.  So these are sized at init, not compiled in.
// Channel 0 is a valid synth (it just isn't reachable from a MIDI cable, whose
// channels are numbered from 1), so valid channels are 0..num_mapping_channels.
struct midi_mapping **midi_cc_mapping_root_by_chan = NULL;
struct midi_mapping **midi_note_mapping_root_by_chan = NULL;
// midi_cc_output mappings (iC): parameter changes on a synth echoed out as
// MIDI CCs.  A list of their own, so they never make a channel "active" for
// MIDI input and never answer a lookup for an incoming CC.
struct midi_mapping **midi_cc_out_mapping_root_by_chan = NULL;
int num_mapping_channels = 0;

static bool mapping_channel_ok(int channel) {
    return mappings_inited && channel >= 0 && channel <= num_mapping_channels;
}

static struct midi_mapping **mapping_root(int channel, int type) {
    if (type == MIDI_MAP_TYPE_CC)      return &midi_cc_mapping_root_by_chan[channel];
    if (type == MIDI_MAP_TYPE_CC_OUT)  return &midi_cc_out_mapping_root_by_chan[channel];
    return &midi_note_mapping_root_by_chan[channel];
}

// Built-in default for note commands
struct midi_mapping default_note_mapping = {
    .next = NULL,
    .channel = 0,
    .type = MIDI_MAP_TYPE_NOTE,
    .code = MIDI_MAP_CODE_ANY,
    .is_log = 0,
    .min_val = 0,
    .max_val = 1.0f,
    .offset_val = 0,
    //.message_template = "i%iiM1n%nl%v",
    .message_template = "i%in%nl%v",
};


void midi_mapping_print(struct midi_mapping *mapping) {
    fprintf(stderr, "mapping 0x%lx chan %d type %d code 0x%x log %d min %.1f max %.1f offs %.1f msg %s\n",
            (unsigned long)mapping, mapping->channel, mapping->type, mapping->code, mapping->is_log, mapping->min_val, mapping->max_val, mapping->offset_val, mapping->message_template);
}

struct midi_mapping *midi_mapping_init(int channel, int type, int code, int is_log, float min_val, float max_val, float offset_val, const char *message_template, int message_len) {
    if (!mapping_channel_ok(channel))  return NULL;
    struct midi_mapping **p_root = mapping_root(channel, type);
    struct midi_mapping *result = (struct midi_mapping *)malloc_caps(sizeof(struct midi_mapping) + message_len + 1, amy_global.config.ram_caps_synth);
    if (result == NULL) {
        amy_oom("midi_mapping_init: out of memory\n");
        return NULL;
    }
    result->message_template = ((char *)result) + sizeof(struct midi_mapping);
    result->channel = channel;
    result->type = type;
    result->code = code;
    result->is_log = is_log;
    result->min_val = min_val;
    result->max_val = max_val;
    result->offset_val = offset_val;
    result->num_targets = 0;
    result->last_sent = 0xFF;  // nothing sent yet
    strncpy(result->message_template, message_template, message_len);
    result->message_template[message_len] = '\0';
    // Insert into the linked list at the head.
    result->next = *p_root;
    *p_root = result;
    return result;
}

void midi_mapping_debug(void) {
    fprintf(stderr, "midi_mapping_debug:\n");
    for (int channel = 0; channel < num_mapping_channels + 1; ++channel) {
        struct midi_mapping **p_mapping = &midi_cc_mapping_root_by_chan[channel];
        while (*p_mapping != NULL) {
            midi_mapping_print(*p_mapping);
            p_mapping = &((*p_mapping)->next);
        }
        p_mapping = &midi_note_mapping_root_by_chan[channel];
        while (*p_mapping != NULL) {
            midi_mapping_print(*p_mapping);
            p_mapping = &((*p_mapping)->next);
        }
        p_mapping = &midi_cc_out_mapping_root_by_chan[channel];
        while (*p_mapping != NULL) {
            midi_mapping_print(*p_mapping);
            p_mapping = &((*p_mapping)->next);
        }
    }
}

void midi_mapping_free(struct midi_mapping **p_mapping) {
    // Close up the linked list.
    struct midi_mapping *doomed = *p_mapping;
    *p_mapping = doomed->next;
    // Return the memory
    free(doomed);
}

void midi_mappings_free(struct midi_mapping **p_mapping) {
    while (*p_mapping != NULL) {
        midi_mapping_free(p_mapping);
    }
}

void midi_mappings_init(void) {
    midi_mappings_deinit();  // Release any earlier allocation; init is called more than once.
    // A mapping channel is a synth number, so cover every synth, but never fewer
    // than the 16 channels that can arrive over a MIDI cable.
    num_mapping_channels = AMY_NUM_MIDI_CHANNELS;
    if ((int)amy_global.config.max_synths > num_mapping_channels)
        num_mapping_channels = (int)amy_global.config.max_synths;
    size_t num_bytes = sizeof(struct midi_mapping *) * (num_mapping_channels + 1);
    midi_cc_mapping_root_by_chan = (struct midi_mapping **)malloc_caps(num_bytes, amy_global.config.ram_caps_synth);
    midi_note_mapping_root_by_chan = (struct midi_mapping **)malloc_caps(num_bytes, amy_global.config.ram_caps_synth);
    midi_cc_out_mapping_root_by_chan = (struct midi_mapping **)malloc_caps(num_bytes, amy_global.config.ram_caps_synth);
    if (midi_cc_mapping_root_by_chan == NULL || midi_note_mapping_root_by_chan == NULL
        || midi_cc_out_mapping_root_by_chan == NULL) {
        fprintf(stderr, "unable to alloc midi mappings for %d channels\n", num_mapping_channels);
        free(midi_cc_mapping_root_by_chan);
        midi_cc_mapping_root_by_chan = NULL;
        free(midi_note_mapping_root_by_chan);
        midi_note_mapping_root_by_chan = NULL;
        free(midi_cc_out_mapping_root_by_chan);
        midi_cc_out_mapping_root_by_chan = NULL;
        num_mapping_channels = 0;
        return;
    }
    for (int channel = 0; channel < num_mapping_channels + 1; ++channel) {
        midi_cc_mapping_root_by_chan[channel] = NULL;
        midi_note_mapping_root_by_chan[channel] = NULL;
        midi_cc_out_mapping_root_by_chan[channel] = NULL;
    }
    mappings_inited = true;
}

void midi_mappings_deinit(void) {
    if (mappings_inited) {
        for (int channel = 0; channel < num_mapping_channels + 1; ++channel) {
            midi_mappings_free(&midi_cc_mapping_root_by_chan[channel]);
            midi_mappings_free(&midi_note_mapping_root_by_chan[channel]);
            midi_mappings_free(&midi_cc_out_mapping_root_by_chan[channel]);
        }
        mappings_inited = false;
    }
    free(midi_cc_mapping_root_by_chan);
    midi_cc_mapping_root_by_chan = NULL;
    free(midi_note_mapping_root_by_chan);
    midi_note_mapping_root_by_chan = NULL;
    free(midi_cc_out_mapping_root_by_chan);
    midi_cc_out_mapping_root_by_chan = NULL;
    num_mapping_channels = 0;
}

void midi_clear_channel_mappings(int channel, int type) {
    if (!mapping_channel_ok(channel))  return;
    // Each root list holds only mappings of its own type on this one channel, so
    // clearing a type is just emptying its list -- no filtering, no recursion.
    if (type == MIDI_MAP_TYPE_ANY || type == MIDI_MAP_TYPE_CC)
        midi_mappings_free(&midi_cc_mapping_root_by_chan[channel]);
    if (type == MIDI_MAP_TYPE_ANY || type == MIDI_MAP_TYPE_NOTE)
        midi_mappings_free(&midi_note_mapping_root_by_chan[channel]);
    // ANY is "everything this synth has", as when the synth is deleted.
    if (type == MIDI_MAP_TYPE_ANY || type == MIDI_MAP_TYPE_CC_OUT)
        midi_mappings_free(&midi_cc_out_mapping_root_by_chan[channel]);
    // Stop listening to this MIDI channel unless there's a synth on it.
    if (!instrument_number_exists(channel, NULL))
        midi_active_channel_set(channel, false);
}

struct midi_mapping **midi_mapping_find(int channel, int type, int code) {
    if (!mapping_channel_ok(channel))  return NULL;
    // Retrieve the mapping associated with a midi channel + code, if any.
    // ANY means any INPUT mapping (it decides whether a MIDI channel is
    // listened to), so it never finds a midi_cc_output.
    struct midi_mapping **result;
    if (type == MIDI_MAP_TYPE_ANY) {
        result = midi_mapping_find(channel, MIDI_MAP_TYPE_CC, code);
        if (result == NULL)
            result = midi_mapping_find(channel, MIDI_MAP_TYPE_NOTE, code);
        return result;
    }
    struct midi_mapping **p_mapping = mapping_root(channel, type);
    while (*p_mapping != NULL) {
        if ((*p_mapping)->channel == channel && ((type == MIDI_MAP_TYPE_ANY) || (*p_mapping)->type == type)) {
            if ((code == MIDI_MAP_CODE_ANY) || ((*p_mapping)->code == MIDI_MAP_CODE_ANY) || ((*p_mapping)->code == code))
                return p_mapping;
        }
        p_mapping = &((*p_mapping)->next);
    }
    return NULL;
}

int midi_clear_mapping(int channel, int type, int code) {
    if (!mapping_channel_ok(channel))  return 0;
    // Backwards compatibility
    if (code == 255) code = MIDI_MAP_CODE_ANY;
    if (code == MIDI_MAP_CODE_ANY) {
        // Magic value means clear all MIDI CCs for this channel
        midi_clear_channel_mappings(channel, type);
        return 1;
    }
    struct midi_mapping **p_mapping = midi_mapping_find(channel, type, code);
    if (p_mapping) {
        midi_mapping_free(p_mapping);
        // We just deleted a mapping on this channel, was it the last one?
        midi_active_channel_set(channel, midi_mappings_exist_for_channel(channel) || instrument_number_exists(channel, NULL));
        return 1;
    }
    return 0;  // nothing found.
}

int midi_store_mapping(int channel, int type, int code, int is_log, float min_val, float max_val, float offset_val, const char *message, size_t message_len) {
    if (!mapping_channel_ok(channel))  return 0;
    // Register a MIDI mapping and a wire code template.
    //char tmp[256];
    //strncpy(tmp, message, message_len);
    //tmp[message_len] = '\0';
    //fprintf(stderr, "midi_store_mapping: ch %d type %d code %d L %d N %.3f X %.3f O %.3f CMD (%d) '%s'\n",
    //        channel, type, code, is_log, min_val, max_val, offset_val, message_len, tmp);
    // Strip trailing wire protocol terminator(s) so they don't accumulate on round-trips.
    while (message_len > 0 && message[message_len - 1] == 'Z') {
        --message_len;
    }
    // A payload that starts with a digit is a direct parameter list
    // (P[,OSC][,P,OSC...]), not a wire command -- no wire command can start
    // with a digit, so the two forms never collide.  Parse it BEFORE the old
    // mapping is dropped, so a typo doesn't silently delete a working CC.
    struct midi_param_target targets[MIDI_MAP_MAX_TARGETS];
    int num_targets = 0;
    if (message_len && message[0] >= '0' && message[0] <= '9') {
        num_targets = midi_parse_param_targets(message, message_len, targets);
        if (num_targets <= 0)  return 0;
    }
    // An output has no wire command to run: it only watches parameters.
    if (type == MIDI_MAP_TYPE_CC_OUT && message_len && num_targets == 0) {
        fprintf(stderr, "midi_cc_output: needs C,L,N,X,O,P[,OSC]..., not a wire command\n");
        return 0;
    }
    if (type == MIDI_MAP_TYPE_CC_OUT && message_len && (channel < 1 || channel > 16)) {
        uint8_t ch;
        bool fwd;
        if (!note_output_midi_channel((uint8_t)channel, &ch, &fwd))
            fprintf(stderr, "midi_cc_output: synth %d is not a MIDI channel (1..16), so its CCs "
                    "won't be sent until it has a MIDI note_output to name one\n", channel);
    }
    struct midi_mapping **p_mapping = midi_mapping_find(channel, type, code);
    if (p_mapping) midi_mapping_free(p_mapping);
    // store with an empty string removes mapping
    if (message_len) {
        struct midi_mapping *mapping = midi_mapping_init(channel, type, code, is_log, min_val, max_val, offset_val, message, message_len);
        if (mapping != NULL && num_targets > 0) {
            memcpy(mapping->targets, targets, num_targets * sizeof(targets[0]));
            mapping->num_targets = (uint8_t)num_targets;
        }
        //midi_mapping_debug();
    }
    // We just deleted a mapping on this channel, was it the last one?
    midi_active_channel_set(channel, midi_mappings_exist_for_channel(channel) || instrument_number_exists(channel, NULL));
    return 1;
}

#define SNPRINT3DPCOMMA(val) \
    snprintfloat3dp(s, len, val); \
    len -= strlen(s); \
    s += strlen(s); \
    if (len) { \
        s[0] = ','; \
        ++s; \
        --len; \
    }

bool midi_fetch_mapping_command(int channel, int type, int code, char *s, size_t len) {
    struct midi_mapping **p_mapping = midi_mapping_find(channel, type, code);
    //fprintf(stderr, "midi_fetch_mapping_command chan %d type %d code %d mapping 0x%llx\n", channel, type, code, (uint64_t)p_mapping);
    if (p_mapping == NULL)
        return false;
    // Format the control code - ic<C>,<L>,<N>,<X>,<O>,<CODE>
    //sprintf(s, "i%c%d,%d,%.3f,%.3f,%.3f,%sZ", (*p_mapping)->type == MIDI_MAP_TYPE_CC? 'c' : 'o', (*p_mapping)->code, (*p_mapping)->is_log, (*p_mapping)->min_val, (*p_mapping)->max_val, (*p_mapping)->offset_val, (*p_mapping)->message_template);
    char letter = (*p_mapping)->type == MIDI_MAP_TYPE_CC ? 'c' : ((*p_mapping)->type == MIDI_MAP_TYPE_CC_OUT ? 'C' : 'o');
    snprintf(s, len, "i%c%d,%d,", letter, (*p_mapping)->code, (*p_mapping)->is_log);
    len -= strlen(s);
    s += strlen(s);
    SNPRINT3DPCOMMA((*p_mapping)->min_val);
    SNPRINT3DPCOMMA((*p_mapping)->max_val);
    SNPRINT3DPCOMMA((*p_mapping)->offset_val);
    snprintf(s, len, "%sZ", (*p_mapping)->message_template);
    return true;
}

bool midi_mappings_exist_for_channel(int channel) {
    if (!mapping_channel_ok(channel))  return false;
    if (midi_mapping_find(channel, MIDI_MAP_TYPE_ANY, MIDI_MAP_CODE_ANY)) return true;
    return false;
}

float map_midi_value(struct midi_mapping *mapping, uint8_t value) {
    float ret_val = 0;
    if (mapping->is_log != 0) {
        ret_val = (mapping->min_val + mapping->offset_val)
            * expf(
                logf((mapping->max_val + mapping->offset_val)
                     / (mapping->min_val + mapping->offset_val))
                * (float)value / 127.0f
              )
            - mapping->offset_val;
    } else {  // Linear.
        ret_val = mapping->min_val
            + (mapping->max_val - mapping->min_val)
              * (float)value / 127.0f;
    }
    return ret_val;
}

// The inverse of map_midi_value: a parameter value back to 0..127.  Values
// outside N..X clamp, as does anything a log mapping can't take the log of.
uint8_t unmap_midi_value(struct midi_mapping *mapping, float value) {
    float v;
    if (mapping->is_log != 0) {
        float lo = mapping->min_val + mapping->offset_val;
        float hi = mapping->max_val + mapping->offset_val;
        float x = value + mapping->offset_val;
        if (lo <= 0 || hi <= 0 || lo == hi)  return 0;
        if (x <= 0)  x = (hi > lo) ? lo : hi;  // below the bottom of an upward map
        v = 127.0f * logf(x / lo) / logf(hi / lo);
    } else {
        if (mapping->max_val == mapping->min_val)  return 0;
        v = 127.0f * (value - mapping->min_val) / (mapping->max_val - mapping->min_val);
    }
    int iv = (int)lrintf(v);
    return (uint8_t)MAX(0, MIN(127, iv));
}

// midi_cc_output (iC): called for every event addressed to a synth, as it
// is ingested -- the same moment note_output sends a note, so a sequenced
// change goes out on its step.  Only events headed for live execution
// count: an event being stored into a patch changes nothing yet.
void midi_cc_output_handle_event(amy_event *e, struct delta **queue) {
    if (!mappings_inited || AMY_IS_UNSET(e->synth) || queue != &amy_global.delta_queue)  return;
    if (!mapping_channel_ok(e->synth))  return;
    struct midi_mapping *mapping = midi_cc_out_mapping_root_by_chan[e->synth];
    if (mapping == NULL)  return;  // the common case: one pointer read.
    // Where the CCs go, and whether a change that itself arrived over MIDI
    // is echoed: the synth's MIDI note output decides both if it has one,
    // so a synth's notes and controls leave on the same channel.
    uint8_t channel;
    bool forward_midi_in;
    if (!note_output_midi_channel(e->synth, &channel, &forward_midi_in)) {
        // Otherwise the synth number is the channel -- if it can be one.
        // A synth outside 1..16 has no channel to send on, and guessing one
        // would put its CCs on somebody else's; midi_store_mapping said so
        // when the mapping was made.
        if (e->synth < 1 || e->synth > 16)  return;
        channel = e->synth;
        forward_midi_in = false;
    }
    // Without this a thru-patched port, or a midi_cc on the same CC, is a
    // feedback loop.
    if (AMY_IS_SET(e->note_source_channel) && !forward_midi_in)  return;
    for (; mapping != NULL; mapping = mapping->next) {
        for (int t = 0; t < mapping->num_targets; ++t) {
            float value = 0;
            if (!amy_event_get_param(e, mapping->targets[t].param, mapping->targets[t].osc, &value))
                continue;
            uint8_t cc_val = unmap_midi_value(mapping, value);
            // Send on change only: an event can reach here more than once,
            // and a CC that didn't move is noise on a slow cable.
            if (cc_val != mapping->last_sent) {
                uint8_t bytes[3] = { (uint8_t)(0xB0 | ((channel - 1) & 0x0F)), (uint8_t)(mapping->code & 0x7F), cc_val };
                midi_out(bytes, 3);
                mapping->last_sent = cc_val;
            }
            break;  // one CC per mapping per event, whichever target matched
        }
    }
}

void substitute_midi_special_values(char *dest, const char *src, int channel, int code, float value) {
    // Copy src string to dest, but replace "%i" with channel and "%v" with value.
    const char *s;
    const char *entry_src = src;
    int n_remain = AMY_WIRE_COMMAND_LEN - 1;
    while((s = strchr(src, '%')) != NULL && n_remain > (int)strlen(src)) {
        // Copy up to the %.
        int nchars = s - src;
        strncpy(dest, src, nchars);
        dest += nchars;
        n_remain -= nchars;
        src += nchars;
        ++src;  // skip over the '%'
        dest[0] = '\0';
        if (src[0] == 'v') {
            sprintf(dest, "%.3f", value);
        } else if (src[0] == 'V') {
            sprintf(dest, "%d", (int)value);
        } else if (src[0] == 'i') {
            sprintf(dest, "%d", channel);
        } else if (src[0] == 'n') {  // 'n' is for note.
            sprintf(dest, "%d", code);
        } else {
            fprintf(stderr, "substitute_midi: unrecognized '%%%c' in %s\n", src[0], entry_src);
        }
        ++src;  // skip over the code char
        nchars = strlen(dest);
        dest += nchars;
        n_remain -= nchars;
    }
    // Copy anything left in the string.
    if (n_remain > (int)strlen(src)) strcpy(dest, src);
}

struct midi_cmd_yield_state {
    size_t pos;
    char *message;
    // Direct-parameter mappings yield one event per target instead of
    // parsing a message.  The targets are copied here, so a mapping changed
    // between yields can't pull them out from under us.
    uint8_t target;
    uint8_t num_targets;
    float value;
    struct midi_param_target targets[MIDI_MAP_MAX_TARGETS];
};

void *yield_midi_message_handler_events(uint8_t status, uint16_t channel, uint8_t * data, uint16_t len, uint32_t time, amy_event *event, void *state) {
    //fprintf(stderr, "time %.3f midi_msg_handler: status 0x%x chan %d 0x%x 0x%x\n", amy_global.time, status, channel, data[0], data[1]);
    //fprintf_event_stderr(event);
    //
    struct midi_cmd_yield_state *yield_state = (struct midi_cmd_yield_state *)state;
    if (len < 2)  return NULL;  // Every status we act on carries two data bytes.
    if (status == 0xB0
        || ((!instrument_number_exists(channel, NULL) || instrument_grab_midi_notes(channel))
            && (status == 0x90 || status == 0x80))) {  // CC or note-on with grab_midi set.
        int type = (status == 0xB0) ? MIDI_MAP_TYPE_CC : MIDI_MAP_TYPE_NOTE;
        int code = data[0];  // note for note-on events
        struct midi_mapping **p_mapping = midi_mapping_find(channel, type, code);
        struct midi_mapping *mapping = &default_note_mapping;
        if (type == MIDI_MAP_TYPE_NOTE || p_mapping != NULL) {
            if (p_mapping != NULL)
                mapping = *p_mapping;
            if (yield_state == NULL) {
                // First call to this mapping, allocate state, perform processing.
                yield_state = malloc_caps(sizeof(struct midi_cmd_yield_state) + AMY_WIRE_COMMAND_LEN, amy_global.config.ram_caps_events);
                // On OOM drop this midi message.
                if (yield_state == NULL) {
                    amy_oom("yield_midi_message_handler_events: out of memory\n");
                    return NULL;
                }
                char *message = yield_state->message = (char *)(yield_state + 1);
                yield_state->pos = 0;
                // And now set up the message
                float value = map_midi_value(mapping, (data[1] == 0xFF)? 0 : data[1]);  // suppress "fake note on" value.
                if (status == 0x80) {  // Translate note-off to note-on with vel 0.
                    status = 0x90;
                    value = 0;
                }
                yield_state->value = value;
                yield_state->target = 0;
                yield_state->num_targets = mapping->num_targets;
                if (mapping->num_targets > 0)
                    memcpy(yield_state->targets, mapping->targets, mapping->num_targets * sizeof(mapping->targets[0]));
                else
                    substitute_midi_special_values(message, mapping->message_template, channel, code, value);
                // Mark the event as already passed through mapping for this
                // channel, so we don't send it back out again.
                event->note_source_channel = channel;
                // If we're given a time, set it in the event.
                if (AMY_IS_SET(time)) event->time = time;
            }  // If state is non-null, assume we're working through the later yields.
            bool done;
            if (yield_state->num_targets > 0) {
                // Direct parameter: one event per target, addressed to this
                // synth, so the value reaches every voice (at the target's
                // voice-relative osc) by the ordinary synth path.  Like the
                // message path, the call after the last event yields
                // nothing and ends the iteration.
                done = (yield_state->target >= yield_state->num_targets);
                if (!done) {
                    struct midi_param_target *t = &yield_state->targets[yield_state->target++];
                    // Each yield starts from a fresh copy of the caller's
                    // base event, so each needs the MIDI marking and time.
                    event->note_source_channel = channel;
                    if (AMY_IS_SET(time)) event->time = time;
                    event->synth = (uint8_t)channel;
                    amy_event_set_param(event, t->param, t->osc, yield_state->value);
                }
            } else {
                // Layer each parsed event on top of the caller's base event, if any.
                yield_state->pos = yield_event_from_message(yield_state->message, event, yield_state->pos);
                done = (yield_state->pos == 0);
            }
            if (done) {
                // End of iteration
                free(yield_state);
                yield_state = NULL;
            }
        }
    }
    return (void *)yield_state;
}

void midi_message_handler_to_queue(uint8_t status, uint16_t channel, uint8_t * data, uint16_t len, uint32_t time, amy_event *base_event, struct delta **queue) {
    //fprintf(stderr, "time %.3f midi_msg_handler: status 0x%x chan %d 0x%x 0x%x base_event 0x%lx queue 0x%lx\n", amy_global.time, status, channel, data[0], data[1], (unsigned long)base_event, (unsigned long)queue);
    //fprintf_event_stderr(base_event);
    //
    void *state = NULL;
    if (queue == NULL)  queue = &amy_global.delta_queue;
    amy_event e;
    bool fake_note_on = (status == 0x90) && (len >= 2) && (data[1] == 0xFF);
    do {
        if (base_event) e = *base_event;
        else amy_clear_event(&e);
        state = yield_midi_message_handler_events(status, channel, data, len, time, &e, state);
        if (state != NULL) {
            if (fake_note_on) {
                AMY_UNSET(e.velocity);
            }
            amy_event_to_deltas_queue(&e, 0, /* oscs_per_voice= */ 0, queue);
        }
    } while (state != NULL);
}

void midi_msg_handler(uint8_t * bytes, uint16_t len, uint8_t is_sysex_unused, uint32_t time) {
    // The external entry point still takes raw MIDI bytes; unpack byte 0 here.
    if (len < 1)  return;
    midi_message_handler_to_queue(bytes[0] & 0xF0, (bytes[0] & 0x0F) + 1, bytes + 1, len - 1, time, NULL, NULL);
}
