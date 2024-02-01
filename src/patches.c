// patches.c
// baked in AMY string patches (Juno-6 for now)

#include "amy.h"
#include "patches.h"
#define JUNO_PATCHES 128
#define TOTAL_PATCHES 128

void patches_init() {

}

void patches_setup_patch(uint16_t osc) {
    char * message = patch_commands[synth[osc].patch];
    amy_play_message_with_base(message, osc);
}

void patches_note_on(uint16_t osc) {
     if(AMY_IS_SET(synth[osc].patch)) { 
        patches_setup_patch(osc);
    }
}


void patches_note_off(uint16_t osc) {
    // Do whatever note_off does in juno.py
}

