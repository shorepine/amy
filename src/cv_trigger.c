// cv_trigger.c
// Support generating events from Control Voltage input transitions.

#include "amy.h"

// Gap between READY and TRIGGERED is how many frames to wait before triggering
#define CV_TRIG_READY 0
#define CV_TRIG_TRIGGERED 2

typedef struct cv_trigger {
    struct cv_trigger *next;
    // Parameters
    uint8_t trigger_cv;  // Which CV input to use.
    float thresh_trigger;  // Trigger when voltage passes this
    float thresh_reset;    // Reset when voltage passes this
    uint8_t pitch_cv;    // CV to use to generate pitch
    float pitch_scale;   // Multiply pitch voltage by this to get 1VPO note
    float pitch_offset;  // Add this to scaled pitch_cv to get 1VPO note
    char *message_template;  // Event to generate
    // State
    uint8_t state;       // CV_TRIG_* value
} cv_trigger_t;

// Root of linked list of configured triggers.
cv_trigger_t *cv_trigger_root = NULL;

void cv_trigger_print(cv_trigger_t *cv_trig) {
    fprintf(stderr, "cv_trigger: cv %d thresh %.2f reset %.2f pitch %d scale %.2f offs %.2f state %d msg %s\n", cv_trig->trigger_cv, cv_trig->thresh_trigger, cv_trig->thresh_reset, cv_trig->pitch_cv, cv_trig->pitch_scale, cv_trig->pitch_offset, cv_trig->state, cv_trig->message_template);
}

void cv_trigger_new(uint8_t trigger_cv, float thresh_trigger, float thresh_reset, uint8_t pitch_cv, float pitch_scale, float pitch_offset, char *message_template) {
    // Prevent references outside known CV index range.
    //if (trigger_cv < 0)  trigger_cv = 0;
    //if (pitch_cv < 0)  pitch_cv = 0;
    // Can't happen - CV args are uint.
    if (trigger_cv >= AMY_MAX_CV_IN)  trigger_cv = AMY_MAX_CV_IN - 1;
    if (AMY_IS_SET(pitch_cv) && (pitch_cv >= AMY_MAX_CV_IN))  pitch_cv = AMY_MAX_CV_IN - 1;
    // Construct a new linked list entry.
    cv_trigger_t *result = (cv_trigger_t *)malloc_caps(
        sizeof(cv_trigger_t) + strlen(message_template) + 1,
        amy_global.config.ram_caps_synth
    );
    result->message_template = ((char *)result) + sizeof(cv_trigger_t);
    strcpy(result->message_template, message_template);
    result->trigger_cv = trigger_cv;
    result->thresh_trigger = thresh_trigger;
    result->thresh_reset = thresh_reset;
    result->pitch_cv = pitch_cv;
    result->pitch_scale = pitch_scale;
    result->pitch_offset = pitch_offset;
    result->state = CV_TRIG_TRIGGERED;  // Wait for transition below thresh_reset before arming
    // Insert into linked list at head.
    result->next = cv_trigger_root;
    cv_trigger_root = result;
}

void cv_trigger_debug(void) {
    fprintf(stderr, "cv_trigger_debug:\n");
    cv_trigger_t *cv_trig =  cv_trigger_root;
    while(cv_trig) {
        cv_trigger_print(cv_trig);
        cv_trig = cv_trig->next;
    }
}

void cv_trigger_free(cv_trigger_t **p_cv_trig) {
    // Close up linked list.
    cv_trigger_t *doomed = *p_cv_trig;
    *p_cv_trig = doomed->next;
    // Return memory
    free(doomed);
}

void cv_trigger_init(void) {
    cv_trigger_root = NULL;
}

void cv_trigger_deinit(void) {
    cv_trigger_t **p_cv_trig = &cv_trigger_root;
    while (*p_cv_trig != NULL) {
        cv_trigger_free(p_cv_trig);
    }
}

void cv_trigger_clear_mappings(int gate_cv) {
    cv_trigger_t **p_cv_trig = &cv_trigger_root;
    while (*p_cv_trig != NULL) {
        if ( (*p_cv_trig)->trigger_cv == gate_cv)
            cv_trigger_free(p_cv_trig);
        else
            p_cv_trig = &((*p_cv_trig)->next);
    }
}

void cv_trigger_generate_events(float *cv_inputs) {
    // Check to see if triggers are run based on new vector of trigger values.
    cv_trigger_t *cv_trig =  cv_trigger_root;
    while(cv_trig) {
        float cv_val = cv_inputs[cv_trig->trigger_cv];
        bool rising = (cv_trig->thresh_trigger > cv_trig->thresh_reset);
        int polarity = rising ? 1 : -1;
        if ((polarity * cv_val) >= (polarity * cv_trig->thresh_trigger)) {
            if (cv_trig->state <= CV_TRIG_TRIGGERED) {
                ++cv_trig->state;
                if (cv_trig->state == CV_TRIG_TRIGGERED) {
                   // execute the event
                   float note = 0;
                   if (AMY_IS_SET(cv_trig->pitch_cv)) {
                       // resample the pitch CV
                       float cv_input = amy_global.config.amy_external_coef_hook(cv_trig->pitch_cv);
                       note = midi_note_for_logfreq(
                           cv_trig->pitch_offset
                           + cv_trig->pitch_scale * cv_input
                       );
                   }
                   char message[AMY_WIRE_COMMAND_LEN];
                   substitute_midi_special_values(message, cv_trig->message_template, 0, 0, note);
                   //fprintf(stderr, "update_external_cv_in: message %s\n", message);
                   amy_add_message(message);
                }
            }
        } else if ((polarity * cv_val) < (polarity * cv_trig->thresh_reset)) {
            if (cv_trig->state != CV_TRIG_READY) {
                // Reset
                cv_trig->state = CV_TRIG_READY;
                //fprintf(stderr, "update_external_cv_in: RESET message %s\n", cv_trig->message_template);
            }
        }
        cv_trig = cv_trig->next;
    }
}


// Main entry, called once per block, check if triggers are run.

float cv_inputs[AMY_MAX_CV_IN];

void update_external_cv_in(void) {
    // Update the CV inputs.
    if (amy_global.config.amy_external_coef_hook != NULL) {
        cv_inputs[0] = amy_global.config.amy_external_coef_hook(0);
        cv_inputs[1] = amy_global.config.amy_external_coef_hook(1);
    } else {
        #ifdef __EMSCRIPTEN__
        cv_inputs[0] = amy_web_cv_1;
        cv_inputs[1] = amy_web_cv_2;
        #else
        cv_inputs[0] = 0;
        cv_inputs[1] = 0;
        #endif
    }
    // Run triggers
    cv_trigger_generate_events(cv_inputs);
}

// To support testing, simulate CV input with a mod osc

// Static array mapping oscs to CV inputs.
int osc_for_cv[AMY_MAX_CV_IN];

void cv_from_osc_init(void) {
    for (int i = 0; i < AMY_MAX_CV_IN; ++i) {
        osc_for_cv[i] = -1;
    }
}

void cv_from_osc_deinit(void) {
}

// Hack to stop trying to evaluate external_coef_hook inside hold_and_modify
// leading to infinite recursion.
static bool _recursion_stop = false;

float cv_from_osc(uint16_t cv_channel) {
    float val = 0;
    if (!_recursion_stop && cv_channel < AMY_MAX_CV_IN) {
        int osc = osc_for_cv[cv_channel];
        if (osc >= 0) {
            _recursion_stop = true;
            hold_and_modify(osc);  // this is what envelope.c does for mod oscs, but causes an infinite loop because hold_and_modify evaluates external_cv inputs.
            _recursion_stop = false;
            val = S2F(compute_mod_value(osc));
        }
    }
    return val;
}

void set_cv_from_osc(int cv_channel, int osc) {
    // Mirror MOD_SOURCE setup from amy.c:config_chorus
    osc_for_cv[cv_channel] = osc;
    if (osc < 0)  return;  // syntax to unset cv_from_osc.
    ensure_osc_allocd(osc, NULL);
    synth[osc]->status = SYNTH_IS_MOD_SOURCE;
    // No longer record this osc in note_off state.
    AMY_UNSET(synth[osc]->note_off_clock);
    // Remove default amplitude dependence on velocity when an oscillator is made a modulator.
    synth[osc]->logfreq_coefs[COEF_NOTE] = 0;
    synth[osc]->logfreq_coefs[COEF_BEND] = 0;
    synth[osc]->amp_coefs[COEF_VEL] = 0;
    synth[osc]->amp_coefs[COEF_EG0] = 0;
    synth[osc]->note_on_clock = amy_global.total_blocks * AMY_BLOCK_SIZE;  // Need a note_on_clock to have envelope work correctly.. not that we care about envelope
    osc_note_on(osc, freq_of_logfreq(synth[osc]->logfreq_coefs[COEF_CONST]));
    // Add the CV retrieval hook.
    if (amy_global.config.amy_external_coef_hook != NULL && amy_global.config.amy_external_coef_hook != cv_from_osc)
        fprintf(stderr, "set_cv_from_osc: WARNING: overwriting existing amy_external_coef_hook\n");
    amy_global.config.amy_external_coef_hook = cv_from_osc;
}
