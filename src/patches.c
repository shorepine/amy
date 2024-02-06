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


does load_patch REQUIRE a voice? hm, i say YES 

so, user says load_patch, voices=0,1 . amy_add_event first calls patches_set_voices with e (which has both)

no, it has to first call patches_load_patch to get the oscs

*/

#define MAX_VOICES 16
uint16_t voice_to_base_osc[MAX_VOICES];
uint16_t voice_to_osc_count[MAX_VOICES]; 

extern int parse_int_list_message(char *message, int16_t *vals, int max_num_vals);



// This is called when i get an event with voices in it, in amy_add_event (not at playback time)
void patches_set_voices(struct event e) {
    int16_t voices[MAX_VOICES];
    uint8_t num_voices = parse_int_list_message(e.voices, voices, MAX_VOICES);
    // for each voice, send the event to the base osc (+ e.osc if given!)
    for(uint8_t i=0;i<num_voices;i++) {
        amy_add_event_internal(e, voice_to_base_osc[voices[i]]+e.osc);
    }
}

// Given an event with a load_patch object in it, load the messages from ROM and set them
uint16_t patches_message(struct event e) {
    char sub_message[255];
    uint16_t start = 0;
    uint16_t max_osc = 0;
    
    char*message = (char*)patch_commands[e.load_patch];

    for(uint16_t i=0;i<strlen(message);i++) {
        if(message[i] == 'Z') {
            strncpy(sub_message, message + start, i - start + 1);
            sub_message[i-start+1]= 0;
            struct event patch_event = amy_parse_message(sub_message);
            patch_event.time = e.time;
            if(patch_event.status == SCHEDULED) {
                if(patch_event.osc > max_osc) max_osc = patch_event.osc;
                amy_add_event_internal(patch_event, e.osc);
            }
            start = i+1;
        }
    }
    return max_osc;
}

void patches_load_patch(struct event e) {

    uint16_t oscs = patches_message(e);
    
}

