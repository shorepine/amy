// amy_sine.c - simplest possible AMY example for Windows
// Plays a 440Hz sine wave through your default audio device.

#include "amy.h"
#include "libminiaudio-audio.h"

// Required by examples.c but not used in this simple app.
#ifdef _WIN32
#include <windows.h>
void delay_ms(uint32_t ms) { Sleep(ms); }
#else
#include <unistd.h>
void delay_ms(uint32_t ms) { usleep(ms * 1000); }
#endif

int main(void) {
    amy_config_t config = amy_default_config();
    config.audio = AMY_AUDIO_IS_MINIAUDIO;
    config.features.default_synths = 1;
    amy_start(config);

    // Play a 440Hz sine wave
    amy_event e = amy_default_event();
    e.wave = SINE;
    e.freq_coefs[COEF_CONST] = 440.0f;
    e.amp_coefs[COEF_CONST] = 1.0f;
    e.velocity = 1.0f;
    amy_add_event(&e);

    printf("Playing 440Hz sine wave. Press Enter to quit.\n");
    getchar();

    amy_stop();
    return 0;
}
