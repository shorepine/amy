// patches.c
// baked in AMY string patches (Juno-6 & DX7 for now)

#include "amy.h"
#include "patches.h"

// Here, we need to return how many oscillators we used...
uint16_t patches_message(char* message, uint16_t base_osc) {
    char sub_message[255];
    uint16_t start = 0;
    uint16_t max_osc = 0;
    for(uint16_t i=0;i<strlen(message);i++) {
        if(message[i] == 'Z') {
            strncpy(sub_message, message + start, i - start + 1);
            sub_message[i-start+1]= 0;
            struct event e = amy_parse_message(sub_message, base_osc);
            if(e.status == SCHEDULED) {
                if(e.osc-base_osc > max_osc) max_osc = e.osc-base_osc;
                amy_add_event(e);
            }
            start = i+1;
        }
    }
    return max_osc;
}

void patches_load_patch(uint16_t patch, uint16_t base_osc) {
    uint16_t oscs = patches_message((char*)patch_commands[patch], base_osc);
    // todo, use oscs to set up voices
    
}

