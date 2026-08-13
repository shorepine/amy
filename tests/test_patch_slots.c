// Tests for the memory-patch slot lifecycle (patch_string storage).
//
// Every patch_string without an explicit patch_number used to burn a
// fresh slot (next_user_patch_index only ever grew), so
// max_memory_patches sends — 32 reconfigures of one synth, or a few
// configure/release cycles of a multi-channel app — emptied the pool
// for the rest of the session. And once it was empty the refusal was
// not the end of it: patches_store_patch dropped the store but the
// event still carried the refused number into patches_load_patch,
// which indexed the 32-slot tables with it unchecked and walked a
// garbage delta pointer — a segfault on the desktop, a Guru Meditation
// on the device.
//
// Also pins the release-path spam: deleting an instrument
// (num_voices=0) unsets e->synth so the rest of the event is not
// forwarded, and patches_voices_for_event then read the unset sentinel
// into instrument_get_num_voices — "instrument_number 255 is out of
// range" once per released synth, and on a device stderr is a blocking
// UART line inside the audio path.
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

extern uint32_t max_num_memory_patches;
extern struct delta **memory_patch_deltas;
extern uint16_t *memory_patch_oscs;
extern int instrument_get_patch_number(int instrument_number);
extern int instrument_get_num_voices(int instrument_number, uint16_t *amy_voices);

#define FIRST_USER 1024

static void render_a_bit(void) {
    for (int i = 0; i < 16; ++i) amy_simple_fill_buffer();
}

static void send(const char *m) {
    amy_add_message((char *)m);
    render_a_bit();
}

static int used_slots(void) {
    int n = 0;
    for (uint32_t i = 0; i < max_num_memory_patches; ++i)
        if (memory_patch_deltas[i] != NULL || memory_patch_oscs[i] != 0) ++n;
    return n;
}

static void test_reconfigure_reuses_slot(void) {
    printf("a reconfigure reuses the instrument's own slot\n");
    send("i17iv1uv0w7p3Z");
    int first = instrument_get_patch_number(17);
    CHECK(first >= FIRST_USER && first < FIRST_USER + (int)max_num_memory_patches,
          "first store lands in the user range (%d)", first);
    int before = used_slots();
    for (int i = 0; i < 40; ++i) {
        char m[64];
        snprintf(m, sizeof(m), "i17iv1uv0w7p%dZ", i % 5);
        send(m);
    }
    CHECK(instrument_get_patch_number(17) == first,
          "40 reconfigures still on patch %d", first);
    CHECK(used_slots() == before, "and no extra slots burned (%d used)",
          used_slots());
}

static void test_release_frees_auto_slot(void) {
    printf("releasing an instrument frees its auto-assigned slot\n");
    send("i17iv0Z");          // release the previous test's holding first
    int before = used_slots();
    send("i18iv1uv0w7p3Z");
    CHECK(used_slots() == before + 1, "config took one slot");
    send("i18iv0Z");
    CHECK(used_slots() == before, "release gave it back");
    // 40 configure/release cycles across synths: the pool never runs dry.
    for (int i = 0; i < 40; ++i) {
        char m[64];
        int s = 17 + (i % 8);
        snprintf(m, sizeof(m), "i%div1uv0w7p%dZ", s, i % 5);
        send(m);
        int p = instrument_get_patch_number(s);
        if (!(p >= FIRST_USER && p < FIRST_USER + (int)max_num_memory_patches)) {
            CHECK(0, "cycle %d: synth %d got patch %d", i, s, p);
            return;
        }
        snprintf(m, sizeof(m), "i%div0Z", s);
        send(m);
    }
    CHECK(used_slots() == before, "40 configure/release cycles leaked nothing");
}

static void test_explicit_slot_survives_release(void) {
    printf("an explicitly numbered patch is not freed behind the caller\n");
    send("K1030uv0w7p3Zv1w7p4Z");            // store 2-osc patch as 1030
    CHECK(memory_patch_oscs[1030 - FIRST_USER] == 2, "stored 2 oscs at 1030");
    send("K1030i19iv1Z");                    // load it on synth 19
    CHECK(instrument_get_patch_number(19) == 1030, "synth 19 loaded 1030");
    send("i19iv0Z");                         // release the instrument
    CHECK(memory_patch_oscs[1030 - FIRST_USER] == 2,
          "slot 1030 still stored after release");
    // A re-store REPLACES rather than appends.
    send("K1030uv0w7p3Z");                   // now a 1-osc patch
    CHECK(memory_patch_oscs[1030 - FIRST_USER] == 1,
          "re-store replaced the slot (1 osc, not 3)");
}

static void test_pool_dry_is_safe(void) {
    printf("a dry pool refuses the store and the load does not crash\n");
    // Fill every slot with explicit stores.
    for (uint32_t i = 0; i < max_num_memory_patches; ++i) {
        char m[64];
        snprintf(m, sizeof(m), "K%duv0w7p3Z", FIRST_USER + i);
        send(m);
    }
    CHECK(used_slots() == (int)max_num_memory_patches, "pool full (%d slots)",
          used_slots());
    // An auto-assign now has nowhere to go: the store is refused and the
    // load of the refused number must be IGNORED, not a wild index.
    send("i20iv1uv0w7p3Z");
    CHECK(instrument_get_num_voices(20, NULL) == 0,
          "synth 20 not configured, and no crash");
}

static void test_release_is_quiet(void) {
    printf("releasing a synth prints nothing (the 255 spam)\n");
    // The spam went to stderr; point stderr at a file and look.
    fflush(stderr);
    FILE *redir = freopen("test_patch_slots.stderr.tmp", "w", stderr);
    send("K7i21if8iv2y1Z");   // ROM patch config, the tulip5 shape
    send("n60l0.8i21Z");
    send("n0l0i21Z");
    send("i21iv0Z");          // the release that used to print
    fflush(stderr);
    char buf[4096] = {0};
    int found = 0;
    if (redir) {
        FILE *f = fopen("test_patch_slots.stderr.tmp", "r");
        if (f) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = 0;
            fclose(f);
        }
        found = strstr(buf, "out of range") != NULL;
    }
    freopen("/dev/stderr", "w", stderr);
    remove("test_patch_slots.stderr.tmp");
    CHECK(!found, "no 'out of range' on release%s%s",
          found ? ": " : "", found ? buf : "");
}

// examples.o wants this from amy-example.c; every ctest stubs it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    render_a_bit();

    test_reconfigure_reuses_slot();
    test_release_frees_auto_slot();
    test_explicit_slot_survives_release();
    test_release_is_quiet();
    test_pool_dry_is_safe();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
