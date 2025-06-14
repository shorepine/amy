// parse.c 
// handle parsing wire strings

#include "amy.h"
#include <ctype.h>  // for isalpha().

float atoff(const char *s) {
    // Returns float value corresponding to parseable prefix of s.
    // Unlike atof(), it does not recognize scientific format ('e' or 'E')
    // and will stop parsing there.  Needed for message strings that contain
    // 'e' as a command prefix.
    float frac = 0;
    // Skip leading spaces.
    while (*s == ' ') ++s;
    float whole = (float)atoi(s);
    int is_negative = (s[0] == '-');  // Can't use (whole < 0) because of "-0.xx".
    //const char *s_in = s;  // for debug message.
    s += strspn(s, "-0123456789");
    if (*s == '.') {
        // Float with a decimal part.
        // Step over dp
        ++s;
        // Extract fractional part.
        int fraclen = strspn(s, "0123456789");
        char fracpart[8];
        // atoi() will overflow for values larger than 2^31, so only decode a prefix.
        if (fraclen > 6) {
            for(int i = 0; i < 7; ++i) {
                fracpart[i] = s[i];
            }
            fracpart[7] = '\0';
            s = fracpart;
            fraclen = 7;
        }
        frac = (float)atoi(s);
        frac /= powf(10.f, (float)fraclen);
        if (is_negative) frac = -frac;
    }
    //fprintf(stderr, "input was %s output is %f + %f = %f\n", s_in, whole, frac, whole+frac);
    return whole + frac;
}


#define PARSE_LIST_STRSPN1(var) _Generic((var), \
    float:    " -0123456789,.", \
    uint32_t: " 0123456789,", \
    uint16_t: " 0123456789,", \
    int16_t:  " -0123456789,", \
    int32_t:  " -0123456789," \
)

#define PARSE_LIST_STRSPN2(var) _Generic((var), \
    float:    "-0123456789.", \
    uint32_t: "0123456789", \
    uint16_t: "0123456789", \
    int16_t:  "-0123456789", \
    int32_t:  "-0123456789" \
)

#define PARSE_LIST_ATO(var) _Generic((var), \
    float:    atoff, \
    uint32_t: atoi, \
    uint16_t: atoi, \
    int32_t:  atoi, \
    int16_t:  atoi \
)

#define PARSE_LIST(type) \
    int parse_list_##type(char *message, type *vals, int max_num_vals, type skipped_val) { \
        uint16_t c = 0, last_c; \
        uint16_t stop = strspn(message, PARSE_LIST_STRSPN1(skipped_val)); \
        int num_vals_received = 0; \
        while(c < stop && num_vals_received < max_num_vals) { \
            *vals = PARSE_LIST_ATO(skipped_val)(message + c); \
            while (message[c] == ' ') ++c; \
            last_c = c; \
            c += strspn(message + c, PARSE_LIST_STRSPN2(skipped_val));  \
            if (last_c == c)  \
                *vals = skipped_val;  \
            while (message[c] != ',' && message[c] != 0 && c < MAX_MESSAGE_LEN) c++; \
            ++c; \
            ++vals; \
            ++num_vals_received; \
        } \
        if (c < stop) { \
            fprintf(stderr, "WARNING: parse__list_##type: More than %d values in \"%s\"\n", \
                max_num_vals, message); \
        } \
        return num_vals_received; \
    }


PARSE_LIST(float)
PARSE_LIST(uint32_t)
PARSE_LIST(uint16_t)
PARSE_LIST(int32_t)
PARSE_LIST(int16_t)



void copy_param_list_substring(char *dest, const char *src) {
    // Copy wire command string up to next parameter char.
    uint16_t c = 0;
    uint16_t stop = strspn(src, " 0123456789-,.");  // Note space & period.
    while (c < stop && src[c]) {
        dest[c] = src[c];
        c++;
    }
    dest[c] = '\0';
}

// helper to parse the list of source oscs for an algorithm
void parse_algo_source(char *message, int16_t *vals) {
    int num_parsed = parse_list_int16_t(message, vals, MAX_ALGO_OPS,
                                        AMY_UNSET_VALUE(vals[0]));
    // Clear unspecified values.
    for (int i = num_parsed; i < MAX_ALGO_OPS; ++i) {
        AMY_UNSET(vals[i]);
    }
}

void parse_voices(char *message, uint16_t *vals) {
    int num_parsed = parse_list_uint16_t(message, vals, MAX_VOICES_PER_INSTRUMENT,
                                         AMY_UNSET_VALUE(vals[0]));
    // Clear unspecified values.
    for (int i = num_parsed; i < MAX_VOICES_PER_INSTRUMENT; ++i) {
        AMY_UNSET(vals[i]);
    }
}

uint32_t ms_to_samples(uint32_t ms) {
    uint32_t samps = 0;
    if (AMY_IS_UNSET(ms)) return AMY_UNSET_VALUE(samps);
    samps = (uint32_t)(((float)ms / 1000.0) * (float)AMY_SAMPLE_RATE);
    return samps;
}

float int_db_to_float_lin(uint32_t db) {
    float lin = 0;
    if (AMY_IS_UNSET(db)) return AMY_UNSET_VALUE(lin);
    lin = powf(10.0f, ((((float)db) - 100.0f) / 20.0f)) - 0.001f;
    if (lin < 0) return 0;
    return lin;
}

// helper to parse the special bp string
int parse_breakpoint_core_float_lin(struct synthinfo * s, char* message, uint8_t which_bpset) {
    // This is the classic version, int_time_delta_ms,float_lin_val,...
    float vals[2 * MAX_BREAKPOINTS];
    // Read all the values as floats.
    int num_vals = parse_list_float(message, vals, 2 * MAX_BREAKPOINTS,
                                    AMY_UNSET_VALUE(vals[0]));
    // Distribute out to times and vals, casting times to ints.
    for (int i = 0; i < num_vals; ++i) {
        int bp_index = (i >> 1);
        if (bp_index >= s->max_num_breakpoints[which_bpset]) {
            fprintf(stderr, "overran bpset %d length %d trying to write %d\n", which_bpset, s->max_num_breakpoints[which_bpset], bp_index);
        } else {
            if ((i % 2) == 0)
                if (AMY_IS_SET(vals[i]))
                    s->breakpoint_times[which_bpset][bp_index] = ms_to_samples((int)vals[i]);
                else  // Have to translate the "unset" value for the non-float type.
                    s->breakpoint_times[which_bpset][bp_index] = AMY_UNSET_VALUE(s->breakpoint_times[which_bpset][bp_index]);
            else
                s->breakpoint_values[which_bpset][bp_index] = vals[i];
        }
    }
    return num_vals;
}

int parse_breakpoint_core_int_db(struct synthinfo * s, char* message, uint8_t which_bpset) {
    // This is for the special faster additive-synth version, int_time_delta_ms,uint_db_val,...
    uint32_t vals[2 * MAX_BREAKPOINTS];
    // Read all the values as floats.
    int num_vals = parse_list_uint32_t(message, vals, 2 * MAX_BREAKPOINTS,
                                       AMY_UNSET_VALUE(vals[0]));
    // Distribute out to times and vals, casting int db values to linear floats.
    // Both ms_to_samples and int_db_to_float_lin pass through AMY_UNSET suitably translated.
    for (int i = 0; i < num_vals; ++i) {
        int bp_index = (i >> 1);
        if (bp_index >= s->max_num_breakpoints[which_bpset]) {
            fprintf(stderr, "overran bpset %d length %d trying to write %d\n", which_bpset, s->max_num_breakpoints[which_bpset], bp_index);
        } else {
            if ((i % 2) == 0)
                s->breakpoint_times[which_bpset][bp_index] = ms_to_samples(vals[i]);
            else
                s->breakpoint_values[which_bpset][bp_index] = int_db_to_float_lin(vals[i]);
        }
    }
    return num_vals;
}

int parse_breakpoint(struct synthinfo * s, char* message, uint8_t which_bpset) {
    int num_vals;
    // Test for special case BP string that begins ".." meaning it's unsigned integer dB values.
    if (message[0] == '.' && message[1] == '.') {
        num_vals = parse_breakpoint_core_int_db(s, message + 2, which_bpset);
    } else {
        num_vals = parse_breakpoint_core_float_lin(s, message, which_bpset);
    }
    // Values that are not specified at the end of the list indicate the total length of the BP set.
    for (int i = num_vals; i < 2 * MAX_BREAKPOINTS; ++i) {
        int bp_index = (i >> 1);
        if (bp_index < s->max_num_breakpoints[which_bpset]) {
            if ((i % 2) == 0)
                AMY_UNSET(s->breakpoint_times[which_bpset][bp_index]);
            else
                AMY_UNSET(s->breakpoint_values[which_bpset][bp_index]);
        }
    }
    // Return the number of breakpoints that were present.
    return (num_vals + 1) >> 1;
}

void parse_coef_message(char *message, float *coefs) {
    int num_coefs = parse_list_float(message, coefs, NUM_COMBO_COEFS,
                                             AMY_UNSET_VALUE(coefs[0]));
    // Clear the unspecified coefs to unset.
    for (int i = num_coefs; i < NUM_COMBO_COEFS; ++i)
        coefs[i] = AMY_UNSET_VALUE(coefs[0]);
}

// Parser for synth-layer ('i') prefix.
void amy_parse_synth_layer_message(char *message, amy_event *e) {
    if (message[0] >= '0' && message[0] <= '9') {
        // It's just the instrument number.
        e->synth = atoi(message);
        return;
    }
    char cmd = message[0];
    message++;
    if (cmd == 'p')  e->pedal = atoi(message);
    else if (cmd == 'f')  e->synth_flags = atoi(message);
    else if (cmd == 'v')  e->num_voices = atoi(message);
    else if (cmd == 't')  e->to_synth = atoi(message);
    else if (cmd == 'm')  e->grab_midi_notes = atoi(message);
    else fprintf(stderr, "Unrecognized synth-level command '%s'\n", message - 1);
}

int _next_alpha(char *s) {
    // Return how many chars to skip to get to the next alphabet (command prefix) (or EOS).
    int p = 0;
    while (*(s + p)) {
        char c = *(s + p);
        if (isalpha(c))  break;
        ++p;
    }
    return p;
}



// given a string return a parsed event
void amy_parse_message(char * message, int length, amy_event *e) {
    peek_stack("parse_message");
    char cmd = '\0';
    uint16_t pos = 0;

    // Check if we're in a transfer block, if so, parse it and leave this loop 
    if(amy_global.transfer_flag) {
        parse_transfer_message(message, length);
        e->status = EVENT_TRANSFER_DATA;
        return;
    }

    while(pos < length) {
        cmd = message[pos];
        char *arg = message + pos + 1;
        if(isalpha(cmd)) {
            switch(cmd) {
            case 'a': parse_coef_message(arg, e->amp_coefs);break;
            case 'A': copy_param_list_substring(e->bp0, arg); e->bp_is_set[0] = 1; break;
            case 'B': copy_param_list_substring(e->bp1, arg); e->bp_is_set[1] = 1; break;
            case 'b': e->feedback = atoff(arg); break;
            case 'c': e->chained_osc = atoi(arg); break;
            /* C available */
            case 'd': parse_coef_message(arg, e->duty_coefs);break;
            case 'D': show_debug(atoi(arg)); break;
            case 'f': parse_coef_message(arg, e->freq_coefs);break;
            case 'F': parse_coef_message(arg, e->filter_freq_coefs); break;
            case 'G': e->filter_type = atoi(arg); break;
            /* g used for Alles for client # */
            case 'H': parse_list_uint32_t(arg, e->sequence, 3, 0); break;
            case 'h': if (AMY_HAS_REVERB) {
                float reverb_params[4] = {AMY_UNSET_VALUE(amy_global.reverb.liveness), AMY_UNSET_VALUE(amy_global.reverb.liveness),
                                          AMY_UNSET_VALUE(amy_global.reverb.liveness), AMY_UNSET_VALUE(amy_global.reverb.liveness)};
                parse_list_float(arg, reverb_params, 4, AMY_UNSET_VALUE(amy_global.reverb.liveness));
                // config_reverb doesn't understand UNSET, so copy in the current values.
                if (AMY_IS_UNSET(reverb_params[0])) reverb_params[0] = S2F(amy_global.reverb.level);
                if (AMY_IS_UNSET(reverb_params[1])) reverb_params[1] = amy_global.reverb.liveness;
                if (AMY_IS_UNSET(reverb_params[2])) reverb_params[2] = amy_global.reverb.damping;
                if (AMY_IS_UNSET(reverb_params[3])) reverb_params[3] = amy_global.reverb.xover_hz;
                config_reverb(reverb_params[0], reverb_params[1], reverb_params[2], reverb_params[3]);
            }
            break;
            /* i is used by alles for sync index -- but only for sync messages -- ok to use here but test */
            case 'i': amy_parse_synth_layer_message(arg, e); ++pos; break;  // Skip over second cmd letter, if any.
            case 'I': e->ratio = atoff(arg); break;
            case 'j': e->tempo = atof(arg); break;
            /* j, J available */
            // chorus.level 
            case 'k': if(AMY_HAS_CHORUS) {
                float chorus_params[4] = {AMY_UNSET_FLOAT, AMY_UNSET_FLOAT, AMY_UNSET_FLOAT, AMY_UNSET_FLOAT};
                parse_list_float(arg, chorus_params, 4, AMY_UNSET_FLOAT);
                // config_chorus doesn't understand UNSET, copy existing values.
                if (AMY_IS_UNSET(chorus_params[0])) chorus_params[0] = S2F(amy_global.chorus.level);
                if (AMY_IS_UNSET(chorus_params[1])) chorus_params[1] = (float)amy_global.chorus.max_delay;
                if (AMY_IS_UNSET(chorus_params[2])) chorus_params[2] = amy_global.chorus.lfo_freq;
                if (AMY_IS_UNSET(chorus_params[3])) chorus_params[3] = amy_global.chorus.depth;
                config_chorus(chorus_params[0], (int)chorus_params[1], chorus_params[2], chorus_params[3]);
            }
            break;
            case 'K': e->patch_number = atoi(arg); break;
            case 'l': e->velocity=atoff(arg); break;
            case 'L': e->mod_source=atoi(arg); break;
            case 'm': e->portamento_ms=atoi(arg); break;
            case 'M': if (AMY_HAS_ECHO) {
                float echo_params[5] = {AMY_UNSET_FLOAT, AMY_UNSET_FLOAT, AMY_UNSET_FLOAT, AMY_UNSET_FLOAT, AMY_UNSET_FLOAT};
                parse_list_float(arg, echo_params, 5, AMY_UNSET_FLOAT);
                if (AMY_IS_UNSET(echo_params[0])) echo_params[0] = S2F(amy_global.echo.level);
                if (AMY_IS_UNSET(echo_params[1])) echo_params[1] = (float)amy_global.echo.delay_samples * 1000.f / AMY_SAMPLE_RATE;
                if (AMY_IS_UNSET(echo_params[2])) echo_params[2] = (float)amy_global.echo.max_delay_samples * 1000.f / AMY_SAMPLE_RATE;
                if (AMY_IS_UNSET(echo_params[3])) echo_params[3] = S2F(amy_global.echo.feedback);
                if (AMY_IS_UNSET(echo_params[4])) echo_params[4] = S2F(amy_global.echo.filter_coef);
                config_echo(echo_params[0], echo_params[1], echo_params[2], echo_params[3], echo_params[4]);
            }
            break;
            case 'n': e->midi_note=atof(arg); break;
            case 'N': e->latency_ms = atoi(arg);  break;
            case 'o': e->algorithm=atoi(arg); break;
            case 'O': parse_algo_source(arg, e->algo_source); break;
            case 'p': e->preset=atoi(arg); break;
            case 'P': e->phase=atoff(arg); break;
            /* q unused */
            case 'Q': parse_coef_message(arg, e->pan_coefs); break;
            case 'r': parse_voices(arg, e->voices); break;
            case 'R': e->resonance=atoff(arg); break;
            case 's': e->pitch_bend = atoff(arg); break;
            case 'S':
                e->reset_osc = atoi(arg);
                // if we're resetting all of AMY, do it now
                if (e->reset_osc & (RESET_AMY | RESET_TIMEBASE | RESET_EVENTS | RESET_SYNTHS)) {
                    if(e->reset_osc & RESET_AMY) {
                        amy_stop();
                        amy_start(amy_global.config);
                    }
                    // if we're resetting timebase, do it NOW
                    if(e->reset_osc & RESET_TIMEBASE) {
                        amy_reset_sysclock();
                    }
                    if(e->reset_osc & RESET_EVENTS) {
                        amy_deltas_reset();
                    }
                    if(e->reset_osc & RESET_SYNTHS) {
                        amy_reset_oscs();
                    }
                    AMY_UNSET(e->reset_osc);
                }
                break;
            /* t used for time */
            case 't': e->time=atol(arg); break;
            case 'T': e->eg_type[0] = atoi(arg); break;
            case 'u': patches_store_patch(e, arg); pos = strlen(message) - 1; break;  // patches_store_patch processes the patch as all the rest of the message and maybe sets patch.
            /* U used by Alles for sync */
            case 'v': e->osc=((atoi(arg)) % (AMY_OSCS+1));  break; // allow osc wraparound
            case 'V': e->volume = atoff(arg); break;
            case 'w': e->wave=atoi(arg); break;
            /* W used by Tulip for CV, external_channel */
            case 'X': e->eg_type[1] = atoi(arg); break;
            case 'x': {
                  float eq[3] = {AMY_UNSET_VALUE(e->eq_l), AMY_UNSET_VALUE(e->eq_m), AMY_UNSET_VALUE(e->eq_h)};
                  parse_list_float(arg, eq, 3, AMY_UNSET_VALUE(e->eq_l));
                  e->eq_l = eq[0];
                  e->eq_m = eq[1];
                  e->eq_h = eq[2];
                }
                break;
            case 'z': {
                uint32_t sm[6]; // preset, length, SR, midinote, loop_start, loopend
                parse_list_uint32_t(arg, sm, 6, 0);
                if(sm[1]==0) { // remove preset
                    pcm_unload_preset(sm[0]);
                } else {
                    int16_t * ram = pcm_load(sm[0], sm[1], sm[2], sm[3],sm[4], sm[5]);
                    start_receiving_transfer(sm[1]*2, (uint8_t*)ram);
                }
                break;
            }
            /* Y,y available */
            /* Z used for end of message */
            case 'Z':
                break;
            default:
                break;
            }
        }
        // Skip over arg, line up for the next cmd.
        ++pos;  // move over the current command.
        if (pos > length) fprintf(stderr, "parse string overrun %d %d %s\n", pos, length, message);
        pos += _next_alpha(message + pos);  // Skip over any non-alpha argument to the current command.
    }
}
