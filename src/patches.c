// patches.c
// baked in AMY string patches (Juno-6 & DX7 for now)

#include "amy.h"
#include "patches.h"

#include <assert.h>   // for buffer overruns in sprint_event.

#define _PATCHES_FIRST_USER_PATCH 1024


uint32_t max_num_memory_patches = 0;
struct delta **memory_patch_deltas = NULL;
uint16_t *memory_patch_oscs = NULL;
uint16_t next_user_patch_index = 0;
uint8_t * osc_to_voice = NULL;
uint16_t *voice_to_base_osc = NULL;

void patches_deinit() {
    memory_patch_deltas = NULL;
    memory_patch_oscs = NULL;
    osc_to_voice = NULL;
    voice_to_base_osc = NULL;
}

void patches_init(int max_memory_patches) {
    max_num_memory_patches = max_memory_patches;
    uint8_t *alloc_base = malloc_caps(
            max_num_memory_patches * sizeof(struct delta *)
	    + max_num_memory_patches * sizeof(uint16_t)
            + AMY_OSCS * sizeof(uint8_t)
            + amy_global.config.max_voices * sizeof(uint16_t),
	    amy_global.config.ram_caps_synth
    );
    memory_patch_deltas = (struct delta **)alloc_base;
    memory_patch_oscs = (uint16_t *)(memory_patch_deltas + max_num_memory_patches);
    osc_to_voice = (uint8_t *)(memory_patch_oscs + max_num_memory_patches);
    voice_to_base_osc = (uint16_t *)(osc_to_voice + AMY_OSCS);
    bzero(memory_patch_deltas, max_num_memory_patches * sizeof(struct delta *));
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
    for(uint8_t v = 0; v < amy_global.config.max_voices; v++) {
        if (AMY_IS_SET(voice_to_base_osc[v]))
            fprintf(stderr, "voice %" PRIu8 " base osc %" PRIu16 "\n", v, voice_to_base_osc[v]);
    }
    fprintf(stderr, "osc_to_voice:\n");
    for(uint16_t i=0;i<AMY_OSCS;) {
        uint16_t j = 0;
        fprintf(stderr, "%" PRIu16 ": ", i);
        for (j=0; j < 16; ++j) {
            if ((i + j) >= AMY_OSCS)  break;
            fprintf(stderr, "%" PRIu8 " ", osc_to_voice[i + j]);
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
            uint16_t cv_gate, cv_note;
            instrument_get_cv_gate(i, &cv_gate, &cv_note);
            fprintf(stderr, "synth %" PRIu8 " num_voices %" PRId32 " patch_num %" PRId32 " cv_gate %d cv_note %d flags %" PRIu32 " voices",
                    i, (int32_t)num_voices, (int32_t)instrument_get_patch_number(i), cv_gate, cv_note, instrument_get_flags(i));
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

void add_deltas_to_queue_with_baseosc(struct delta *d, int base_osc, struct delta **queue, uint32_t time) {
    //fprintf(stderr, "add_deltas_to_queue_with_baseosc: added %d baseosc %d time %d\n", delta_list_len(d), base_osc, time);
    struct delta d_offset;
    while(d) {
        d_offset = *d;
        d_offset.osc += base_osc;
        if (d_offset.param == CHAINED_OSC || d_offset.param == MOD_SOURCE || d_offset.param == RESET_OSC
            || (d_offset.param >= ALGO_SOURCE_START && d_offset.param < ALGO_SOURCE_START + MAX_ALGO_OPS))
            if (!(AMY_IS_UNSET((int16_t)d_offset.data.i) || AMY_IS_UNSET((uint16_t)d_offset.data.i)))  // CHAINED_OSC is uint16_t, but ALGO_SOURCE is int16_t.
                d_offset.data.i += base_osc;
        d_offset.time = time;
        // assume the d->time is 0 and that's good.
        add_delta_to_queue(&d_offset, &amy_global.delta_queue);
        d = d->next;
    }
}

#define _EPRINT_I(FIELD, NAME, WIRECODE) if (AMY_IS_SET(e->FIELD)) { sprintf(s, "%s%" PRId32, wirecode ? WIRECODE : " " NAME ": ", (int32_t)e->FIELD); s += strlen(s); }
#define _EPRINT_F(FIELD, NAME, WIRECODE) if (AMY_IS_SET(e->FIELD)) { sprintf(s, "%s%.3f", wirecode ? WIRECODE : " " NAME ": ", e->FIELD); s += strlen(s); }
#define _EPRINT_COEF(FIELD, NAME, WIRECODE) {            \
    int last_set = -1; \
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) {    \
        if (AMY_IS_SET(e->FIELD[i])) last_set = i; \
    }                                              \
    if (last_set >= 0) { \
        sprintf(s, "%s", wirecode ? WIRECODE : " " NAME ": ");        \
        s += strlen(s);  \
        for (int i = 0; i <= last_set; ++i) { \
            if (i > 0) { sprintf(s, ","); s += strlen(s); }      \
            if (AMY_IS_SET(e->FIELD[i])) {        \
                sprintf(s, "%.3f", e->FIELD[i]); \
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
        sprintf(s, "%s", wirecode ? WIRECODE : " " NAME ": ");       \
        s += strlen(s);  \
        for (int i = 0; i <= last_set; ++i) { \
            if (i > 0) { sprintf(s, ","); s += strlen(s); }        \
            if (AMY_IS_SET(e->FIELD[i])) { \
                sprintf(s, "%" PRId32, (int32_t)e->FIELD[i]); \
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
        sprintf(s, "%s", wirecode ? WIRECODE : " " NAME ": ");        \
        s += strlen(s);   \
        for (int i = 0; i <= last_set; ++i) {       \
            if (i > 0) { sprintf(s, ","); s += strlen(s); }        \
            if (AMY_IS_SET(e->TFIELD[i])) {                        \
                sprintf(s, "%" PRIu32, e->TFIELD[i]); \
                s += strlen(s);  \
            }    \
            sprintf(s, ",");                   \
            s += strlen(s);    \
            if (AMY_IS_SET(e->VFIELD[i])) {       \
                sprintf(s, "%.3f", e->VFIELD[i]); \
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
            sprintf(s, "%s", wirecode ? WIRECODE : " " NAME ": "); \
            s += strlen(s); \
            for (int j = 0; j <= last_one; ++j) {  \
                if (AMY_IS_SET(vals[j])) { \
                    sprintf(s, "%.3f", vals[j]);   \
                    s += strlen(s); \
                } \
                if (j < last_one) { \
                    sprintf(s, ","); \
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
        sprintf(s, "amy_event(time=%" PRIu32 ", osc=%" PRIu16 "): ", e->time, e->osc);
        s += strlen(s);
    } else {
        if (AMY_IS_SET(e->time)) { sprintf(s, "t%" PRIu32, (int32_t)e->time); s += strlen(s); }
        if (AMY_IS_SET(e->osc)) { sprintf(s, "v%" PRIu16, (int16_t)e->osc); s += strlen(s); }
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
    _EPRINT_F(volume, "volume", "V");  // NOT osc-dep
    _EPRINT_F(pitch_bend, "pitch_bend", "s");  // NOT osc-dep
    _EPRINT_F(tempo, "tempo", "j");  // NOT osc-dep
    _EPRINT_I(latency_ms, "latency_ms", "N");  // NOT osc-dep
    _EPRINT_F(ratio, "ratio", "I");
    _EPRINT_F(resonance, "resonance", "R");
    _EPRINT_I(portamento_ms, "portamento_ms", "m");
    _EPRINT_I(chained_osc, "chained_osc", "c");
    _EPRINT_I(mod_source, "mod_source", "L");
    _EPRINT_I(algorithm, "algorithm", "o");
    _EPRINT_I(filter_type, "filter_type", "G");
    _EPRINT_I_SEQ(bp_is_set, "bp_is_set", MAX_BREAKPOINT_SETS, "??");
    // Convert these two at least to vectors of ints, save several hundred bytes
    _EPRINT_I_SEQ(algo_source, "algo_source", MAX_ALGO_OPS, "O");
    _EPRINT_I_SEQ(voices, "voices", MAX_VOICES_PER_INSTRUMENT, "r");
    _EPRINT_BP(eg0_times, eg0_values, "eg0", "A");
    _EPRINT_BP(eg1_times, eg1_values, "eg1", "B");
    _EPRINT_I(eg_type[0], "eg_type[0]", "T");
    _EPRINT_I(eg_type[1], "eg_type[1]", "X");
    // Instrument-layer values.
    _EPRINT_I(synth, "synth", "i");
    _EPRINT_I(synth_flags, "synth_flags", "if");  // Special flags to set when defining instruments.
    _EPRINT_I_SEQ(cv_gate, "cv_gate", 2, "ig");  // CV channels to use for gate and note
    _EPRINT_I(synth_delay_ms, "synth_delay_ms", "id");  // Extra delay added to synth note-ons to allow decay on voice-stealing.
    _EPRINT_I(to_synth, "to_synth", "it");  // For moving setup between synth numbers.
    _EPRINT_I(grab_midi_notes, "grab_midi_notes", "im");  // To enable/disable automatic MIDI note-on/off generating note-on/off.
    _EPRINT_I(pedal, "pedal", "ip");  // MIDI pedal value.
    _EPRINT_I(num_voices, "num_voices", "iv");
    _EPRINT_I(oscs_per_voice, "oscs_per_voice", "in");
    _EPRINT_I_SEQ(sequence, "sequence", 3, "H"); // tick, period, tag
    //
    //_EPRINT_I(status, "status");
    _EPRINT_I(note_source, "note_source", "??");  // .. to mark note on/offs that come from MIDI so we don't send them back out again.
    _EPRINT_I(reset_osc, "reset_osc", "S");
    // Global effects
    _EPRINT_VALS_5(e->eq_l, e->eq_m, e->eq_h, AMY_UNSET_FLOAT, AMY_UNSET_FLOAT, "eq_{l,m,h}", "x");
    _EPRINT_VALS_5(e->echo_level, e->echo_delay_ms, e->echo_max_delay_ms, e->echo_feedback, e->echo_filter_coef, "echo_{level,delay,max,fb,filt}", "M");
    _EPRINT_VALS_5(e->chorus_level, e->chorus_max_delay, e->chorus_lfo_freq, e->chorus_depth, AMY_UNSET_FLOAT, "chorus_{level,delay,lfo,depth}", "k");
    _EPRINT_VALS_5(e->reverb_level, e->reverb_liveness, e->reverb_damping, e->reverb_xover_hz, AMY_UNSET_FLOAT, "reverb_{level,live,damp,xover}", "h");

    if (wirecode && (s - s_entry) > 0) { sprintf(s, "Z"); s += strlen(s); }

    assert( ((size_t)(s - s_entry)) < len);  // if we corrupted memory, at least we'll abort.
    return s - s_entry;
}


#define _CASE_I(FIELD, PARAM) case PARAM: event->FIELD = queue->data.i; break;
#define _CASE_F(FIELD, PARAM) case PARAM: event->FIELD = queue->data.f; break;
#define _CASE_LOG(FIELD, PARAM) case PARAM: event->FIELD = exp2f(queue->data.f); break;
#define _TEST_COEFS(FIELD, PARAM)  \
    for (int i = 0; i < NUM_COMBO_COEFS; ++i) {                          \
        if ((int)queue->param == (int)PARAM + i) event->FIELD[i] = queue->data.f; \
    } \
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
      _CASE_F(volume, VOLUME)
      _CASE_F(pitch_bend, PITCH_BEND)
      _CASE_I(latency_ms, LATENCY)
      _CASE_F(tempo, TEMPO)
      _CASE_LOG(ratio, RATIO)
      _CASE_F(resonance, RESONANCE)
      _CASE_I(portamento_ms, PORTAMENTO)
      _CASE_I(chained_osc, CHAINED_OSC)
      _CASE_I(reset_osc, RESET_OSC)
      _CASE_I(mod_source, MOD_SOURCE)
      _CASE_I(note_source, NOTE_SOURCE)
      _CASE_I(filter_type, FILTER_TYPE)
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

void parse_patch_string_to_queue(char *message, int base_osc, struct delta **queue, uint32_t time);

void *yield_patch_events(uint16_t patch_number, struct amy_event *event, void *state) {
    // Return a sequence of events defining a patch (specified by number).
    // state = NULL on first call and it returns state to be passed on next call.  Returns NULL when event sequence is finished.
    amy_clear_event(event);
    struct delta *queue = (struct delta *)state;
    if (queue == NULL) {
      // First call, initialize deltas queue.
      if (patch_number < _PATCHES_FIRST_USER_PATCH) {
	// This grabs a new set of deltas from the pool, when are they returned?
	parse_patch_string_to_queue((char *)patch_commands[patch_number], 0, &queue, 0);
      } else {
	  int32_t patch_index = patch_number - _PATCHES_FIRST_USER_PATCH;
	  if (patch_index < 0 || patch_index >= (int32_t)max_num_memory_patches) {
		      fprintf(stderr, "patch_number %" PRIu16 " is out of range (%" PRId32 " .. %" PRId32 ")\n",
			      patch_number, (int32_t)_PATCHES_FIRST_USER_PATCH,
			      (int32_t)(_PATCHES_FIRST_USER_PATCH + (int32_t)max_num_memory_patches));
	      return NULL;
	  }
	  queue = memory_patch_deltas[patch_index];
      }
    }
    struct delta *queue_on_entry = queue;
    /* Loop down the queue emitting events as needed. */
    queue = deltas_to_event(queue, event);

    if (patch_number < _PATCHES_FIRST_USER_PATCH) {
      // We allocated this queue, take care of releasing deltas we're finished with.
      while (queue_on_entry != queue) {
	struct delta *doomed = queue_on_entry;
	queue_on_entry = doomed->next;
	delta_release(doomed);
      }
    }

    return (void *)queue;
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
    EVENT_FROM_OSC_MAPPED(logratio, ratio, exp2f);
    EVENT_FROM_OSC(resonance);
    EVENT_FROM_OSC_MAPPED(portamento_alpha, portamento_ms, alpha_to_portamento_ms);
    EVENT_FROM_OSC_BASEOSC(chained_osc);
    EVENT_FROM_OSC_BASEOSC(mod_source);
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

void set_event_for_global_fx(amy_event *event, struct state *state) {
    // Always emit all FX fields so saved patches are fully self-describing.
    // Volume
    event->volume = state->volume;
    // EQ
    event->eq_l = lin_to_db(S2F(state->eq[0]));
    event->eq_m = lin_to_db(S2F(state->eq[1]));
    event->eq_h = lin_to_db(S2F(state->eq[2]));
    // Reverb
    event->reverb_level = S2F(state->reverb.level);
    event->reverb_liveness = state->reverb.liveness;
    event->reverb_damping = state->reverb.damping;
    event->reverb_xover_hz = state->reverb.xover_hz;
    // Chorus
    event->chorus_level = S2F(state->chorus.level);
    event->chorus_max_delay = state->chorus.max_delay;
    event->chorus_lfo_freq = state->chorus.lfo_freq;
    event->chorus_depth = state->chorus.depth;
    // Echo
    event->echo_level = S2F(state->echo.level);
    event->echo_delay_ms = state->echo.delay_samples * 1000.f / AMY_SAMPLE_RATE;
    if (state->echo.max_delay_samples != 65536)
        event->echo_max_delay_ms = state->echo.max_delay_samples * 1000.f / AMY_SAMPLE_RATE;
    event->echo_feedback = S2F(state->echo.feedback);
    event->echo_filter_coef = S2F(state->echo.filter_coef);
}


void *yield_synth_events(uint8_t synth, struct amy_event *event, bool include_fx, void *state) {
    // Return a sequence of events defining a synth.
    // state = NULL on first call and it returns state to be passed on next call.  Returns NULL when event sequence is finished.
   // Find oscs for synth.
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    int num_voices = instrument_get_num_voices(synth, voices);
    if (num_voices < 1) {
        fprintf(stderr, "yield_synth_events: synth %" PRId32" has no voices.\n", (int32_t)synth);
        return NULL;  // instrument not allocated.
    }
    uint32_t flags = instrument_get_flags(synth);
    uint16_t cv_gate = 0;
    uint16_t cv_note = 0;
    instrument_get_cv_gate(synth, &cv_gate, &cv_note);
    uint16_t voice = voices[0];
    uint16_t base_osc = voice_to_base_osc[voice];
    int num_oscs = 0;
    while(osc_to_voice[base_osc + num_oscs] == voice) ++num_oscs;
    // The "state" indicates which osc within the voice we're going to report for.
    int state_val = (intptr_t)state;
    //fprintf(stderr, "yield_synth_events(%d) voice=%d num_oscs=%d state_val=%d\n", synth, voice, num_oscs, (int)state_val);
    amy_clear_event(event);
    int first_osc_state_val = 0;
    int last_osc_state_val = num_oscs;
    if (flags != 0 || cv_note != 0) {
        ++first_osc_state_val;
        ++last_osc_state_val;
        if (state_val == 0) {
            event->synth_flags = flags;
            event->cv_gate[0] = cv_gate;
            event->cv_gate[1] = cv_note;
        }
    }
    if (state_val >= first_osc_state_val && state_val < last_osc_state_val) {
        event->osc = state_val - first_osc_state_val;
        //fprintf(stderr, "2 base_osc %d, event->osc %d, state_val %d first_osc_state_val %d last_osc_state_val %d\n",
        //    base_osc, event->osc, state_val, first_osc_state_val, last_osc_state_val);
        set_event_for_osc(base_osc, event->osc, event);
    } else if (include_fx && (state_val == last_osc_state_val)) {
        // optional final event contains the global settings (volume, eq, chorus, echo, reverb).
        set_event_for_global_fx(event, &amy_global);
    }
    ++state_val;
    if (state_val == last_osc_state_val + (include_fx ? 1 : 0))  state_val = 0;  // Indicate this is the final event.
    return (void *)((intptr_t)state_val);
}

#define STATE_START_OF_MIDI 1024
void *yield_synth_commands(uint8_t synth, char *s, size_t len, bool include_fx, void *state) {
    // Generator to return multiple wirecode strings to reconfigure a synth.
    int state_val = (intptr_t)state;
    //fprintf(stderr, "yield_synth_commands: synth %d state %d\n", synth, state_val);
    s[0] = '\0';  // By default, return an empty string.
    if (state_val < STATE_START_OF_MIDI) {
        amy_event event = amy_default_event();
        state_val = (intptr_t)yield_synth_events(synth, &event, include_fx, (void *)(intptr_t)state_val);
        sprint_event(&event, s, len, /* wirecode= */ true);
        if (state_val == 0) {
            // Push the state machine on to the MIDI codes
            state_val = STATE_START_OF_MIDI;
        }
    } else {
        // MIDI CC part
        bool found = false;
        for (int next_midi_code = state_val - STATE_START_OF_MIDI; next_midi_code < 128; ++next_midi_code) {
            if (midi_fetch_control_code_command(synth, next_midi_code, s, len) == true) {
                state_val = STATE_START_OF_MIDI + next_midi_code + 1;
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


void parse_patch_string_to_queue(char *message, int base_osc, struct delta **queue, uint32_t time) {
    // Work though the patch string and send to voices.
    // Now actually initialize the newly-allocated osc blocks with the patch
    uint16_t start = 0;
    //fprintf(stderr, "parse_patch_string: message %s\n", message);
    while(strlen(message + start)) {
      amy_event patch_event = amy_default_event();
      int num_used = amy_parse_message(message + start, strlen(message + start), &patch_event);
      //{
      //  char sub_message[256];
      //  strncpy(sub_message, message + start, num_used);
      //  sub_message[num_used]= 0;
      //  fprintf(stderr, "parse_patch_string: sub_message %s\n", sub_message);
      //}
      start += num_used;
      amy_process_event(&patch_event);
      patch_event.time = time;
      if(patch_event.status == EVENT_SCHEDULED) {
	amy_event_to_deltas_queue(&patch_event, base_osc, queue);
      }
    }
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
    if (!AMY_IS_SET(e->patch_number)) {
        // We need to allocate a new number.
        e->patch_number = next_user_patch_index + _PATCHES_FIRST_USER_PATCH;
        // next_user_patch_index is updated as needed at the bottom of the function (so it can reflect user-defined numbers too).
        //fprintf(stderr, "store_patch: auto-assigning patch number %d for '%s'\n", e->patch_number, patch_string);
    }
    int patch_index = (int)e->patch_number - _PATCHES_FIRST_USER_PATCH;
    if (patch_index < 0 || patch_index >= (int)max_num_memory_patches) {
        fprintf(stderr, "patch number %" PRId32 " is out of range (%" PRId32 " .. %" PRId32 ")\n",
                (int32_t)(patch_index + _PATCHES_FIRST_USER_PATCH),
                (int32_t)_PATCHES_FIRST_USER_PATCH,
                (int32_t)(_PATCHES_FIRST_USER_PATCH + (int32_t)max_num_memory_patches));
        return;
    }
    if (patch_index >= next_user_patch_index)  next_user_patch_index = patch_index + 1;
    // Store the patch as deltas and  find out how many oscs this message uses
    parse_patch_string_to_queue(patch_string, 0, &memory_patch_deltas[patch_index], e->time);
    update_num_oscs_for_patch_number(patch_index + _PATCHES_FIRST_USER_PATCH);
    //fprintf(stderr, "store_patch: patch %d max_osc %d patch %s #deltas %d (e->num_vx=%d)\n", patch_index, max_osc, patch_string, delta_list_len(memory_patch_deltas[patch_index]), e->num_voices);
}

extern int32_t parse_list_uint16_t(char *message, uint16_t *vals, int32_t max_num_vals, uint16_t skipped_val);


// This code was originally in midi.c, but putting it here allows endogenous use of MIDI drums.
// Drum kit - copied from tulip/shared/py/patches.py
// Drumkit is [base_midi_note, name, general_midi_note]

struct pcm_sample_info {
    int8_t pcm_preset_number;
    int8_t base_midi_note;
};

#define AMY_MIDI_DRUMS_LOWEST_NOTE 35
#define AMY_MIDI_DRUMS_HIGHEST_NOTE 81

// drumkit[midi_note - AMY_MIDI_DRUMS_LOWEST_NOTE] == {pcm_patch_number, base_midi_note}

// PCM presets available (from pcm_tiny.h):
//  [0]  808-MARACA    root=89
//  [1]  808-KIK 4     root=39
//  [2]  808-SNR 4     root=45
//  [3]  808-SNR 7     root=52
//  [4]  808-SNR 10    root=51
//  [5]  808-SNR 12    root=41
//  [6]  808-C-HAT1    root=53
//  [7]  808-O-HAT1    root=56
//  [8]  808-LTOM M    root=61
//  [9]  808-DRYCLP    root=94
//  [10] 808-CWBELL    root=69
struct pcm_sample_info drumkit[AMY_MIDI_DRUMS_HIGHEST_NOTE - AMY_MIDI_DRUMS_LOWEST_NOTE + 1] = {
    {1, 39},   // 35 Acoustic Bass Drum -> 808-KIK
    {1, 39},   // 36 Bass Drum 1        -> 808-KIK
    {4, 51},   // 37 Side Stick         -> 808-SNR 10 (rimshot-like)
    {2, 45},   // 38 Acoustic Snare     -> 808-SNR 4
    {9, 94},   // 39 Hand Clap          -> 808-DRYCLP
    {5, 41},   // 40 Electric Snare     -> 808-SNR 12
    {8, 56},   // 41 Low Floor Tom      -> 808-LTOM (pitched down)
    {6, 53},   // 42 Closed Hi Hat      -> 808-C-HAT1
    {8, 61},   // 43 High Floor Tom     -> 808-LTOM (root)
    {7, 61},   // 44 Pedal Hi-Hat       -> 808-O-HAT1 (short)
    {8, 56},   // 45 Low Tom            -> 808-LTOM (pitched down)
    {7, 56},   // 46 Open Hi-Hat        -> 808-O-HAT1
    {8, 63},   // 47 Low-Mid Tom        -> 808-LTOM (slightly up)
    {8, 68},   // 48 Hi Mid Tom         -> 808-LTOM (pitched up)
    {7, 46},   // 49 Crash Cymbal 1     -> 808-O-HAT1 (pitched down for wash)
    {8, 73},   // 50 High Tom           -> 808-LTOM (pitched up high)
    {7, 51},   // 51 Ride Cymbal 1      -> 808-O-HAT1 (pitched down)
    {7, 48},   // 52 Chinese Cymbal     -> 808-O-HAT1 (pitched down)
    {6, 47},   // 53 Ride Bell          -> 808-C-HAT1 (pitched down, bell-like)
    {0, 79},   // 54 Tambourine         -> 808-MARACA (pitched down)
    {7, 46},   // 55 Splash Cymbal      -> 808-O-HAT1 (pitched down)
    {10, 69},  // 56 Cowbell            -> 808-CWBELL
    {7, 48},   // 57 Crash Cymbal 2     -> 808-O-HAT1 (pitched down)
    {-1, -1},  // 58 Vibraslap
    {7, 53},   // 59 Ride Cymbal 2      -> 808-O-HAT1 (pitched down)
    {-1, -1},  // 60 Hi Bongo
    {-1, -1},  // 61 Low Bongo
    {-1, -1},  // 62 Mute Hi Conga
    {-1, -1},  // 63 Open Hi Conga
    {-1, -1},  // 64 Low Conga
    {8, 73},   // 65 High Timbale       -> 808-LTOM (pitched up)
    {8, 63},   // 66 Low Timbale        -> 808-LTOM (slightly up)
    {10, 76},  // 67 High Agogo         -> 808-CWBELL (pitched up)
    {10, 64},  // 68 Low Agogo          -> 808-CWBELL (pitched down)
    {0, 79},   // 69 Cabasa             -> 808-MARACA (pitched down)
    {0, 89},   // 70 Maracas            -> 808-MARACA (root)
    {-1, -1},  // 71 Short Whistle
    {-1, -1},  // 72 Long Whistle
    {-1, -1},  // 73 Short Guiro
    {-1, -1},  // 74 Long Guiro
    {-1, -1},  // 75 Claves
    {10, 76},  // 76 Hi Wood Block      -> 808-CWBELL (pitched up)
    {10, 64},  // 77 Low Wood Block     -> 808-CWBELL (pitched down)
    {-1, -1},  // 78 Mute Cuica
    {-1, -1},  // 79 Open Cuica
    {-1, -1},  // 80 Mute Triangle
    {-1, -1},  // 81 Open Triangle
};


bool setup_drum_event(amy_event *e, uint8_t note) {
  // Special-case processing to convert MIDI drum notes into PCM patch events.
  bool forward_note = false;
  if (note >= AMY_MIDI_DRUMS_LOWEST_NOTE && note <= AMY_MIDI_DRUMS_HIGHEST_NOTE) {
      struct pcm_sample_info s = drumkit[note - AMY_MIDI_DRUMS_LOWEST_NOTE];
      if (s.pcm_preset_number != -1) {
          e->wave = PCM;
          e->preset = s.pcm_preset_number;
          e->midi_note = s.base_midi_note;
          forward_note = true;
      }
  }
  return forward_note;
}

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
        if (synth_flags & _SYNTH_FLAGS_MIDI_DRUMS) {
            if (!setup_drum_event(e, note))
                return 0;   // It's not a MIDI drum event we can emulate, just drop the event.
        }
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


uint8_t patches_voices_for_event(amy_event *e, uint16_t voices[]) {
    // Convert an event that may specify a synth into a number of specific voices.
    uint8_t num_voices = 0;
    uint32_t synth_flags = 0;
    if (!AMY_IS_SET(e->synth)) {
        // No instrument, just directly naming the voices.
        num_voices = copy_voices(e->voices, voices);
    } else {  // We have an instrument specified - decide which of its voices are actually to be used.
        // It's a mistake to specify both synth (instrument) and voices, warn user we're ignoring voices.
        // (except in the afterlife of a load_patch event, which will most likely be empty anyway).
        if (AMY_IS_SET(e->voices[0]) && !AMY_IS_SET(e->patch_number)) {
            fprintf(stderr, "You specified both synth %" PRId32 " and voices %" PRIu16 "...  Synth implies voices, ignoring voices.\n",
                    (int32_t)e->synth, e->voices[0]);
        }
        synth_flags = instrument_get_flags(e->synth);
        if (AMY_IS_SET(e->to_synth)) {
            // This involves moving the instrument number.
            instrument_change_number(e->synth, e->to_synth);
            e->synth = e->to_synth;
            AMY_UNSET(e->to_synth);
            // Then continue handling any other args.
        }
        if (AMY_IS_SET(e->synth_delay_ms)) {
            // Set the synth noteon delay.
            instrument_set_noteon_delay_ms(e->synth, e->synth_delay_ms);
        }
        if (AMY_IS_SET(e->grab_midi_notes)) {
            // Set the grab_midi state.
            instrument_set_grab_midi_notes(e->synth, e->grab_midi_notes);
        }
        if (AMY_IS_SET(e->synth_flags)) {
            instrument_set_flags(e->synth, e->synth_flags);
        }
        if (AMY_IS_SET(e->cv_gate[0])) {
            instrument_set_cv_gate(e->synth, e->cv_gate[0], e->cv_gate[1]);
        }
        if (AMY_IS_SET(e->grab_midi_notes)) {
            // Set the grab_midi state.
            instrument_set_grab_midi_notes(e->synth, e->grab_midi_notes);
        }
        if (AMY_IS_SET(e->pedal)) {
            // Pedal events are a special case
            bool sustain = (e->pedal != 0);
            if (synth_flags & _SYNTH_FLAGS_NEGATE_PEDAL) {
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
                e->time = playback_time;
                //fprintf(stderr, "synth %d note %d delay %d time %d\n", e->synth, (int)roundf(e->midi_note), instrument_noteon_delay_ms(e->synth), e->time);
            }
        } else {
            // Not note on/off, treat the synth as a shorthand for *all* the voices.
            num_voices = instrument_get_num_voices(e->synth, voices);
        }
        if (AMY_IS_SET(e->velocity) && e->velocity == 0 && (synth_flags & _SYNTH_FLAGS_IGNORE_NOTE_OFFS))
            return 0;  // Ignore the note off, as requested.
    }
    return num_voices;
}

// This is called when i get an event with voices (or an instrument) in it.
// If the event also has synth and patch_number specified (a "load patch"), those are handled before this is called.
// So i know that the patch / voice alloc already exists and the patch has already been set!
void patches_event_has_voices(amy_event *e, struct delta **queue) {
    peek_stack("has_voices");
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    uint8_t num_voices = patches_voices_for_event(e, voices);
    if (num_voices == 0) {
        // No voices to process, somehow event is to be ignored.
        return;
    }
    // Clear out the instrument, voices, patch from the event. If we didn't, we'd keep calling this over and over
    AMY_UNSET(e->voices[0]);
    AMY_UNSET(e->patch_number);
    int32_t instrument = e->synth;
    AMY_UNSET(e->synth);
    // for each voice, send the event to the base osc (+ e->osc if given)
    for(uint8_t i=0;i<num_voices;i++) {
        if(AMY_IS_SET(voice_to_base_osc[voices[i]])) {
            uint16_t target_osc = voice_to_base_osc[voices[i]];
            amy_event_to_deltas_queue(e, target_osc, queue);
	    //fprintf(stderr, "patches: synth %d voice %d osc %d wav %d note %d vel %d\n", instrument, voices[i], target_osc, e->wave, (int)e->midi_note, (int)(127.f * e->velocity));
        }
    }
    // Restore the instrument in case this event is re-used.
    e->synth = instrument;
}

void release_voice_oscs(int32_t voice) {
    if(AMY_IS_SET(voice_to_base_osc[voice])) {
        //fprintf(stderr, "Already set voice %d, removing it\n", voice);
        // Remove the oscs for this old voice
        for(uint16_t i=0;i<AMY_OSCS;i++) {
            if(osc_to_voice[i]==voice) {
                //fprintf(stderr, "Already set voice %d osc %d, removing it\n", voices[v], i);
                AMY_UNSET(osc_to_voice[i]);
                // Make sure the osc is cleared.
                reset_osc(i);
            }
        }
        AMY_UNSET(voice_to_base_osc[voice]);
    }
}

void parse_patch_string_to_queue(char *message, int base_osc, struct delta **queue, uint32_t time);

uint8_t patches_voices_for_load_synth(amy_event *e, uint16_t voices[]) {
    // When load_patch specifies a synth, convert that into voices.
    // e->synth is assumed to be set.
    int num_voices = 0;
    // If the instrument is alread initialized, copy the voice numbers.
    num_voices = instrument_get_num_voices(e->synth, voices);
    if (AMY_IS_SET(e->num_voices) && e->num_voices != num_voices) {
        // If we did already have voice oscs, release them.
        for (int32_t i = 0; i < num_voices; ++i) {
            release_voice_oscs(voices[i]);
        }
        num_voices = 0;
        // Find avaliable voices with a single pass through voice_to_base_osc.
        uint32_t v = 0;
        for (int32_t i = 0; i < e->num_voices; ++i) {
            while (v < amy_global.config.max_voices) {
                if (AMY_IS_UNSET(voice_to_base_osc[v])) break;
                ++v;
            }
            if (v == amy_global.config.max_voices)  {
                fprintf(stderr, "ran out of voices allocating %" PRId32 " voices to synth %" PRId32 ", ignoring.",
                        (int32_t)e->num_voices, (int32_t)e->synth);
                patches_debug();
                return 0;
            }
            voices[i] = v;
            ++v;
            ++num_voices;
        }
        // Was this deleting the instrument?  i.e. was e->num_voices set but setting the num_voices to zero?
        if (num_voices == 0) {
            instrument_release(e->synth);
            // Delete the instrument number so we don't forward the 'rest' of the event to it.
            AMY_UNSET(e->synth);
            // Clear all the midi control code mappings.
            midi_clear_channel_mappings(e->synth);
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
    // Given an event with a synth (instrument) or voice spec, set up a synth.
    // Common case is to call with a patch to load, but can also have just
    // a oscs_per_voice value.
    // (also called if instrument & num_voices even if no patch specified, to change #voices).
    // This means to set/reset the voices and load the messages (from ROM or memory) and set them.
    peek_stack("load_patch");
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    uint8_t num_voices = 0;
    uint16_t oscs_per_voice = 0;
    uint16_t patch_number = e->patch_number;   // Need to match type of e->patch_number so AMY_IS_UNSET(patch_number) will work.
    //fprintf(stderr, "load_patch synth %d patch_number %d num_voices %d oscs_per_voice %d\n", e->synth, e->patch_number, e->num_voices, e->oscs_per_voice);
    if (AMY_IS_SET(e->synth)) {
        num_voices = patches_voices_for_load_synth(e, voices);
    } else if (AMY_IS_SET(e->voices[0])) {
        num_voices = copy_voices(e->voices, voices);
    }
    if (num_voices == 0) {
        if (AMY_IS_UNSET(e->num_voices)) {
            // Print a warning unless we deliberately set the voices to zero to release the synth.
            fprintf(stderr, "synth %" PRId32 ": no voices selected, ignored (e->num_voices %" PRId32 " e->voices [0] %" PRIu16 "...)\n",
                    (int32_t)e->synth, (int32_t)e->num_voices, e->voices[0]);
        }
        return;
    } else {
        // Reset every osc belonging to each voice so stale state doesn't persist
        // before we reallocate and reinitialize them below.
        for (uint8_t v = 0; v < num_voices; v++) {
            if (AMY_IS_SET(voice_to_base_osc[voices[v]])) {
                uint16_t base = voice_to_base_osc[voices[v]];
                for (uint16_t j = 0; j < AMY_OSCS - base; j++) {
                    if (osc_to_voice[base + j] != voices[v]) break;
                    reset_osc(base + j);
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
    } else {
        if (AMY_IS_UNSET(patch_number))
            patch_number = instrument_get_patch_number(e->synth);
        if(patch_number < _PATCHES_FIRST_USER_PATCH) {
            // Built-in patch
            message = (char*)patch_commands[patch_number];
            oscs_per_voice = patch_oscs[patch_number];
        } else {
            // User-defined patch
            int32_t patch_index = patch_number - _PATCHES_FIRST_USER_PATCH;
            oscs_per_voice = memory_patch_oscs[patch_index];
            if(oscs_per_voice > 0){
                deltas = memory_patch_deltas[patch_index];
            } else {
                fprintf(stderr, "patch_number %" PRIu16 " has %" PRIu16 " num_deltas %" PRIi32 " (synth %" PRId32 " num_voices %" PRId32 "), ignored\n",
                        patch_number, oscs_per_voice, delta_list_len(memory_patch_deltas[patch_index]),
                        (int32_t)e->synth, (int32_t)e->num_voices);
                return;
            }
        }
    }

    for(uint8_t v=0;v<num_voices;v++)  {
        // Release all the oscs of any voices we're re-using before we start re-allocating oscs.
        release_voice_oscs(voices[v]);
    }

    for(uint8_t v=0;v<num_voices;v++)  {
        // Find the first osc with oscs_per_voice free oscs.
        uint8_t good = 0;

        uint16_t osc_start = (AMY_OSCS/2);

        #ifdef TULIP
        // On tulip. core 0 (oscs0-60) is shared with the display, who does GDMA a lot. so we favor core1 (oscs60-120)
        if(voices[v]%3==2) osc_start = 0;
        #else
        if(voices[v]%2==1) osc_start = 0;
        #endif

        for(uint16_t i=0;i<AMY_OSCS;i++) {
            uint16_t osc = (osc_start + i) % AMY_OSCS;
            if(AMY_IS_UNSET(osc_to_voice[osc])) {
                // Are there num_voices x oscs_per_voice free oscs after this one?
                good = 1;
                for(uint16_t j=0; j < oscs_per_voice; j++) {
                    good = good & (AMY_IS_UNSET(osc_to_voice[osc + j]));
                }
                if(good) {
                    //fprintf(stderr, "found %d consecutive oscs starting at %d for voice %d\n", patch_oscs[patch_number], osc, voices[v]);
                    //fprintf(stderr, "setting base osc for voice %d to %d\n", voices[v], osc);
                    voice_to_base_osc[voices[v]] = osc; 
                    for(uint16_t j=0; j < oscs_per_voice; j++) {
                        //fprintf(stderr, "setting osc %d for voice %d to amy osc %d\n", j, voices[v], osc+j);
                        osc_to_voice[osc+j] = voices[v];
                        //reset_osc(osc+j);
                        // Use event mechanism to post osc resets, to ensure they are done in sequence.
                        // This was important because for testing the patch reassignment, we were running the default
                        // osc setup, then changing the patch, which reset the oscs here, but then the osc config
                        // from the default setup was applied *after* the reset, so the osc state was not reset.
                        //amy_event reset_event = amy_default_event();
                        //reset_event.reset_osc = osc + j;
                        //amy_event_to_deltas_queue(&reset_event, 0, &amy_global.delta_queue);
                        // That's a lot of stack usage to add a single delta.  Let's cut to the chase.
                        struct delta d = {
                            .time = 0,
                            .osc = 0,
                            .param = RESET_OSC,
                            .data.i = osc + j,
                            .next = NULL,
                        };
                        add_delta_to_queue(&d, &amy_global.delta_queue);
                    }
                    // exit the loop
                    i = AMY_OSCS + 1;
                }
            }
        }
        if(!good) {
            fprintf(stderr, "cannot find %" PRIu16 " oscs for patch %" PRIu16 " for voice %" PRIu16 ". not setting this voice\n",
                    oscs_per_voice, patch_number, voices[v]);
        }
    }  // end of loop setting up voice_to_base_osc for all voices[v]

    // Now actually initialize the newly-allocated osc blocks with the patch
    for(uint8_t v = 0; v < num_voices; v++) {
        if(AMY_IS_SET(voice_to_base_osc[voices[v]])) {
            if (deltas) {
                add_deltas_to_queue_with_baseosc(deltas, voice_to_base_osc[voices[v]], &amy_global.delta_queue, e->time);
            } else if (message) {
                parse_patch_string_to_queue(message, voice_to_base_osc[voices[v]], &amy_global.delta_queue, e->time);
            }
            // Or maybe there's no deltas and no message, in which case we just set oscs_per_voice, waiting for config.
        }
    }

    // Finally, store as an instrument if instrument number is specified.
    if (AMY_IS_SET(e->synth)) {
        uint32_t flags = 0;
        if (AMY_IS_SET(e->synth_flags)) flags = e->synth_flags;
        instrument_add_new(e->synth, num_voices, voices, patch_number, oscs_per_voice, flags);
    }

}
