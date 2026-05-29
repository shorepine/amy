// cv_trigger.c
// Support generating events from Control Voltage input transitions.

#include "amy.h"

#define CV_TRIG_TRIGGERED 0
#define CV_TRIG_READY 1

typedef struct cv_trigger {
    struct cv_trigger *next;
    // Parameters
    uint8_t trigger_cv;  // Which CV input to use.
    float thresh_high;   // Trigger when voltage goes above this
    float thresh_low;    // Reset when voltage goes below this
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
    fprintf(stderr, "cv_trigger: cv %d thresh %.2f reset %.2f pitch %d scale %.2f offs %.2f state %d msg %s\n", cv_trig->trigger_cv, cv_trig->thresh_high, cv_trig->thresh_low, cv_trig->pitch_cv, cv_trig->pitch_scale, cv_trig->pitch_offset, cv_trig->state, cv_trig->message_template);
}

void cv_trigger_new(uint8_t trigger_cv, float thresh_high, float thresh_low, uint8_t pitch_cv, float pitch_scale, float pitch_offset, char *message_template) {
    cv_trigger_t *result = (cv_trigger_t *)malloc_caps(
        sizeof(cv_trigger_t) + strlen(message_template) + 1,
        amy_global.config.ram_caps_synth
    );
    result->message_template = ((char *)result) + sizeof(cv_trigger_t);
    strcpy(result->message_template, message_template);
    result->trigger_cv = trigger_cv;
    result->thresh_high = thresh_high;
    result->thresh_low = thresh_low;
    result->pitch_cv = pitch_cv;
    result->pitch_scale = pitch_scale;
    result->pitch_offset = pitch_offset;
    result->state = CV_TRIG_TRIGGERED;  // Wait for transition below thresh_low before arming
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

