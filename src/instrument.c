// instrument.c
//
// An instrument is a set of voices with voice-allocation logic.
// The user calls instrument_note_on(), and this logic chooses one of the
// preallocated voices to play the note on.
//
// This is a replacement in C for the functionality in
// tulip/shared/py/synth.py.

#include "amy.h"

typedef uint8_t bool;

#define MAX_VOICES_PER_INSTRUMENT 16

#define INSTRUMENT_RAM_CAPS SYNTH_RAM_CAPS

struct voice_fifo {
    char *name;
    uint8_t length;
    uint8_t entries[MAX_VOICES_PER_INSTRUMENT + 1];  // fifo has one extra element to distinguish empty and full.
    uint8_t head;
    uint8_t tail;
};

// Fifo must provide init, put, get, remove, empty

struct voice_fifo *voice_fifo_init(int size, char *name) {
    if (size <=0 || size > MAX_VOICES_PER_INSTRUMENT) {
        fprintf(stderr, "init_voice_fifo: size %d value error (max size is %d)\n", size, MAX_VOICES_PER_INSTRUMENT);
        return NULL;
    }
    struct voice_fifo *result = (struct voice_fifo *)malloc_caps(sizeof(struct voice_fifo), INSTRUMENT_RAM_CAPS);
    result->head = 0;
    result->tail = 0;
    result->length = MAX_VOICES_PER_INSTRUMENT + 1;  // One more than max size.  fixed in this implementation.
    result->name = name;
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
    fprintf(stderr, "I: fifo %s: put %d\n", fifo->name, val);
}

uint8_t voice_fifo_get(struct voice_fifo *fifo) {
    if (voice_fifo_empty(fifo)) {
        fprintf(stderr, "**fifo %s: get on empty\n", fifo->name);
        return 0;
    }
    uint8_t val = fifo->entries[fifo->tail++];
    if (fifo->tail == fifo->length)  fifo->tail = 0;
    fprintf(stderr, "I: fifo %s: get %d\n", fifo->name, val);
    return val;
}

void voice_fifo_remove(struct voice_fifo *fifo, uint8_t val) {
    // Remove the oldest instance of val in the fifo and close up.
    fprintf(stderr, "I: fifo %s: remove %d\n", fifo->name, val);
    uint8_t index;
    for (index = fifo->tail; index != fifo->head; index = (index + 1) % fifo->length) {
        if (fifo->entries[index] == val) {
            // Copy entries beyond the value to remove back one position.
            uint8_t new_head = (fifo->head - 1) % fifo->length;
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
    // AMY "voice" index for each of the num_voices allocated voices.
    uint8_t amy_voices[MAX_VOICES_PER_INSTRUMENT];
    // Track which note each voice is sounding.
    uint8_t note_per_voice[MAX_VOICES_PER_INSTRUMENT];
};


// Stubs for testing.
void send_note_on(uint8_t amy_voice, uint8_t note, uint8_t velocity) {
    fprintf(stderr, "note on voice=%d note=%d velocity=%d\n", amy_voice, note, velocity);
}

void send_note_off(uint8_t amy_voice) {
    fprintf(stderr, "note off voice=%d\n", amy_voice);
}

// Instrument provides note_on, note_off, all_notes_off, sustain(bool)

#define _INSTRUMENT_NO_NOTE 255
#define _INSTRUMENT_NO_VOICE 255

struct instrument_info *instrument_init(int num_voices, uint8_t* amy_voices) {
    struct instrument_info *instrument = (struct instrument_info *)malloc_caps(sizeof(struct instrument_info), INSTRUMENT_RAM_CAPS);
    instrument->num_voices = num_voices;
    instrument->released_voices = voice_fifo_init(num_voices, "released");
    instrument->active_voices = voice_fifo_init(num_voices, "active");
    for (uint8_t voice = 0; voice < num_voices; ++voice) {
        instrument->amy_voices[voice] = amy_voices[voice];
        voice_fifo_put(instrument->released_voices, voice);
        instrument->note_per_voice[voice] = _INSTRUMENT_NO_NOTE;
    }
    return instrument;
}

void instrument_info_free(struct instrument_info *instrument) {
    voice_fifo_free(instrument->active_voices);
    voice_fifo_free(instrument->released_voices);
    free(instrument);
}

uint8_t _instrument_get_voice(struct instrument_info *instrument) {
    if (!voice_fifo_empty(instrument->released_voices))
        return voice_fifo_get(instrument->released_voices);
    // No released voices, have to steal.
    return voice_fifo_get(instrument->active_voices);
}

uint8_t _instrument_voice_for_note(struct instrument_info *instrument, uint8_t note) {
    // Linear search through note_per_voice to see if this note is present.
    for (uint8_t voice = 0; voice < instrument->num_voices; ++voice)
        if (instrument->note_per_voice[voice] == note)
            return voice;
    return _INSTRUMENT_NO_VOICE;
}

void instrument_note_off(struct instrument_info *instrument, uint8_t note) {
    uint8_t voice = _instrument_voice_for_note(instrument, note);
    if (voice == _INSTRUMENT_NO_VOICE) {
        fprintf(stderr, "note off for %d does not match note on\n", note);
        return;
    }
    instrument->note_per_voice[voice] = _INSTRUMENT_NO_NOTE;
    voice_fifo_remove(instrument->active_voices, voice);
    voice_fifo_put(instrument->released_voices, voice);
    return send_note_off(instrument->amy_voices[voice]);
}

void instrument_note_on(struct instrument_info *instrument, uint8_t note, uint8_t velocity) {
    if (velocity == 0)
        return instrument_note_off(instrument, note);
    uint8_t voice = _instrument_voice_for_note(instrument, note);
    if (voice == _INSTRUMENT_NO_VOICE) {
        // Not a re-onset, need to allocate a new voice.
        voice = _instrument_get_voice(instrument);
        voice_fifo_put(instrument->active_voices, voice);
    }
    instrument->note_per_voice[voice] = note;
    return send_note_on(instrument->amy_voices[voice], note, velocity);
}

// test exercise.

void instrument_test(void) {
    uint8_t amy_voices[4] = {101, 102, 103, 104};
    struct instrument_info *instrument = instrument_init(4, amy_voices);

    // 5 non-overlapping notes
    for (int i = 0; i < 5; ++i) {
        instrument_note_on(instrument, 48 + i, 10);
        instrument_note_off(instrument, 48 + i);
    }
    // 5 overlapping notes
    instrument_note_on(instrument, 48, 10);
    for (int i = 0; i < 5; ++i) {
        instrument_note_on(instrument, 48 + i + 1, 10);
        instrument_note_off(instrument, 48 + i);
    }
    instrument_note_off(instrument, 48 + 5);

    // 1 note held, alternating notes over the top.
    instrument_note_on(instrument, 48, 10);
    for (int i = 0; i < 5; ++i) {
        instrument_note_on(instrument, 48 + i + 1, 10);
        instrument_note_off(instrument, 48 + i + 1);
    }
    instrument_note_off(instrument, 48);
}
