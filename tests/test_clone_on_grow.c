// Tests that changing a live synth's num_voices rebuilds its voices from the
// synth as it stands, not from the patch it was born with.
//
// Growing a synth releases every voice and re-initializes them all, and the
// only source it had for that was the instrument's stored patch number. So
// every osc-level edit made since the patch was loaded was silently thrown
// away -- including voice 0's, which had been playing the edited sound right
// up until the moment you asked for more of it.
//
// And a synth with no patch to reload could not grow at all: `oscs_per_voice=`
// with no patch leaves the instrument's patch number unset, which fell through
// to the user-patch branch as 65535, printed "out of range" and returned --
// after the voices had already been reallocated, so the synth was left with
// voices reserved that the instrument did not know about.
//
// Both are fixed by snapshotting the live voice before the reallocation and
// replaying that into every new voice.
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

extern uint16_t *voice_to_base_osc;
extern int instrument_get_num_voices(int instrument_number, uint16_t *amy_voices);
extern void set_event_for_osc(int base_osc, int rel_osc, struct amy_event *event);
extern int32_t delta_num_free(void);

static void render_a_bit(void) {
    for (int i = 0; i < 16; ++i) amy_simple_fill_buffer();
}

static void send(const char *m) {
    amy_add_message((char *)m);
    render_a_bit();
}

// Read back one osc of one voice of a synth, the same way the synth readout
// does. Returns false if that voice/osc isn't there at all.
static bool read_osc(uint8_t instr, int voice_index, int rel_osc, amy_event *e) {
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    int num_voices = instrument_get_num_voices(instr, voices);
    if (voice_index >= num_voices)  return false;
    uint16_t base_osc = voice_to_base_osc[voices[voice_index]];
    if (AMY_IS_UNSET(base_osc))  return false;
    amy_clear_event(e);
    set_event_for_osc(base_osc, rel_osc, e);
    return true;
}

static bool osc_wave_is(uint8_t instr, int voice_index, int rel_osc, int wave) {
    amy_event e;
    if (!read_osc(instr, voice_index, rel_osc, &e))  return false;
    return AMY_IS_SET(e.wave) && (int)e.wave == wave;
}

static bool osc_freq_is(uint8_t instr, int voice_index, int rel_osc, float freq) {
    amy_event e;
    if (!read_osc(instr, voice_index, rel_osc, &e))  return false;
    float got = e.freq_coefs[COEF_CONST];
    return AMY_IS_SET(got) && got > freq - 1.0f && got < freq + 1.0f;
}

static void test_edits_survive_growth(void) {
    printf("growing a synth keeps the edits, not the patch it started from\n");
    // 2-osc patch, one voice: both oscs are sine (w0).
    send("i24iv1uv0w0f220Zv1w0f330Z");
    // Now edit osc 1 of the live synth to a saw. This is the whole point of a
    // patch_string synth: you set it up and then you tweak it.
    send("i24v1w2Z");
    CHECK(osc_wave_is(24, 0, 1, 2), "the edit took (osc 1 is wave 2)");

    send("i24iv3Z");   // grow to 3 voices
    CHECK(instrument_get_num_voices(24, NULL) == 3, "synth grew to 3 voices");
    bool all_edited = true, all_freq = true;
    for (int v = 0; v < 3; ++v) {
        if (!osc_wave_is(24, v, 1, 2))  all_edited = false;
        if (!osc_freq_is(24, v, 0, 220.f))  all_freq = false;
    }
    CHECK(all_edited, "every voice has the EDITED osc 1 (wave 2, not the patch's 0)");
    CHECK(all_freq, "and the un-edited osc 0 still came along (f220)");

    // Shrinking is the same path, and voice 0 must survive it intact.
    send("i24iv1Z");
    CHECK(instrument_get_num_voices(24, NULL) == 1, "shrank back to 1 voice");
    CHECK(osc_wave_is(24, 0, 1, 2), "the edit survived the shrink too");
}

static void test_growth_without_a_patch(void) {
    printf("a synth configured osc-by-osc can grow (it has no patch to reload)\n");
    send("i25iv1in2Z");        // 2 oscs per voice, no patch at all
    // Not 440: that is the default logfreq, and the readout only reports
    // non-default values, so a 440 would prove nothing either way.
    send("i25v0w2f550Z");      // configure it by hand
    CHECK(osc_wave_is(25, 0, 0, 2) && osc_freq_is(25, 0, 0, 550.f),
          "hand-configured osc 0 is wave 2, f550");

    send("i25iv2Z");
    CHECK(instrument_get_num_voices(25, NULL) == 2, "grew to 2 voices");
    CHECK(osc_wave_is(25, 1, 0, 2) && osc_freq_is(25, 1, 0, 550.f),
          "the new voice got the same hand-made config");
}

static void test_explicit_patch_still_reloads(void) {
    printf("naming a patch still reloads it, edits and all\n");
    // Patch wave 3, edited to wave 2, so neither value is the default and
    // "the edit lost" and "nothing happened" can't be confused.
    send("i26iv1uv0w3f220Z");
    send("i26v0w2Z");
    CHECK(osc_wave_is(26, 0, 0, 2), "edited to wave 2");
    // Re-stating the patch_string is an explicit reload: the edit goes away.
    send("i26iv2uv0w3f220Z");
    CHECK(instrument_get_num_voices(26, NULL) == 2, "reloaded onto 2 voices");
    CHECK(osc_wave_is(26, 0, 0, 3),
          "an explicit patch load overrides the edit (back to the patch's wave 3)");
}

static float osc_resonance(uint8_t instr, int voice_index) {
    amy_event e;
    if (!read_osc(instr, voice_index, 0, &e))  return -1.f;
    return AMY_IS_SET(e.resonance) ? e.resonance : -1.f;
}

static void test_rom_patch_growth(void) {
    printf("a ROM-patch synth grows without reloading the ROM patch\n");
    send("i27iv1K0Z");
    // Edit away from whatever the ROM patch set, by enough that the patch's
    // own value can't be mistaken for the edit.
    float from_patch = osc_resonance(27, 0);
    float edited = (from_patch > 0.f) ? from_patch + 1.5f : 2.0f;
    char m[64];
    snprintf(m, sizeof(m), "i27v0R%.3fZ", edited);
    send(m);
    CHECK(osc_resonance(27, 0) > edited - 0.05f,
          "resonance edit took (%.2f, patch had %.2f)", osc_resonance(27, 0), from_patch);
    send("i27iv4Z");
    CHECK(instrument_get_num_voices(27, NULL) == 4, "grew to 4 voices");
    bool all_res = true;
    for (int v = 0; v < 4; ++v) {
        float r = osc_resonance(27, v);
        if (!(r > edited - 0.05f && r < edited + 0.05f))  all_res = false;
    }
    CHECK(all_res, "every voice kept the edited resonance (%.2f), not the patch's (%.2f)",
          osc_resonance(27, 3), from_patch);
}

static void test_growth_does_not_leak_deltas(void) {
    printf("the voice snapshot is handed back to the delta pool\n");
    send("i28iv1uv0w0f220Zv1w2f330Zv2w3f440Z");
    send("i28iv2Z");
    render_a_bit();
    int32_t before = delta_num_free();
    for (int i = 0; i < 20; ++i) {
        send((i & 1) ? "i28iv4Z" : "i28iv2Z");
    }
    render_a_bit();
    CHECK(delta_num_free() == before,
          "20 grow/shrink cycles left the pool where it started (%" PRId32 " vs %" PRId32 ")",
          delta_num_free(), before);
}

// AMY calls these; the test binary has to provide them.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    render_a_bit();

    test_edits_survive_growth();
    test_growth_without_a_patch();
    test_explicit_patch_still_reloads();
    test_rom_patch_growth();
    test_growth_does_not_leak_deltas();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
