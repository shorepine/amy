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
    // What we actually do.
    char *message_template;
};

bool mappings_inited = false;

struct midi_mapping *midi_cc_mapping_root_by_chan[AMY_NUM_MIDI_CHANNELS + 1];
struct midi_mapping *midi_note_mapping_root_by_chan[AMY_NUM_MIDI_CHANNELS + 1];

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
    if (channel < 1 || channel > AMY_NUM_MIDI_CHANNELS)  return NULL;
    struct midi_mapping **p_root;
    if (type == MIDI_MAP_TYPE_CC)
        p_root = &midi_cc_mapping_root_by_chan[channel];
    else
        p_root = &midi_note_mapping_root_by_chan[channel];
    struct midi_mapping *result = (struct midi_mapping *)malloc_caps(sizeof(struct midi_mapping) + message_len + 1, amy_global.config.ram_caps_synth);
    result->message_template = ((char *)result) + sizeof(struct midi_mapping);
    result->channel = channel;
    result->type = type;
    result->code = code;
    result->is_log = is_log;
    result->min_val = min_val;
    result->max_val = max_val;
    result->offset_val = offset_val;
    strncpy(result->message_template, message_template, message_len);
    result->message_template[message_len] = '\0';
    // Insert into the linked list at the head.
    result->next = *p_root;
    *p_root = result;
    return result;
}

void midi_mapping_debug(void) {
    fprintf(stderr, "midi_mapping_debug:\n");
    for (int channel = 1; channel < AMY_NUM_MIDI_CHANNELS + 1; ++channel) {
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
    for (int channel = 1; channel < AMY_NUM_MIDI_CHANNELS + 1; ++channel) {
        midi_cc_mapping_root_by_chan[channel] = NULL;
        midi_note_mapping_root_by_chan[channel] = NULL;
    }
    mappings_inited = true;
}

void midi_mappings_deinit(void) {
    if (mappings_inited) {
        for (int channel = 1; channel < AMY_NUM_MIDI_CHANNELS + 1; ++channel) {
            midi_mappings_free(&midi_cc_mapping_root_by_chan[channel]);
            midi_mappings_free(&midi_note_mapping_root_by_chan[channel]);
        }
        mappings_inited = false;
    }
}

void midi_clear_channel_mappings(int channel, int type) {
    if (channel < 1 || channel > AMY_NUM_MIDI_CHANNELS)  return;
    if (type == MIDI_MAP_TYPE_ANY) {
        midi_clear_channel_mappings(channel, MIDI_MAP_TYPE_CC);
        midi_clear_channel_mappings(channel, MIDI_MAP_TYPE_NOTE);
    }
    struct midi_mapping **p_mapping;
    if (type == MIDI_MAP_TYPE_CC)
        p_mapping = &midi_cc_mapping_root_by_chan[channel];
    else
        p_mapping = &midi_note_mapping_root_by_chan[channel];
    while (*p_mapping != NULL) {
        if ((*p_mapping)->channel == channel && ((type == MIDI_MAP_TYPE_ANY) || ((*p_mapping)->type == type))) {
            midi_mapping_free(p_mapping);
        } else {
            p_mapping = &((*p_mapping)->next);
        }
    }
    // Stop listening to this MIDI channel unless there's a synth on it.
    if (!instrument_number_exists(channel, NULL))
        midi_active_channel_set(channel, false);
}

struct midi_mapping **midi_mapping_find(int channel, int type, int code) {
    if (channel < 1 || channel > AMY_NUM_MIDI_CHANNELS)  return NULL;
    // Retrieve the mapping associated with a midi channel + code, if any.
    struct midi_mapping **result;
    if (type == MIDI_MAP_TYPE_ANY) {
        result = midi_mapping_find(channel, MIDI_MAP_TYPE_CC, code);
        if (result == NULL)
            result = midi_mapping_find(channel, MIDI_MAP_TYPE_NOTE, code);
        return result;
    }
    struct midi_mapping **p_mapping;
    if (type == MIDI_MAP_TYPE_CC)
        p_mapping = &midi_cc_mapping_root_by_chan[channel];
    else
        p_mapping = &midi_note_mapping_root_by_chan[channel];
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
    if (channel < 1 || channel > AMY_NUM_MIDI_CHANNELS)  return 0;
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
    if (channel < 1 || channel > AMY_NUM_MIDI_CHANNELS)  return 0;
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
    struct midi_mapping **p_mapping = midi_mapping_find(channel, type, code);
    if (p_mapping) midi_mapping_free(p_mapping);
    // store with an empty string removes mapping
    if (message_len) {
        /* struct midi_mapping *mapping = */ midi_mapping_init(channel, type, code, is_log, min_val, max_val, offset_val, message, message_len);
        //midi_mapping_debug();
    }
    // We just deleted a mapping on this channel, was it the last one?
    midi_active_channel_set(channel, midi_mappings_exist_for_channel(channel) || instrument_number_exists(channel, NULL));
    return 1;
}

bool midi_fetch_mapping_command(int channel, int type, int code, char *s, size_t len) {
    struct midi_mapping **p_mapping = midi_mapping_find(channel, type, code);
    //fprintf(stderr, "midi_fetch_mapping_command chan %d type %d code %d mapping 0x%llx\n", channel, type, code, (uint64_t)p_mapping);
    if (p_mapping == NULL)
        return false;
    // Format the control code - ic<C>,<L>,<N>,<X>,<O>,<CODE>
    sprintf(s, "i%c%d,%d,%.3f,%.3f,%.3f,%sZ", (*p_mapping)->type == MIDI_MAP_TYPE_CC? 'c' : 'o', (*p_mapping)->code, (*p_mapping)->is_log, (*p_mapping)->min_val, (*p_mapping)->max_val, (*p_mapping)->offset_val, (*p_mapping)->message_template);
    assert(strlen(s) < len);
    return true;
}

bool midi_mappings_exist_for_channel(int channel) {
    if (channel < 1 || channel > AMY_NUM_MIDI_CHANNELS)  return false;
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
};

void *yield_midi_message_handler_events(uint8_t * bytes, uint16_t len, uint32_t time, amy_event *event, void *state) {
    //fprintf(stderr, "time %.3f midi_msg_handler: 0x%x 0x%x 0x%x\n", amy_global.time, bytes[0], bytes[1], bytes[2]);
    //fprintf_event_stderr(event);
    //
    struct midi_cmd_yield_state *yield_state = (struct midi_cmd_yield_state *)state;
    uint8_t status = bytes[0] & 0xF0;
    uint8_t channel = (bytes[0] & 0x0F) + 1;
    if (status == 0xB0
        || ((!instrument_number_exists(channel, NULL) || instrument_grab_midi_notes(channel))
            && (status == 0x90 || status == 0x80))) {  // CC or note-on with grab_midi set.
        int type = (status == 0xB0) ? MIDI_MAP_TYPE_CC : MIDI_MAP_TYPE_NOTE;
        int code = bytes[1];  // note for note-on events
        struct midi_mapping **p_mapping = midi_mapping_find(channel, type, code);
        struct midi_mapping *mapping = &default_note_mapping;
        if (type == MIDI_MAP_TYPE_NOTE || p_mapping != NULL) {
            if (p_mapping != NULL)
                mapping = *p_mapping;
            if (yield_state == NULL) {
                // First call to this mapping, allocate state, perform processing.
                yield_state = malloc_caps(sizeof(struct midi_cmd_yield_state) + AMY_WIRE_COMMAND_LEN, amy_global.config.ram_caps_events);
                char *message = yield_state->message = (char *)(yield_state + 1);
                yield_state->pos = 0;
                // And now set up the message
                float value = map_midi_value(mapping, (bytes[2] == 0xFF)? 0 : bytes[2]);  // suppress "fake note on" value.
                if (status == 0x80) {  // Translate note-off to note-on with vel 0.
                    status = 0x90;
                    value = 0;
                }
                // Mark message as already passed through mapping for this channel (synth).
                sprintf(message, "iM%d", channel);
                int offset = strlen(message);
                if (AMY_IS_SET(time)) {
                    sprintf(message + offset, "t%" PRId32, time);
                    offset = strlen(message);
                }
                substitute_midi_special_values(message + offset, mapping->message_template, channel, code, value);
            }  // If state is non-null, assume we're working through the later yields.
            // Layer each parsed event on top of the caller's base event, if any.
            yield_state->pos = yield_event_from_message(yield_state->message, event, yield_state->pos);
            if (yield_state->pos == 0) {
                // End of iteration
                free(yield_state);
                yield_state = NULL;
            }
        }
    }
    return (void *)yield_state;
}

void midi_message_handler_to_queue(uint8_t * bytes, uint16_t len, uint32_t time, amy_event *base_event, struct delta **queue) {
    //fprintf(stderr, "time %.3f midi_msg_handler: 0x%x 0x%x 0x%x base_event 0x%lx queue 0x%lx\n", amy_global.time, bytes[0], bytes[1], bytes[2], (unsigned long)base_event, (unsigned long)queue);
    //fprintf_event_stderr(base_event);
    //
    void *state = NULL;
    if (queue == NULL)  queue = &amy_global.delta_queue;
    amy_event e;
    bool fake_note_on = (((bytes[0] & 0xF0) == 0x90) && (bytes[2] == 0xFF));
    do {
        if (base_event) e = *base_event;
        else amy_clear_event(&e);
        state = yield_midi_message_handler_events(bytes, len, time, &e, state);
        if (state != NULL) {
            if (fake_note_on) {
                AMY_UNSET(e.velocity);
            }
            amy_event_to_deltas_queue(&e, 0, queue);
        }
    } while (state != NULL);
}

void midi_msg_handler(uint8_t * bytes, uint16_t len, uint8_t is_sysex_unused, uint32_t time) {
    midi_message_handler_to_queue(bytes, len, time, NULL, NULL);
}
