// Stub implementations for AMY platform/hardware functions.
// We don't need I2S or MIDI hardware - Godot handles all audio.
// miniaudio stubs are no longer needed thanks to -DAMY_NO_MINIAUDIO.

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

// --- MIDI stubs (amy_midi.c / macos_midi.m) ---
void midi_out(uint8_t *bytes, uint16_t len) { (void)bytes; (void)len; }
void midi_local(uint8_t *bytes, uint16_t len) { (void)bytes; (void)len; }
void run_midi(void) {}
void stop_midi(void) {}
void *run_midi_macos(void *vargp) { (void)vargp; return (void *)0; }
void check_tusb_midi(void) {}
void init_tusb_midi(void) {}
