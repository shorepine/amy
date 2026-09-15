// Changing filter_type on a sounding osc must not ring the new filter.
//
// The filter kernels disagree about what filter_delay holds (raw vs b0-scaled
// input history, four vs six words, an allpass chain for the phaser), so the
// state one kernel leaves behind is an impulse into the next; without a reset
// a switch into LPF24 rings tens of dB above the note.
// The check: after a switch, the loudest of the next blocks stays within a
// small factor of the loudest block the destination filter produces when the
// same note starts into it from rest.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

void delay_ms(uint32_t ms) { (void)ms; }

static void restart(void) {
    amy_stop();
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
}

// Peak |sample| of one rendered block, full scale = 1.
static float render_peak(void) {
    int16_t *buf = amy_simple_fill_buffer();
    float pk = 0;
    for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i) {
        float v = buf[i] / 32768.0f;
        if (v < 0) v = -v;
        if (v > pk) pk = v;
    }
    return pk;
}

// Saw at 60 Hz through a resonant 200 Hz filter of the given type, quiet
// enough to leave headroom for any burst to show.
static void start_note(int filter_type) {
    char msg[64];
    snprintf(msg, sizeof msg, "v0w2G%dF200R4.0f60l0.05", filter_type);
    amy_add_message(msg);
}

static float settle(int blocks) {
    float pk = 0;
    for (int b = 0; b < blocks; ++b) pk = render_peak();
    return pk;
}

static float peak_after_switch(int from, int to) {
    restart();
    start_note(from);
    settle(60);
    char msg[16];
    snprintf(msg, sizeof msg, "v0G%d", to);
    amy_add_message(msg);
    float worst = 0;
    for (int b = 0; b < 8; ++b) {
        float pk = render_peak();
        if (pk > worst) worst = pk;
    }
    return worst;
}

// Loudest block over the same window when the note starts into the filter.
static float cold_start_peak(int type) {
    restart();
    start_note(type);
    float worst = 0;
    for (int b = 0; b < 68; ++b) {
        float pk = render_peak();
        if (pk > worst) worst = pk;
    }
    return worst;
}

static void test_switch(int from, int to) {
    float ref = cold_start_peak(to);
    float got = peak_after_switch(from, to);
    CHECK(got <= 2.0f * ref, "%d -> %d: peak after switch %.4f, cold start %.4f", from, to, got, ref);
}

int main(void) {
    int types[] = { FILTER_LPF, FILTER_BPF, FILTER_HPF, FILTER_LPF24, FILTER_NOTCH, FILTER_PHASER };
    int n = sizeof types / sizeof types[0];
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j) test_switch(types[i], types[j]);
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
