// patches.c
// baked in AMY string patches (Juno-6 & DX7 for now)

#include "amy.h"
#include "patches.h"

/* patch spec

amy.send(load_patch=1, voices="0,1,2,3") # load juno patch into 4 voices. internally figure out which oscs to use and update our internal map of oscs <-> voices
amy.send(load_patch=130, voices="4,5") # load dx7 patch into 2 voices.
amy.send(voices="0,1", vel=1, note=40) # send this note on to voices 0 and 1
amy.send(voices="2",vel=1, note=44) # different note on differnet voice
amy.send(voices="4", vel=1, note=50) # dx7 note on
amy.send(voices="4", osc=1, filter_freq="440,0,0,0,5") -- can address an osc offset if you give both

so basically, a voice is a lookup to a base_osc
if you get multiple in a message, you call the message N times across the base_oscs , that includes load_patch 
if you get a osc and a voice, you add the osc to the base_osc lookup and send the message 

*/

#define _PATCHES_FIRST_USER_PATCH 1024

#define MAX_VOICES 32
#define MEMORY_PATCHES 32
char * memory_patch[MEMORY_PATCHES];
uint16_t memory_patch_oscs[MEMORY_PATCHES];
uint8_t osc_to_voice[AMY_OSCS];
uint16_t voice_to_base_osc[MAX_VOICES];

void patches_debug() {
    for(uint8_t v=0;v<MAX_VOICES;v++) {
        if (AMY_IS_SET(voice_to_base_osc[v]))
            fprintf(stderr, "voice %d base osc %d\n", v, voice_to_base_osc[v]);
    }
    fprintf(stderr, "osc_to_voice:\n");
    for(uint16_t i=0;i<AMY_OSCS;) {
        uint16_t j = 0;
        fprintf(stderr, "%d: ", i);
        for (j=0; j < 16; ++j) {
            if ((i + j) >= AMY_OSCS)  break;
            fprintf(stderr, "%d ", osc_to_voice[i + j]);
        }
        i += j;
        fprintf(stderr, "\n");
    }
    for(uint8_t i=0;i<MEMORY_PATCHES;i++) {
        if(memory_patch_oscs[i])
            fprintf(stderr, "memory_patch %d oscs %d patch %s\n", i, memory_patch_oscs[i], memory_patch[i]);
    }
}

void all_notes_off() {
    for(uint16_t i=0;i<AMY_OSCS;i++) {
        if (AMY_IS_SET(osc_to_voice[i])) {
            if(synth[i].status == SYNTH_AUDIBLE) {
                synth[i].status = SYNTH_AUDIBLE_SUSPENDED;
            }
        }
    }
}

void patches_init() {
    for(uint8_t i=0;i<MEMORY_PATCHES;i++) {
        memory_patch_oscs[i]  = 0;
        memory_patch[i] = NULL;
    }    
}

void patches_reset() {
    for(uint8_t v=0;v<MAX_VOICES;v++) {
        AMY_UNSET(voice_to_base_osc[v]);
    }
    for(uint16_t i=0;i<AMY_OSCS;i++) {
        AMY_UNSET(osc_to_voice[i]);
    }
    for(uint8_t i=0;i<MEMORY_PATCHES;i++) {
        if(memory_patch_oscs[i] > 0) free(memory_patch[i]);
        memory_patch[i] = NULL;
        memory_patch_oscs[i] = 0; 
    }
}

void patches_store_patch(char * message) {
    // patch#,amy patch string
    // put it in ram
    int patch_number = atoi(message) - _PATCHES_FIRST_USER_PATCH;
    if (patch_number < 0 || patch_number >= MEMORY_PATCHES) {
        fprintf(stderr, "patch number in '%s' is out of range (%d .. %d)\n",
                message, _PATCHES_FIRST_USER_PATCH, _PATCHES_FIRST_USER_PATCH + MEMORY_PATCHES);
        return;
    }
    char * patch = message + strspn(message, ",0123456789"); // 5; // always 4 digit patch + ,
    // Now find out how many oscs this message uses
    uint16_t max_osc = 0;
    char sub_message[255];
    uint16_t start = 0;
    for(uint16_t i=0;i<strlen(patch);i++) {
        if(patch[i] == 'Z') {
            strncpy(sub_message, patch + start, i - start + 1);
            sub_message[i-start+1]= 0;
            struct event patch_event = amy_parse_message(sub_message);
            if(AMY_IS_SET(patch_event.osc) && patch_event.osc > max_osc)
                max_osc = patch_event.osc;
            start = i+1;
        }
    }
    if(memory_patch_oscs[patch_number] >0) { free(memory_patch[patch_number]); }
    memory_patch[patch_number] = malloc(strlen(patch)+1);
    memory_patch_oscs[patch_number] = max_osc + 1;
    strcpy(memory_patch[patch_number], patch);
    //fprintf(stderr, "store_patch: patch %d n_osc %d patch %s\n", patch_number, max_osc, patch);
}

extern int parse_list_uint16_t(char *message, uint16_t *vals, int max_num_vals, uint16_t skipped_val);


// This is called when i get an event with voices (or an instrument) in it, BUT NOT for a load_patch - that has already been handled.
// So i know that the patch / voice alloc already exists and the patch has already been set!
void patches_event_has_voices(struct event e, void (*callback)(struct delta d, void*user_data), void*user_data ) {
    uint16_t voices[MAX_VOICES];
    uint8_t num_voices;
    if (!AMY_IS_SET(e.instrument)) {
        // No instrument, just directly naming the voices.
        num_voices = parse_list_uint16_t(e.voices, voices, MAX_VOICES, 0);
    } else {
        // We have an instrument specified - decide which of its voices are actually to be used.
        
        // It's a mistake to specify both synth (instrument) and voices, warn user we're ignoring voices.
        // (except in the afterlife of a load_patch event, which will most likely be empty anyway).
        if (e.voices[0] != 0 && !AMY_IS_SET(e.load_patch)) {
            fprintf(stderr, "You specified both synth %d and voices %s.  Synth implies voices, ignoring voices.\n",
                    e.instrument, e.voices);
        }
        if (!AMY_IS_SET(e.velocity)) {
            // Not a note-on/note-off message - treat the synth as a shorthand for *all* the voices.
            num_voices = instrument_get_voices(e.instrument, voices);
        } else {
            // velocity is present, this is a note-on/note-off.
            if (! (AMY_IS_SET(e.midi_note) && e.midi_note != 0) ) {
                // velocity without a note number (or for midi_note=0).  This is valid for velocity==0 => all-notes-off.
                if (e.velocity != 0) {
                    // Attempted a note-on to all voices, suppress.
                    fprintf(stderr, "note-on for synth %d without specifying note (or note 0) - ignored.\n", e.instrument);
                    return;
                }
                // All notes off - find out which voices are actually currently active, so we can turn them off.
                num_voices = instrument_all_notes_off(e.instrument, voices);
            } else {
                // It's a note-on or note-off event, so the instrument mechanism chooses which single voice to use.
                uint8_t note = (uint8_t)roundf(e.midi_note);
                bool is_note_off = (e.velocity == 0);
                voices[0] = instrument_voice_for_note_event(e.instrument, note, is_note_off);
                if (voices[0] == _INSTRUMENT_NO_VOICE) {
                    // For now, I think this can only happen with a note-off that has no matching note-on.
                    fprintf(stderr, "synth %d did not find a voice, dropping message.", e.instrument);
                    return;
                }
                num_voices = 1;
            }
        }
    }
    // clear out the instrument, voices, patch from the event. If we didn't, we'd keep calling this over and over
    e.voices[0] = 0;
    AMY_UNSET(e.load_patch);
    AMY_UNSET(e.instrument);
    // for each voice, send the event to the base osc (+ e.osc if given, added by amy_add_event)
    for(uint8_t i=0;i<num_voices;i++) {
        if(AMY_IS_SET(voice_to_base_osc[voices[i]])) {
            uint16_t target_osc = voice_to_base_osc[voices[i]];
            amy_parse_event_to_deltas(e, target_osc, callback, user_data);
        }
    }
}

// Given an event with a load_patch object AND a voices object in it
// This means to set/reset the voices and load the messages from ROM and set them

void patches_load_patch(struct event e) {
    char sub_message[255];
    
    uint16_t voices[MAX_VOICES];
    uint8_t num_voices = parse_list_uint16_t(e.voices, voices, MAX_VOICES, 0);
    char *message;
    uint16_t patch_osc = 0;
    if(e.load_patch >= _PATCHES_FIRST_USER_PATCH) {
        int patch_number = e.load_patch - _PATCHES_FIRST_USER_PATCH;
        patch_osc = memory_patch_oscs[patch_number];
        if(patch_osc > 0){
            message = memory_patch[patch_number];
        } else {
            num_voices = 0; // don't do anything
        }
    } else {
        message = (char*)patch_commands[e.load_patch];    
        patch_osc = patch_oscs[e.load_patch];
    }
    for(uint8_t v=0;v<num_voices;v++) {
        // Find the first osc with patch_oscs[e.load_patch] free oscs
        // First, is this an old voice we're re-doing? 
        if(AMY_IS_SET(voice_to_base_osc[voices[v]])) {
            //fprintf(stderr, "Already set voice %d, removing it\n", voices[v]);
            // Remove the oscs for this old voice
            for(uint16_t i=0;i<AMY_OSCS;i++) {
                if(osc_to_voice[i]==voices[v]) { 
                    //fprintf(stderr, "Already set voice %d osc %d, removing it\n", voices[v], i);
                    AMY_UNSET(osc_to_voice[i]);
                }
            }
            AMY_UNSET(voice_to_base_osc[voices[v]]);
        }

        // Now find some oscs
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
                // Are there num_voices patch_oscs free oscs after this one?
                good = 1;
                for(uint16_t j=0;j<patch_osc;j++) {
                    good = good & (AMY_IS_UNSET(osc_to_voice[osc+j]));
                }
                if(good) {
                    //fprintf(stderr, "found %d consecutive oscs starting at %d for voice %d\n", patch_oscs[e.load_patch], osc, voices[v]);
                    //fprintf(stderr, "setting base osc for voice %d to %d\n", voices[v], osc);
                    voice_to_base_osc[voices[v]] = osc; 
                    for(uint16_t j=0;j<patch_osc;j++) {
                        //fprintf(stderr, "setting osc %d for voice %d to amy osc %d\n", j, voices[v], osc+j);
                        osc_to_voice[osc+j] = voices[v];
                        reset_osc(osc+j);
                    }
                    // exit the loop
                    i = AMY_OSCS + 1;
                }
            }
        }
        if(!good) {
            fprintf(stderr, "we are out of oscs for voice %d. not setting this voice\n", voices[v]);
        } else {
            uint16_t start = 0;
            for(uint16_t i=0;i<strlen(message);i++) {
                if(message[i] == 'Z') {
                    strncpy(sub_message, message + start, i - start + 1);
                    sub_message[i-start+1]= 0;
                    struct event patch_event = amy_parse_message(sub_message);
                    patch_event.time = e.time;
                    if(patch_event.status == EVENT_SCHEDULED) {
                        amy_add_event_internal(patch_event, voice_to_base_osc[voices[v]]);
                    }
                    start = i+1;
                }
            }
        }
    }
    // Finally, store as an instrument if instrument number is specified.
    if (AMY_IS_SET(e.instrument)) {
        instrument_add_new(e.instrument, num_voices, voices);
    }
}
