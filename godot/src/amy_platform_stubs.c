// Stub implementations for AMY platform/hardware functions.
// We don't need I2S or MIDI hardware - Godot handles all audio.
// miniaudio stubs are no longer needed thanks to -DAMY_NO_MINIAUDIO.
// amy_midi.c is excluded from the build to avoid conflicts, so we
// stub the MIDI functions here.

#include <stdint.h>
#include <stddef.h>

// --- i2s.c stubs ---
void amy_platform_init(void) {}
void amy_platform_deinit(void) {}
void amy_update_tasks(void) {}
int16_t *amy_render_audio(void) { return (int16_t *)0; }
size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) {
    (void)buffer; (void)nbytes; return nbytes;
}

// --- amy_midi.c stubs ---
void stop_midi(void) {}
void amy_send_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    (void)channel; (void)note; (void)velocity;
}
void amy_send_midi_note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
    (void)channel; (void)note; (void)velocity;
}
