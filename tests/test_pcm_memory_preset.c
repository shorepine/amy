// Tests pcm_get_memory_preset(), the read-back an embedder uses to save a
// load_sample / start_sample preset to disk.
//
// It must hand back the preset's real shape -- frame count, channel count,
// sample rate, tuning -- since a mono load_sample preset and a stereo
// start_sample recording are both just int16 RAM, and a caller that guesses
// the channel count reads past the end of a mono buffer.  And it must NOT
// fall back to a ROM preset the way pcm_get_sample_ram_for_preset() does: an
// unknown preset number is "nothing to save", not "ROM preset 0".
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

static void test_mono_load(void) {
    printf("mono pcm_load preset reads back its shape and data\n");
    int16_t *ram = pcm_load(301, 1000, 22050, 1, 60.04f, 0, 1000);
    for (int i = 0; i < 1000; ++i) ram[i] = (int16_t)(i * 7);
    uint32_t frames = 0, sr = 0; uint8_t ch = 0; float note = 0;
    const int16_t *got = pcm_get_memory_preset(301, &frames, &ch, &sr, &note);
    CHECK(got == ram, "returns the preset's sample RAM");
    CHECK(frames == 1000, "frames = 1000 (got %u)", (unsigned)frames);
    CHECK(ch == 1, "channels = 1 (got %u)", ch);
    CHECK(sr == 22050, "samplerate = 22050 (got %u)", (unsigned)sr);
    CHECK(note > 60.03f && note < 60.05f, "midinote = 60.04 (got %f)", note);
    CHECK(got && got[999] == (int16_t)(999 * 7), "data intact");
}

static void test_stereo_record(void) {
    printf("start_sample preset is stereo at AMY's rate\n");
    char start[] = "zS302,1,4800,60,0,0Z";
    amy_add_message(start);
    for (int i = 0; i < 8; ++i) amy_simple_fill_buffer();
    uint32_t frames = 0, sr = 0; uint8_t ch = 0;
    const int16_t *got = pcm_get_memory_preset(302, &frames, &ch, &sr, NULL);
    CHECK(got != NULL, "recording preset found");
    CHECK(frames == 4800, "frames = max_frames 4800 (got %u)", (unsigned)frames);
    CHECK(ch == 2, "channels = 2 (got %u)", ch);
    CHECK(sr == AMY_SAMPLE_RATE, "samplerate = AMY_SAMPLE_RATE (got %u)", (unsigned)sr);
    char stop[] = "zO1Z";
    amy_add_message(stop);
    amy_simple_fill_buffer();
}

static void test_no_rom_fallback(void) {
    printf("non-memory presets return NULL\n");
    uint32_t frames = 12345;
    CHECK(pcm_get_memory_preset(0, &frames, NULL, NULL, NULL) == NULL, "ROM preset 0 -> NULL");
    CHECK(frames == 12345, "out-params untouched on miss");
    CHECK(pcm_get_memory_preset(4000, NULL, NULL, NULL, NULL) == NULL, "unknown preset -> NULL");
    pcm_unload_preset(301);
    CHECK(pcm_get_memory_preset(301, NULL, NULL, NULL, NULL) == NULL, "unloaded preset -> NULL");
}

// AMY calls these; the test binary has to provide them.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    for (int i = 0; i < 4; ++i) amy_simple_fill_buffer();

    test_mono_load();
    test_stereo_record();
    test_no_rom_fallback();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
