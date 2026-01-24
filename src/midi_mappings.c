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

struct cc_mapping {
    struct cc_mapping *next;
    int channel;
    int code;
    int is_log;
    float min_val;
    float max_val;
    float offset_val;
    char *message_template;
};

struct cc_mapping *cc_mapping_root = NULL;

void cc_mapping_print(struct cc_mapping *mapping) {
    fprintf(stderr, "mapping 0x%lx chan %d code 0x%x log %d min %.1f max %.1f offs %.1f msg %s\n",
            (unsigned long)mapping, mapping->channel, mapping->code, mapping->is_log, mapping->min_val, mapping->max_val, mapping->offset_val, mapping->message_template);
}

struct cc_mapping *cc_mapping_init(struct cc_mapping **p_root, int channel, int code, int is_log, float min_val, float max_val, float offset_val, char *message_template) {
    struct cc_mapping *result = (struct cc_mapping *)malloc_caps(sizeof(struct cc_mapping) + strlen(message_template) + 1, amy_global.config.ram_caps_synth);
    result->message_template = ((char *)result) + sizeof(struct cc_mapping);
    result->channel = channel;
    result->code = code;
    result->is_log = is_log;
    result->min_val = min_val;
    result->max_val = max_val;
    result->offset_val = offset_val;
    strcpy(result->message_template, message_template);
    // Insert into the linked list at the head.
    result->next = *p_root;
    *p_root = result;
    return result;
}

void cc_mapping_debug(void) {
    fprintf(stderr, "cc_mapping_debug:\n");
    struct cc_mapping **p_mapping = &cc_mapping_root;
    while (*p_mapping != NULL) {
        cc_mapping_print(*p_mapping);
        p_mapping = &((*p_mapping)->next);
    }
}

void cc_mapping_free(struct cc_mapping **p_mapping) {
    // Close up the linked list.
    struct cc_mapping *doomed = *p_mapping;
    *p_mapping = doomed->next;
    // Return the memory
    free(doomed);
}

struct cc_mapping **cc_mapping_find(int channel, int code) {
    // Retrieve the mapping associated with a midi channel + code, if any.
    struct cc_mapping **p_mapping = &cc_mapping_root;
    while (*p_mapping != NULL) {
        if ((*p_mapping)->channel == channel && (*p_mapping)->code == code)  return p_mapping;
        p_mapping = &((*p_mapping)->next);
    }
    return NULL;
}

int midi_store_control_code(int channel, int code, int is_log, float min_val, float max_val, float offset_val, char *message) {
    // Register a MIDI control code and mapping and a wire code template.
    struct cc_mapping **p_mapping = cc_mapping_find(channel, code);
    if (p_mapping) cc_mapping_free(p_mapping);
    // store with an empty string removes mapping
    if (strlen(message)) {
        /* struct cc_mapping *mapping = */ cc_mapping_init(&cc_mapping_root, channel, code, is_log, min_val, max_val, offset_val, message);
        //cc_mapping_print(mapping);
    }
    return 1;
}

float map_cc_value(struct cc_mapping *mapping, uint8_t value) {
    if (mapping->is_log != 0) {
        return (mapping->min_val + mapping->offset_val)
            * expf(
                logf((mapping->max_val + mapping->offset_val)
                     / (mapping->min_val + mapping->offset_val))
                * (float)value / 127.0f
              )
            - mapping->offset_val;
    } else {  // Linear.
        return mapping->min_val
            + (mapping->max_val - mapping->min_val)
              * (float)value / 127.0f;
    }
}

#define WIRE_COMMAND_LEN 256

void substitute_cc_special_values(char *dest, const char *src, int channel, float value) {
    // Copy src string to dest, but replace "%i" with channel and "%v" with value.
    const char *s;
    const char *entry_src = src;
    int n_remain = WIRE_COMMAND_LEN - 1;
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
        } else {
            fprintf(stderr, "substitute_cc: unrecognized '%%%c' in %s\n", src[0], entry_src);
        }
        ++src;  // skip over the code char
        nchars = strlen(dest);
        dest += nchars;
        n_remain -= nchars;
    }
    // Copy anything left in the string.
    if (n_remain > (int)strlen(src)) strcpy(dest, src);
}

void midi_cc_handler(uint8_t * bytes, uint16_t len, uint8_t is_sysex) {
    if ((bytes[0] & 0xF0) == 0xB0) {  // CC
        int channel = bytes[0] & 0x0F;
        int code = bytes[1];
        struct cc_mapping **p_mapping = cc_mapping_find(channel, code);
        if (p_mapping != NULL) {
            float value = map_cc_value(*p_mapping, bytes[2]);
            char message[WIRE_COMMAND_LEN];
            substitute_cc_special_values(message, (*p_mapping)->message_template, channel, value);
            fprintf(stderr, "midi_cc_handler: message %s\n", message);
            amy_add_message(message);
        }
    }
}
