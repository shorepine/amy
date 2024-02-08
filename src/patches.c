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

#define MAX_VOICES 16
uint8_t osc_to_voice[AMY_OSCS];
uint16_t voice_to_base_osc[MAX_VOICES];

extern int parse_int_list_message(char *message, int16_t *vals, int max_num_vals);


void patches_reset() {
    for(uint8_t v=0;v<MAX_VOICES;v++) {
        AMY_UNSET(voice_to_base_osc[v]);
    }
    for(uint16_t i=0;i<AMY_OSCS;i++) {
        AMY_UNSET(osc_to_voice[i]);
    }
}


// This is called when i get an event with voices in it, BUT NOT with a load_patch 
// So i know that the patch / voice alloc already exists and the patch has already been set!
void patches_event_has_voices(struct event e) {
    int16_t voices[MAX_VOICES];
    uint8_t num_voices = parse_int_list_message(e.voices, voices, MAX_VOICES);
    // clear out the voices and patch now from the event. If we didn't, we'd keep calling this over and over
    e.voices[0] = 0;
    AMY_UNSET(e.load_patch);
    // for each voice, send the event to the base osc (+ e.osc if given!)
    for(uint8_t i=0;i<num_voices;i++) {
        if(AMY_IS_SET(voice_to_base_osc[i])) {
            uint16_t target_osc = voice_to_base_osc[voices[i]] + e.osc;
            amy_add_event_internal(e, target_osc);
        }
    }
}

// Given an event with a load_patch object AND a voices object in it
// This means to set/reset the voices and load the messages from ROM and set them

void patches_load_patch(struct event e) {
    char sub_message[255];
    
    int16_t voices[MAX_VOICES];
    uint8_t num_voices = parse_int_list_message(e.voices, voices, MAX_VOICES);

    char*message = (char*)patch_commands[e.load_patch];
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

        // If voice % 2 = 1, start at AMY_OSCS/2, to distribute across both "halves" for mulitcore on platforms that use it
        uint16_t osc_start = 0;
        if(voices[v]%2) osc_start = (AMY_OSCS/2);

        for(uint16_t i=0;i<AMY_OSCS;i++) {
            uint16_t osc = (osc_start + i) % AMY_OSCS;
            if(AMY_IS_UNSET(osc_to_voice[osc])) {
                // Are there num_voices patch_oscs free oscs after this one?
                good = 1;
                for(uint16_t j=0;j<patch_oscs[e.load_patch];j++) {
                    good = good & (AMY_IS_UNSET(osc_to_voice[osc+j]));
                }
                if(good) {
                    //fprintf(stderr, "found %d consecutive oscs starting at %d for voice %d\n", patch_oscs[e.load_patch], osc, voices[v]);
                    //fprintf(stderr, "setting base osc for voice %d to %d\n", voices[v], osc);
                    voice_to_base_osc[voices[v]] = osc; 
                    for(uint16_t j=0;j<patch_oscs[e.load_patch];j++) {
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
                    if(patch_event.status == SCHEDULED) {
                        amy_add_event_internal(patch_event, voice_to_base_osc[voices[v]]);
                    }
                    start = i+1;
                }
            }
        }
    }
}


