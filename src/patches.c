// patches.c
// baked in AMY string patches (Juno-6 & DX7 for now)

#include "amy.h"
#include "patches.h"

/* patch spec

amy.send(patch_number=1, voices="0,1,2,3") # load juno patch into 4 voices. internally figure out which oscs to use and update our internal map of oscs <-> voices
amy.send(patch_number=130, voices="4,5") # load dx7 patch into 2 voices.
amy.send(voices="0,1", vel=1, note=40) # send this note on to voices 0 and 1
amy.send(voices="2",vel=1, note=44) # different note on differnet voice
amy.send(voices="4", vel=1, note=50) # dx7 note on
amy.send(voices="4", osc=1, filter_freq="440,0,0,0,5") -- can address an osc offset if you give both

so basically, a voice is a lookup to a base_osc
if you get multiple in a message, you call the message N times across the base_oscs, that includes patch_number
if you get a osc and a voice, you add the osc to the base_osc lookup and send the message 

*/

#define _PATCHES_FIRST_USER_PATCH 1024

#define MAX_VOICES 32
#define MEMORY_PATCHES 32
char * memory_patch[MEMORY_PATCHES];
uint16_t next_user_patch_index = 0;
uint16_t memory_patch_oscs[MEMORY_PATCHES];
uint8_t * osc_to_voice;
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
    uint16_t voices[16 /* MAX_VOICES_PER_INSTRUMENT */];
    for (uint8_t i = 0; i < 32 /* MAX_INSTRUMENTS */; ++i) {
        int num_voices = instrument_get_voices(i, voices);
        if (num_voices) {
            fprintf(stderr, "instrument %d num_voices %d patch_num %d flags %d voices", i, num_voices, instrument_get_patch_number(i), instrument_get_flags(i));
            for (int j = 0; j < num_voices; ++j)  fprintf(stderr, " %d", voices[j]);
            fprintf(stderr, "\n");
        }
    }
}

void all_notes_off() {
    for(uint16_t i=0;i<AMY_OSCS;i++) {
        if (AMY_IS_SET(osc_to_voice[i])) {
            if(synth[i]->status == SYNTH_AUDIBLE) {
                synth[i]->status = SYNTH_AUDIBLE_SUSPENDED;
            }
        }
    }
}

void patches_init() {
    for(uint8_t i=0;i<MEMORY_PATCHES;i++) {
        memory_patch_oscs[i]  = 0;
        memory_patch[i] = NULL;
    }
    next_user_patch_index = 0;
}

void patches_reset() {
    if(osc_to_voice != NULL) free(osc_to_voice);
    osc_to_voice = (uint8_t*)malloc(AMY_OSCS);

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
    next_user_patch_index = 0;
}

void patches_store_patch(struct event *e, char * patch_string) {
    // amy patch string. Either pull patch_number from e, or allocate a new one and write it to e.
    // Patch is stored in ram.
    //fprintf(stderr, "store_patch: synth %d patch_num %d patch '%s'\n", e->instrument, e->patch_number, patch_string);
    if (!AMY_IS_SET(e->patch_number)) {
        // Check for a repeated string
        for (int i = 0; i < MEMORY_PATCHES; ++i) {
            if (memory_patch_oscs[i] > 0)
                if (strcmp(memory_patch[i], patch_string) == 0) {
                    e->patch_number = i + _PATCHES_FIRST_USER_PATCH;
                    // Actually, we don't need to store it, we already have it.
                    //fprintf(stderr, "store_patch: using existing patch_number %d for '%s'\n", e->patch_number, patch_string);
                    return;
                }
        }
        // We need to allocate a new number.
        e->patch_number = next_user_patch_index + _PATCHES_FIRST_USER_PATCH;
        // next_user_patch_index is updated as needed at the bottom of the function (so it can reflect user-defined numbers too).
        //fprintf(stderr, "store_patch: auto-assigning patch number %d for '%s'\n", e->patch_number, patch_string);
    }
    int patch_index = e->patch_number - _PATCHES_FIRST_USER_PATCH;
    if (patch_index < 0 || patch_index >= MEMORY_PATCHES) {
        fprintf(stderr, "patch number %d is out of range (%d .. %d)\n",
                patch_index + _PATCHES_FIRST_USER_PATCH, _PATCHES_FIRST_USER_PATCH, _PATCHES_FIRST_USER_PATCH + MEMORY_PATCHES);
        return;
    }
    if (patch_index >= next_user_patch_index)  next_user_patch_index = patch_index + 1;
    // Now find out how many oscs this message uses
    uint16_t max_osc = 0;
    char sub_message[255];
    uint16_t start = 0;
    for(uint16_t i = 0; i < strlen(patch_string) + 1; i++) {
        if (patch_string[i] == 'Z' || patch_string[i] == '\0') {
            strncpy(sub_message, patch_string + start, i - start + 1);
            sub_message[i - start + 1]= 0;
            struct event patch_event = amy_default_event();
	    amy_parse_message(sub_message, &patch_event);
            if(AMY_IS_SET(patch_event.osc) && patch_event.osc > max_osc)
                max_osc = patch_event.osc;
            start = i+1;
        }
    }
    if(memory_patch_oscs[patch_index] > 0) { free(memory_patch[patch_index]); }
    memory_patch[patch_index] = malloc(strlen(patch_string)+1);
    memory_patch_oscs[patch_index] = max_osc + 1;
    strcpy(memory_patch[patch_index], patch_string);
    //fprintf(stderr, "store_patch: patch %d max_osc %d patch %s (e->num_vx=%d)\n", patch_index, max_osc, patch_string, e->num_voices);
}

extern int parse_list_uint16_t(char *message, uint16_t *vals, int max_num_vals, uint16_t skipped_val);


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

struct pcm_sample_info drumkit[AMY_MIDI_DRUMS_HIGHEST_NOTE - AMY_MIDI_DRUMS_LOWEST_NOTE + 1] = {
    {28, 60},  // "Std Kick", 35)
    {25, 60},  // "Pwr Kick", 36),
    {2, 45},   // "Snare", 37),
    {5, 41},   // "Snare4", 38),
    {9, 94},   // "Clap", 39),
    {20, 60},  // "Pwr Snare", 40),
    {8, 61},   // "Low floor Tom", 41),
    {6, 53},   // "Closed Hat", 42),
    {8, 68},  // "Hi floor Tom", 43),
    {7, 61},  // "Pedal hi-hat", 44
    {21, 56},  // "low tom", 45
    {7, 56},   // "Open Hat", 46),
    {21, 63},  // "low-mid tom", 47
    {21, 70},  // "hi-mid tom", 48
    {16, 60},  // "Crash", 49),
    {21, 77},  // "hi-tom", 50,
    {7, 51},  // "ride cymbal", 51,
    {16, 50},  // "chinese cymbal", 52,
    {6,  47},  // "ride bell", 53,
    {9, 84},  // "tambourine", 54,
    {7, 46},  // "splash cymbal", 55,
    {10, 69},  // "Cowbell", 56),
    {7, 57},  // "crash cymbal 2", 57,
    {-1, -1},  // "vibraslap", 58,
    {7, 48},  // "ride cymbal", 59,
    {11, 74},  // "hi bongo", 60,
    {11, 67},  // "low bongo", 61,
    {11, 77},  // "mute hi conga", 62,
    {8, 77},  // "open hi conga", 63,
    {11, 64},  // "Congo Low", 64),
    {21, 79},  // "high timbale", 65,
    {21, 73},  // "low timbale", 66,
    {13, 55},  // "high agogo", 67,
    {13, 50},  // "low agogo", 68,
    {0, 79},   // "cabasa", 69,
    {0, 89},   // "Maraca", 70),
    {-1, -1},  // "short whistle", 71,
    {-1, -1},  // "long whistle", 72,
    {-1, -1},  // "short guiro", 73,
    {-1, -1},  // "long guiro", 74,
    {12, 82},  // "Clave", 75),
    {13, 60},  // "hi Block", 76),
    {13, 52},  // "low block", 77
    {-1, -1},  // "mute cuica", 78
    {-1, -1},  // "open cuica", 79
    {-1, -1},  // "mute triangle", 80
    {-1, -1},  // "open trianlge", 81
    //    {1, 39},  // "Kick", None),
    //    {3, 52},  // "Snare2", None),
    //    {4, 51},  // "Snare3", None),
    //    {14, 60},  // "Roll", None),
    //    {15, 60},  // "Hit", None),
    //    {26, 66},  // "Marimba", None),
    //    {27, 60},  // "Frets", None),
    //    {17, 60},  // "Shell", None),
    //    {18, 60},  // "Chimes", None),
    //    {19, 60},  // "Seashore", None),
    //    {22, 66},  // "Shamisen", None),
    //    {23, 66},  // "Koto", None),
    //    {24, 72},  // "Steel", None),
};


bool setup_drum_event(struct event *e, uint8_t note) {
  // Special-case processing to convert MIDI drum notes into PCM patch events.
  bool forward_note = false;
  if (note >= AMY_MIDI_DRUMS_LOWEST_NOTE && note <= AMY_MIDI_DRUMS_HIGHEST_NOTE) {
      struct pcm_sample_info s = drumkit[note - AMY_MIDI_DRUMS_LOWEST_NOTE];
      if (s.pcm_preset_number != -1) {
          e->preset = s.pcm_preset_number;
          e->midi_note = s.base_midi_note;
          forward_note = true;
      }
  }
  return forward_note;
}


// This is called when i get an event with voices (or an instrument) in it, BUT NOT for a load_patch - that has already been handled.
// So i know that the patch / voice alloc already exists and the patch has already been set!
void patches_event_has_voices(struct event *e, void (*callback)(struct delta *d, void*user_data), void*user_data ) {
    uint16_t voices[MAX_VOICES];
    uint8_t num_voices;
    uint32_t flags = 0;
    if (!AMY_IS_SET(e->instrument)) {
        // No instrument, just directly naming the voices.
        num_voices = parse_list_uint16_t(e->voices, voices, MAX_VOICES, 0);
    } else {
        // We have an instrument specified - decide which of its voices are actually to be used.

        // It's a mistake to specify both synth (instrument) and voices, warn user we're ignoring voices.
        // (except in the afterlife of a load_patch event, which will most likely be empty anyway).
        if (e->voices[0] != 0 && !AMY_IS_SET(e->patch_number)) {
            fprintf(stderr, "You specified both synth %d and voices %s.  Synth implies voices, ignoring voices.\n",
                    e->instrument, e->voices);
        }
        flags = instrument_get_flags(e->instrument);
        if (AMY_IS_SET(e->to_instrument)) {
            // This involves moving the instrument number.
            instrument_change_number(e->instrument, e->to_instrument);
            e->instrument = e->to_instrument;
            AMY_UNSET(e->to_instrument);
            // Then continue handling any other args.
        }
        if (AMY_IS_SET(e->grab_midi_notes)) {
            // Set the grab_midi state.
            instrument_set_grab_midi_notes(e->instrument, e->grab_midi_notes);
        }
        if (AMY_IS_SET(e->pedal)) {
            // Pedal events are a special case
            bool sustain = (e->pedal != 0);
            if (flags & _INSTRUMENT_FLAGS_NEGATE_PEDAL) {
                sustain = !sustain;  // Some MIDI pedals report backwards.
            }
            // A sustain release can result in note-off events for multiple voices.
            num_voices = instrument_sustain(e->instrument, sustain, voices);
            if (num_voices) {
                e->velocity = 0;
            }
            //fprintf(stderr, "instrument %d pedal %d num_voices %d\n", e->instrument, e->pedal, num_voices);
        } else if (!AMY_IS_SET(e->velocity)) {
            // Not note on/off, treat the synth as a shorthand for *all* the voices.
            num_voices = instrument_get_voices(e->instrument, voices);
        } else {
            // velocity is present, this is a note-on/note-off.
            if (AMY_IS_UNSET(e->midi_note) && AMY_IS_UNSET(e->preset)) {
                // velocity without midi_note is valid for velocity==0 => all-notes-off.
                if (e->velocity != 0) {
                    // Attempted a note-on to all voices, suppress.
                    fprintf(stderr, "note-on with no note for synth %d - ignored.\n", e->instrument);
                    return;
                }
                // All notes off - find out which voices are actually currently active, so we can turn them off.
                num_voices = instrument_all_notes_off(e->instrument, voices);
            } else {
                // It's a note-on or note-off event, so the instrument mechanism chooses which single voice to use.
                uint16_t note = 0;
                if (AMY_IS_SET(e->midi_note))  // midi note can be unset if preset is set.
                    note = (uint8_t)roundf(e->midi_note);
                if (flags & _INSTRUMENT_FLAGS_MIDI_DRUMS) {
                    if (!setup_drum_event(e, note))
                        return;   // It's not a MIDI drum event we can emulate, just drop the event.
                }
		if (AMY_IS_SET(e->preset)) {
		    // This event includes a note *and* a preset, so it's like a drum sample note on.
		    // Wrap the preset number into the note, so we don't allocate the same pitch for different drums to the same voice.
		    note += 128 * e->preset;
		}
                bool is_note_off = (e->velocity == 0);
                voices[0] = instrument_voice_for_note_event(e->instrument, note, is_note_off);
                if (voices[0] == _INSTRUMENT_NO_VOICE) {
                    // For now, I think this can only happen with a note-off that has no matching note-on.
                    //fprintf(stderr, "synth %d did not find a voice, dropping message.\n", e->instrument);
                    // No, it also happens with note-offs when pedal is down.
                    return;
                }
                num_voices = 1;
            }
            //fprintf(stderr, "instrument %d vel %d note %d voice %d\n", e->instrument, (int)roundf(127.f * e->velocity), (int)roundf(e->midi_note), voices[0]);
        }
        if (AMY_IS_SET(e->velocity) && e->velocity == 0 && (flags & _INSTRUMENT_FLAGS_IGNORE_NOTE_OFFS))
            return;  // Ignore the note off, as requested.
    }
    // clear out the instrument, voices, patch from the event. If we didn't, we'd keep calling this over and over
    e->voices[0] = 0;
    AMY_UNSET(e->patch_number);
    int instrument = e->instrument;
    AMY_UNSET(e->instrument);
    // for each voice, send the event to the base osc (+ e->osc if given, added by amy_add_event)
    for(uint8_t i=0;i<num_voices;i++) {
        if(AMY_IS_SET(voice_to_base_osc[voices[i]])) {
            uint16_t target_osc = voice_to_base_osc[voices[i]];
            amy_parse_event_to_deltas(e, target_osc, callback, user_data);
	    //fprintf(stderr, "patches: synth %d voice %d osc %d wav %d note %d vel %d\n", instrument, voices[i], target_osc, e->wave, (int)e->midi_note, (int)(127.f * e->velocity));
        }
    }
    // Restore the instrument in case this event is re-used.
    e->instrument = instrument;
}

void release_voice_oscs(int voice) {
    if(AMY_IS_SET(voice_to_base_osc[voice])) {
        //fprintf(stderr, "Already set voice %d, removing it\n", voice);
        // Remove the oscs for this old voice
        for(uint16_t i=0;i<AMY_OSCS;i++) {
            if(osc_to_voice[i]==voice) {
                //fprintf(stderr, "Already set voice %d osc %d, removing it\n", voices[v], i);
                AMY_UNSET(osc_to_voice[i]);
            }
        }
        AMY_UNSET(voice_to_base_osc[voice]);
    }
}

void patches_load_patch(struct event *e) {
    // Given an event with a patch/patch_number AND a voices/instrument spec in it.
    // (also called if instrument & num_voices even if no patch specified, to change #voices).
    // This means to set/reset the voices and load the messages from ROM and set them.
    uint16_t voices[MAX_VOICES];
    uint8_t num_voices = 0;
    uint16_t patch_number = e->patch_number;
    //fprintf(stderr, "load_patch synth %d patch_number %d num_voices %d\n", e->instrument, e->patch_number, e->num_voices);
    if (AMY_IS_SET(e->instrument)) {
        // Instrument specified.
        if (AMY_IS_UNSET(e->patch_number)) {
            // If no patch number is provided, pull from existing instrument.
            int old_patch_number = instrument_get_patch_number(e->instrument);
            if (old_patch_number == -1) {
                fprintf(stderr, "attempting to configure synth %d (%d voices) without patch/patch_number, but no previous patch found\n",
                        e->instrument, e->num_voices);
                return;  // Ignore
            }
            // Otherwise, inherit the existing patch number.
            patch_number = old_patch_number;
        }
        // If the instrument is alread initialized, copy the voice numbers.
        num_voices = instrument_get_voices(e->instrument, voices);
        if (AMY_IS_SET(e->num_voices) && e->num_voices != num_voices) {
            // If we did already have voice oscs, release them.
            for (int i = 0; i < num_voices; ++i) {
                release_voice_oscs(voices[i]);
            }
            num_voices = 0;
            // Find avaliable voices with a single pass through voice_to_base_osc.
            int v = 0;
            for (int i = 0; i < e->num_voices; ++i) {
                while (v < MAX_VOICES) {
                    if (AMY_IS_UNSET(voice_to_base_osc[v])) break;
                    ++v;
                }
                if (v == MAX_VOICES)  {
                    fprintf(stderr, "ran out of voices allocating %d voices to instrument %d for patch %d, ignoring.", e->num_voices, e->instrument, patch_number);
                    return;
                }
                voices[i] = v;
                ++v;
                ++num_voices;
            }
            // Was this deleting the instrument?  i.e. was e->num_voices set but setting the num_voices to zero?
            if (num_voices == 0) {
                instrument_release(e->instrument);
                // Delete the instrument number so we don't forward the 'rest' of the event to it.
                AMY_UNSET(e->instrument);
                return;
            }
        }
        //fprintf(stderr, "Allocated %d voices to instrument %d patch %d\n", num_voices, e->instrument, patch_number);
        //for (int i = 0; i < num_voices; ++i) {
        //    fprintf(stderr, "%d; ", voices[i]);
        //}
        //fprintf(stderr, "\n");
    }
    if (e->voices[0]) {
        num_voices = parse_list_uint16_t(e->voices, voices, MAX_VOICES, 0);
    }
    if (num_voices == 0) {
        fprintf(stderr, "patch_number %d but no voices allocated, ignored (instrument %d num_voices %d voices '%s')\n",
                patch_number, e->instrument, e->num_voices, e->voices);
        return;
    }
    // At this point, we have the voices[] array and num_voices set up to be initialized.
    char *message;
    uint16_t patch_osc = 0;
    if(patch_number >= _PATCHES_FIRST_USER_PATCH) {
        int patch_index = patch_number - _PATCHES_FIRST_USER_PATCH;
        patch_osc = memory_patch_oscs[patch_index];
        if(patch_osc > 0){
            message = memory_patch[patch_index];
        } else {
            num_voices = 0; // don't do anything
        }
    } else {
        message = (char*)patch_commands[patch_number];
        patch_osc = patch_oscs[patch_number];
    }
    for(uint8_t v=0;v<num_voices;v++)  {
        // Release all the oscs of any voices we're re-using before we start re-allocating oscs.
        release_voice_oscs(voices[v]);
    }
    for(uint8_t v=0;v<num_voices;v++)  {
        // Find the first osc with patch_osc free oscs.
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
                    //fprintf(stderr, "found %d consecutive oscs starting at %d for voice %d\n", patch_oscs[patch_number], osc, voices[v]);
                    //fprintf(stderr, "setting base osc for voice %d to %d\n", voices[v], osc);
                    voice_to_base_osc[voices[v]] = osc; 
                    for(uint16_t j=0;j<patch_osc;j++) {
                        //fprintf(stderr, "setting osc %d for voice %d to amy osc %d\n", j, voices[v], osc+j);
                        osc_to_voice[osc+j] = voices[v];
                        //reset_osc(osc+j);
                        // Use event mechanism to post osc resets, to ensure they are done in sequence.
                        // This was important because for testing the patch reassignment, we were running the default
                        // osc setup, then changing the patch, which reset the oscs here, but then the osc config
                        // from the default setup was applied *after* the reset, so the osc state was not reset.
                        struct event reset_event = amy_default_event();
                        reset_event.reset_osc = osc + j;
                        amy_add_event_internal(&reset_event, 0);
                    }
                    // exit the loop
                    i = AMY_OSCS + 1;
                }
            }
        }
        if(!good) {
            fprintf(stderr, "we are out of oscs for voice %d. not setting this voice\n", voices[v]);
        }
    }  // end of loop setting up voice_to_base_osc for all voices[v]
    // Now actually initialize the newly-allocated osc blocks with the patch
    uint16_t start = 0;
    //fprintf(stderr, "load_patch: synth %d voice %d message %s\n", e->instrument, voices[v], message);
    char sub_message[256];
    for(uint16_t i=0;i<strlen(message) + 1;i++) {
        if(i == strlen(message) || message[i] == 'Z') {  // If patch doesn't end in Z, still send up to the the end.
            strncpy(sub_message, message + start, i - start + 1);
            sub_message[i-start+1]= 0;
            struct event patch_event = amy_default_event();
            amy_parse_message(sub_message, &patch_event);
            patch_event.time = e->time;
            if(patch_event.status == EVENT_SCHEDULED) {
                for(uint8_t v=0;v<num_voices;v++)
                    if(AMY_IS_SET(voice_to_base_osc[voices[v]]))
                        amy_add_event_internal(&patch_event, voice_to_base_osc[voices[v]]);
            }
            start = i+1;
            //fprintf(stderr, "load_patch: sub_message %s\n", sub_message);
        }
    }
    // Finally, store as an instrument if instrument number is specified.
    if (AMY_IS_SET(e->instrument)) {
        uint32_t flags = 0;
        if (AMY_IS_SET(e->instrument_flags)) flags = e->instrument_flags;
        instrument_add_new(e->instrument, num_voices, voices, patch_number, flags);
    }
}
