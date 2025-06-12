// instrument.c
//
// An instrument is a set of voices with voice-allocation logic.
// The user calls instrument_note_on(), and this logic chooses one of the
// preallocated voices to play the note on.
//
// This is a replacement in C for the functionality in
// tulip/shared/py/synth.py.

#include "amy.h"
#include <inttypes.h>

#define INSTRUMENT_RAM_CAPS SYNTH_RAM_CAPS

struct voice_fifo {
    char *name;
    uint8_t length;
    uint8_t entries[MAX_VOICES_PER_INSTRUMENT + 1];  // fifo has one extra element to distinguish empty and full.
    uint8_t head;
    uint8_t tail;
};

// Fifo must provide init, put, get, remove, empty

void voice_fifo_debug(struct voice_fifo *fifo) {
    fprintf(stderr, "fifo %s entries 0x%lx len %d head %d tail %d: ", fifo->name, (long)fifo->entries, fifo->length, fifo->head, fifo->tail);
    uint8_t current = fifo->tail;
    while(current != fifo->head) {
        fprintf(stderr, "%d ", fifo->entries[current]);
        current = (current + 1) % fifo->length;
    }
    fprintf(stderr, "\n");
}

struct voice_fifo *voice_fifo_init(int size, char *name) {
    if (size <=0 || size > MAX_VOICES_PER_INSTRUMENT) {
        fprintf(stderr, "init_voice_fifo: size %d value error (max size is %d)\n", size, MAX_VOICES_PER_INSTRUMENT);
        return NULL;
    }
    struct voice_fifo *result = (struct voice_fifo *)malloc_caps(sizeof(struct voice_fifo), amy_global.config.ram_caps_synth);
    result->head = 0;
    result->tail = 0;
    result->length = MAX_VOICES_PER_INSTRUMENT + 1;  // One more than max size.  fixed in this implementation.
    result->name = name;  // very dicey - only works because I'm passing statically-allocated names.
    return result;
}

void voice_fifo_free(struct voice_fifo *fifo) {
    free(fifo);
}

bool voice_fifo_empty(struct voice_fifo *fifo) {
    return (fifo->head == fifo->tail);
}

void voice_fifo_put(struct voice_fifo *fifo, uint8_t val) {
    fifo->entries[fifo->head++] = val;
    if (fifo->head == fifo->length)  fifo->head = 0;
    if (fifo->head == fifo->tail) {
        fprintf(stderr, "**fifo %s: overflow\n", fifo->name);
        // Drop the entry we just overwrote.
        fifo->tail = (fifo->tail + 1) % fifo->length;
    }
    //fprintf(stderr, "I: fifo %s: put %d\n", fifo->name, val);
}

uint8_t voice_fifo_get(struct voice_fifo *fifo) {
    if (voice_fifo_empty(fifo)) {
        fprintf(stderr, "**fifo %s: get on empty\n", fifo->name);
        return 0;
    }
    uint8_t val = fifo->entries[fifo->tail++];
    if (fifo->tail == fifo->length)  fifo->tail = 0;
    //fprintf(stderr, "I: fifo %s: get %d\n", fifo->name, val);
    return val;
}

void voice_fifo_remove(struct voice_fifo *fifo, uint8_t val) {
    // Remove the oldest instance of val in the fifo and close up.
    //fprintf(stderr, "I: fifo %s: remove %d\n", fifo->name, val);
    uint8_t index;
    for (index = fifo->tail; index != fifo->head; index = (index + 1) % fifo->length) {
        if (fifo->entries[index] == val) {
            // Copy entries beyond the value to remove back one position.
            uint8_t new_head = (fifo->head + fifo->length - 1) % fifo->length;
            while (index != new_head) {
                uint8_t next_index = (index + 1) % fifo->length;
                fifo->entries[index] = fifo->entries[next_index];
                index = next_index;
            }
            fifo->head = new_head;
            return;
        }
    }
    fprintf(stderr, "**fifo %s: remove did not find %d (#entries=%d)\n", fifo->name, val, (fifo->head - fifo->tail) % fifo->length);
}

struct instrument_info {
    struct voice_fifo *released_voices;
    struct voice_fifo *active_voices;
    uint8_t num_voices;
    uint16_t patch_number;  // What patch this instrument is currently set to.
    int16_t bank_number;    // Optional top-7-bit word of Program, set by MIDI CC 0 (-1 if not set).
    uint32_t flags;         // Bitmask of special instrument properties (for MIDI Drums translation).
    // AMY "voice" index for each of the num_voices allocated voices.
    uint16_t amy_voices[MAX_VOICES_PER_INSTRUMENT];
    // Track which note each voice is sounding.  We use int16 so we can store PCM_PRESET *127 + midi_note
    uint16_t note_per_voice[MAX_VOICES_PER_INSTRUMENT];
    // Sustain tracking
    bool in_sustain;  // Pedal is down.
    bool pending_release[MAX_VOICES_PER_INSTRUMENT];
    bool grab_midi_notes;  // Flag to automatically play midi notes.
};

void instrument_debug(struct instrument_info *instrument) {
    fprintf(stderr, "**instrument 0x%lx num_voices %d patch %d bank %d flags %" PRIu32 " in_sustain %d grab_midi %d\n",
            (unsigned long)instrument, instrument->num_voices, instrument->patch_number, instrument->bank_number, instrument->flags,
            instrument->in_sustain, instrument->grab_midi_notes);
    for (int i = 0; i < instrument->num_voices; ++i)
        fprintf(stderr, "voice %d amy_voice %d note_per_voice %d pending_release %d\n",
                i, instrument->amy_voices[i], instrument->note_per_voice[i], instrument->pending_release[i]);
    voice_fifo_debug(instrument->active_voices);
    voice_fifo_debug(instrument->released_voices);
}

// Instrument provides note_on, note_off, all_notes_off, sustain(bool)

#define _INSTRUMENT_NO_NOTE 255
// Defined in amy.h because patches.c needs to know it.
//#define _INSTRUMENT_NO_VOICE 255

struct instrument_info *instrument_init(int num_voices, uint16_t* amy_voices, uint16_t patch_number, uint32_t flags) {
    struct instrument_info *instrument = (struct instrument_info *)malloc_caps(sizeof(struct instrument_info), amy_global.config.ram_caps_synth);
    if (num_voices <= 0 || num_voices > MAX_VOICES_PER_INSTRUMENT) {
        fprintf(stderr, "num_voices %d not within 1 .. MAX_VOICES_PER_INSTRUMENT %d\n", num_voices, MAX_VOICES_PER_INSTRUMENT);
        abort();
        return NULL;
    }
    instrument->num_voices = num_voices;
    instrument->patch_number = patch_number;
    instrument->bank_number = -1;
    instrument->flags = flags;
    instrument->in_sustain = false;
    instrument->grab_midi_notes = true;
    instrument->released_voices = voice_fifo_init(num_voices, "released");
    instrument->active_voices = voice_fifo_init(num_voices, "active");
    for (uint8_t voice = 0; voice < num_voices; ++voice) {
        instrument->amy_voices[voice] = amy_voices[voice];
        voice_fifo_put(instrument->released_voices, voice);
        instrument->note_per_voice[voice] = _INSTRUMENT_NO_NOTE;
        instrument->pending_release[voice] = false;
    }
    return instrument;
}

void instrument_free(struct instrument_info *instrument) {
    voice_fifo_free(instrument->active_voices);
    voice_fifo_free(instrument->released_voices);
    free(instrument);
}

uint16_t _instrument_get_voice(struct instrument_info *instrument) {
    if (!voice_fifo_empty(instrument->released_voices))
        return voice_fifo_get(instrument->released_voices);
    // No released voices, have to steal.
    return voice_fifo_get(instrument->active_voices);
}

uint16_t _instrument_voice_for_note(struct instrument_info *instrument, uint16_t note) {
    // Linear search through note_per_voice to see if this note is present.
    for (uint16_t voice = 0; voice < instrument->num_voices; ++voice)
        if (instrument->note_per_voice[voice] == note)
            return voice;
    return _INSTRUMENT_NO_VOICE;
}

uint16_t _instrument_voice_off(struct instrument_info *instrument, uint16_t voice) {
    instrument->note_per_voice[voice] = _INSTRUMENT_NO_NOTE;
    instrument->pending_release[voice] = false;
    voice_fifo_remove(instrument->active_voices, voice);
    voice_fifo_put(instrument->released_voices, voice);
    return instrument->amy_voices[voice];
}

uint16_t instrument_note_off(struct instrument_info *instrument, uint16_t note) {
    uint16_t voice = _instrument_voice_for_note(instrument, note);
    if (voice == _INSTRUMENT_NO_VOICE) {
        fprintf(stderr, "note off for %d does not match note on\n", note);
        //instrument_debug(instrument);
        return _INSTRUMENT_NO_VOICE;  // We could just fall through, but this is more explicit.
    }
    if (instrument->in_sustain) {
        instrument->pending_release[voice] = true;
        return _INSTRUMENT_NO_VOICE;
    }
    //fprintf(stderr, "voice %d note %d off\n", instrument->amy_voices[voice], note);
    return _instrument_voice_off(instrument, voice);
}

int _instrument_all_notes_off(struct instrument_info *instrument, uint16_t *amy_voices) {
    // Register any active voices as inactive; return those voices.
    int num_voices_turned_off = 0;
    for (uint16_t voice = 0; voice < instrument->num_voices; ++voice)
        if (instrument->note_per_voice[voice] != _INSTRUMENT_NO_NOTE) {
            //fprintf(stderr, "voice %d note %d all-off\n", instrument->amy_voices[voice], instrument->note_per_voice[voice]);
            _instrument_voice_off(instrument, voice);
            *amy_voices++ = instrument->amy_voices[voice];
            ++num_voices_turned_off;
        }
    return num_voices_turned_off;
}

uint16_t instrument_note_on(struct instrument_info *instrument, uint16_t note) {
    if (note == 0) {
        // note == 0 is for all-notes-off, it's not allowed for note-on (sorry, C-1).
        fprintf(stderr, "note-on for note 0: ignored.\n");
        return _INSTRUMENT_NO_VOICE;
    }
    uint16_t voice = _instrument_voice_for_note(instrument, note);
    if (voice == _INSTRUMENT_NO_VOICE) {
        // Not a re-onset, need to allocate a new voice.
        voice = _instrument_get_voice(instrument);
        voice_fifo_put(instrument->active_voices, voice);
    }
    instrument->note_per_voice[voice] = note;
    instrument->pending_release[voice] = false;
    //fprintf(stderr, "voice %d note %d on\n", instrument->amy_voices[voice], note);
    return instrument->amy_voices[voice];
}

////// Interface of instrument mechanism to AMY records.

//#define MAX_INSTRUMENTS 32
//struct instrument_info *instruments[MAX_INSTRUMENTS];
struct instrument_info **instruments = NULL;
int max_instruments = 0;

void instruments_free() {
    if (instruments != NULL)  {
        instruments_reset();
        free(instruments);
    }
}

void instruments_init(int num_instruments) {
    max_instruments = num_instruments;
    instruments = (struct instrument_info **)malloc_caps(max_instruments * sizeof(struct instrument_info), amy_global.config.ram_caps_synth);
    for(uint16_t i = 0; i < max_instruments; i++) {
        instruments[i]  = NULL;
    }
}

void instruments_reset() {
    for(uint16_t i = 0; i < max_instruments; i++)
        instrument_release(i);
}

void instrument_release(int instrument_number) {
    if(instrument_number < max_instruments && instruments[instrument_number]) {
        instrument_free(instruments[instrument_number]);
    }
    instruments[instrument_number] = NULL;
}

bool instrument_number_ok(int instrument_number, char *tag) {
    if (instrument_number < 0 || instrument_number >= max_instruments) {
        fprintf(stderr, "instrument_number %d is out of range 0..%d (%s)\n", instrument_number, max_instruments, tag);
        return false;
    }
    return true;
}

bool instrument_number_exists(int instrument_number, char *tag) {
    if (instrument_number_ok(instrument_number, tag)) {
        if (instruments[instrument_number])
            return true;
    }
    return false;
}

void instrument_add_new(int instrument_number, int num_voices, uint16_t *amy_voices, uint16_t patch_number, uint32_t flags) {
    if (!instrument_number_ok(instrument_number, "add_new")) return;
    if(instruments[instrument_number]) {
        instrument_free(instruments[instrument_number]);
    }
    instruments[instrument_number] = instrument_init(num_voices, amy_voices, patch_number, flags);
}

void instrument_change_number(int old_instrument_number, int new_instrument_number) {
    if (!instrument_number_exists(old_instrument_number, "change:old")) return;
    if (!instrument_number_ok(new_instrument_number, "change:new")) return;
    if (old_instrument_number == new_instrument_number)
        return;  // Degenerate change.
    if (instruments[new_instrument_number]) {
        instrument_free(instruments[new_instrument_number]);
    }
    instruments[new_instrument_number] = instruments[old_instrument_number];
    instruments[old_instrument_number] = NULL;
}


int instrument_get_voices(int instrument_number, uint16_t *amy_voices) {
    // instrument_get_voices is used to test if an instrument is set or not, so no error message if it doesn't exist, only if the number is out of range.
    if (!instrument_number_ok(instrument_number, "get_voices")) return 0;
    int num_voices = 0;
    struct instrument_info *instrument = instruments[instrument_number];
    if (instrument == NULL) {
        //fprintf(stderr, "get_voices: instrument_number %d is not defined.\n", instrument_number);
    } else {
        num_voices = instrument->num_voices;
        for (int i = 0; i < num_voices; ++i)  amy_voices[i] = instrument->amy_voices[i];
    }
    return num_voices;
}

uint16_t instrument_voice_for_note_event(int instrument_number, int note, bool is_note_off) {
    // Called from patches_event_has_voices for events including an instrument, velocity, and note (note-on/note-off).
    if (!instrument_number_exists(instrument_number, "voice_for_event")) return _INSTRUMENT_NO_VOICE;
    struct instrument_info *instrument = instruments[instrument_number];
    if (is_note_off) {
        // Note off.
        if (note == 0) {
            uint16_t amy_voices[MAX_VOICES_PER_INSTRUMENT];
            _instrument_all_notes_off(instrument, amy_voices);
            return _INSTRUMENT_NO_VOICE;
        }
        return instrument_note_off(instrument, note);
    } else {
        // Note on.
        return instrument_note_on(instrument, note);
    }
}

int instrument_all_notes_off(int instrument_number, uint16_t *amy_voices) {
    if (!instrument_number_exists(instrument_number, "all_off")) return 0;
    struct instrument_info *instrument = instruments[instrument_number];
    return _instrument_all_notes_off(instrument, amy_voices);
}

int instrument_sustain(int instrument_number, bool sustain, uint16_t *amy_voices) {
    // Will return nonzero voices if the result is to release multiple notes.
    if (!instrument_number_exists(instrument_number, "sustain")) return 0;
    struct instrument_info *instrument = instruments[instrument_number];
    if (sustain) {
        instrument->in_sustain = true;
        return 0;
    }
    // Sustain pedal released - return multiple note-offs.  Like all notes off, but a different criterion.
    instrument->in_sustain = false;
    int num_voices_turned_off = 0;
    for (uint16_t voice = 0; voice < instrument->num_voices; ++voice)
        if (instrument->pending_release[voice]) {
            //fprintf(stderr, "voice %d note %d pedal-released\n", instrument->amy_voices[voice], instrument->note_per_voice[voice]);
            _instrument_voice_off(instrument, voice);
            *amy_voices++ = instrument->amy_voices[voice];
            ++num_voices_turned_off;
        }
    return num_voices_turned_off;
}

int instrument_get_patch_number(int instrument_number) {
    if (!instrument_number_exists(instrument_number, "get_patch")) return -1;
    struct instrument_info *instrument = instruments[instrument_number];
    return instrument->patch_number;
}

uint32_t instrument_get_flags(int instrument_number) {
    if (!instrument_number_exists(instrument_number, "get_flags")) return (uint32_t)-1;
    struct instrument_info *instrument = instruments[instrument_number];
    return instrument->flags;
}

bool instrument_grab_midi_notes(int instrument_number) {
    if (!instrument_number_exists(instrument_number, "grab_midi")) return false;
    struct instrument_info *instrument = instruments[instrument_number];
    if (instrument == NULL) {
        return false;
    }
    return instrument->grab_midi_notes;
}

void instrument_set_grab_midi_notes(int instrument_number, bool grab_midi_notes) {
    if (!instrument_number_exists(instrument_number, "set_grab")) return;
    struct instrument_info *instrument = instruments[instrument_number];
    instrument->grab_midi_notes = grab_midi_notes;
}

int instrument_bank_number(int instrument_number) {
    if (!instrument_number_exists(instrument_number, "bank_number")) return -1;
    struct instrument_info *instrument = instruments[instrument_number];
    if (instrument == NULL) {
        return -1;
    }
    return instrument->bank_number;
}

void instrument_set_bank_number(int instrument_number, int bank_number) {
    if (!instrument_number_exists(instrument_number, "set_bank_number")) return;
    struct instrument_info *instrument = instruments[instrument_number];
    instrument->bank_number = bank_number;
}
