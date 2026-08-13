// Tests that reading a synth's configuration back never dereferences an
// oscillator that was never allocated.
//
// synth[] is an array of POINTERS, NULL until ensure_osc_allocd() allocates
// one, and a voice can legitimately own oscs that nothing ever touched -- a
// synth configured with oscs_per_voice= and no patch reserves its oscs
// (osc_to_voice / voice_to_base_osc) without allocating a single synthinfo,
// and a patch_string only allocates the oscs it actually mentions.
//
// set_event_for_osc() read synth[osc]->max_num_breakpoints on its first line,
// with no check. So amy_get_synth_commands() and amy_dump_state() -- which
// both walk every osc of every voice through it -- segfaulted on any such
// synth. Not an obscure shape: `oscs_per_voice=N` with no patch is the plain
// way to make a synth, and it crashed 100% of the time.
//
// Reading an unallocated osc is not an error. It is at its defaults, so there
// is simply nothing to emit for it.
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

static void render_a_bit(void) {
    for (int i = 0; i < 16; ++i) amy_simple_fill_buffer();
}

static void send(const char *m) {
    amy_add_message((char *)m);
    render_a_bit();
}

// Walk the whole generator the way amy_get_synth_commands() does. Returns the
// number of command strings yielded. Before the guard this did not return at
// all -- it took the process down.
static int read_back(uint8_t instr) {
    char buf[MAX_MESSAGE_LEN];
    void *state = NULL;
    int n = 0;
    do {
        state = yield_synth_commands(instr, buf, sizeof(buf), true, state);
        if (buf[0] != '\0') ++n;
    } while (state);
    return n;
}

static void test_oscs_per_voice_no_patch(void) {
    printf("a synth made with oscs_per_voice and no patch can be read back\n");
    send("i20iv1in1Z");
    CHECK(read_back(20) > 0, "oscs_per_voice=1, 1 voice yields commands");
    send("i21iv2in4Z");
    CHECK(read_back(21) > 0, "oscs_per_voice=4, 2 voices yields commands");
}

static void test_patch_string_partial_oscs(void) {
    printf("a patch_string that leaves oscs untouched can be read back\n");
    // Two oscs named, but the voice is sized by the patch, and nothing
    // allocates the oscs the patch never mentions.
    send("i22iv1uv0w2f220Zv1w0f330Z");
    CHECK(read_back(22) > 0, "2-osc patch_string yields commands");
}

static void test_builtin_still_works(void) {
    printf("the previously-working shapes still work\n");
    send("i23iv1K0Z");
    CHECK(read_back(23) > 0, "built-in patch 0 yields commands");
}

static void test_undefined_synth_is_empty(void) {
    printf("an undefined synth yields nothing, quietly\n");
    CHECK(read_back(60) == 0, "no commands for a synth that does not exist");
}

// AMY calls these; the test binary has to provide them.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    render_a_bit();

    test_oscs_per_voice_no_patch();
    test_patch_string_partial_oscs();
    test_builtin_still_works();
    test_undefined_synth_is_empty();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
