// patches.c
// baked in AMY string patches (Juno-6 & DX7 for now)

#include "amy.h"
#include "patches.h"
#define JUNO_PATCHES 128
#define DX7_PATCHES 128
#define TOTAL_PATCHES 256

void patches_init() {

}

void patches_message(char* message, uint16_t base_osc) {
    // This, but also check for Zs and iterate, and bump up the oscs in the message to + osc
    // grab the Zs from alles
    char sub_message[255];
    uint16_t start = 0;
    char * message_start_pointer = message;
    fprintf(stderr, "got message %s strlen is %d, base is %d\n", message, strlen(message), base_osc);
    for(uint16_t i=0;i<strlen(message);i++) {
        if(message[i] == 'Z') {
            message_start_pointer = message + start;
            strncpy(sub_message, message_start_pointer, i - start);
            fprintf(stderr, "sending message %s with len %d\n", sub_message, strlen(sub_message));
            struct event e = amy_parse_message(sub_message, base_osc);
            fprintf(stderr, "parsed into e\n");
            if(e.status == SCHEDULED) {
                fprintf(stderr, "About to add e\n");
                amy_add_event(e);
                fprintf(stderr, "sent\n");
            }
            start = i+1;
        }
    }
}


void patches_setup_patch(uint16_t osc) {
    patches_message((char*)patch_commands[synth[osc].patch], osc);
}

void patches_note_on(uint16_t osc) {
    fprintf(stderr, "patches note on osc %d patch is %d\n", osc, synth[osc].patch);
    if(!synth[osc].patch_loaded) {
        fprintf(stderr, "patch NOT loaded\n");
        patches_setup_patch(osc);
        synth[osc].patch_loaded = 1;
    }
}


void patches_note_off(uint16_t osc) {
    // Do whatever note_off does in juno.py
}

