// patches.c
// baked in AMY string patches (Juno-6 & DX7 for now)

#include "amy.h"
#include "patches.h"

void patches_message(char* message, uint16_t base_osc) {
    char sub_message[255];
    uint16_t start = 0;
    for(uint16_t i=0;i<strlen(message);i++) {
        if(message[i] == 'Z') {
            strncpy(sub_message, message + start, i - start + 1);
            sub_message[i-start+1]= 0;
            struct event e = amy_parse_message(sub_message, base_osc);
            if(e.status == SCHEDULED) {
                amy_add_event(e);
            }
            start = i+1;
        }
    }
}

void patches_load_patch(uint16_t patch, uint16_t base_osc) {
    patches_message((char*)patch_commands[patch], base_osc);
}

