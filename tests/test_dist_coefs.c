// Drive and mix are coef-routed, so the modulation sources have to reach the
// shaper and land on the documented scale: the CONST coef is linear drive, the
// rest are octaves of it. These checks pin both halves of that contract by
// rendering equivalent scenes two ways and comparing energy.
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

void delay_ms(uint32_t ms) { (void)ms; }

// Render one scene from a clean engine and return the RMS of its output.
static double scene_rms(const char *const *msgs, int n_msgs) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    amy_reset_sysclock();
    for (int i = 0; i < n_msgs; ++i) amy_add_message((char *)msgs[i]);

    double sumsq = 0;
    long n = 0;
    for (int b = 0; b < 40; ++b) {
        int16_t *buf = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i) {
            sumsq += (double)buf[i] * (double)buf[i];
            ++n;
        }
    }
    amy_stop();
    return sqrt(sumsq / (double)n);
}

// Two scenes are "the same sound" if their energies agree to within a fraction
// of a dB; the shaper is deterministic, so anything routed differently moves
// the number far more than this.
static int within_db(double a, double b, double tol_db) {
    if (a <= 0 || b <= 0) return 0;
    return fabs(20.0 * log10(a / b)) < tol_db;
}

int main(void) {
    alarm(60);

    // Drive 4 stated directly, versus drive 1 with a velocity coef of 2
    // octaves at full velocity: 1 * 2^2 = 4. Same sound if VEL reaches drive
    // and the modulation coefs really are octaves.
    const char *direct[] = {"v0w3f110l1GC1GD4Z"};
    const char *via_vel[] = {"v0w3f110l1GC1GD1,,2Z"};
    double a = scene_rms(direct, 1);
    double b = scene_rms(via_vel, 1);
    printf("velocity reaches drive on an octave scale\n");
    CHECK(a > 0, "direct drive 4 renders (rms %.1f)", a);
    CHECK(within_db(a, b, 0.5), "VEL coef 2 at full velocity == drive 4 (rms %.1f vs %.1f)", a, b);

    // A different octave count must NOT match, or the check above would pass
    // on any wiring that ignores the coef entirely.
    const char *via_vel_1oct[] = {"v0w3f110l1GC1GD1,,1Z"};
    double b1 = scene_rms(via_vel_1oct, 1);
    printf("the octave scale is actually read\n");
    CHECK(!within_db(a, b1, 0.5), "VEL coef 1 differs from drive 4 (rms %.1f vs %.1f)", b1, a);

    // Mix is coef-routed too, and mix 0 is the bypass: identical to no stage.
    const char *dry[] = {"v0w3f110l1Z"};
    const char *mix_zero[] = {"v0w3f110l1GC1GD8GM0Z"};
    double d = scene_rms(dry, 1);
    double m = scene_rms(mix_zero, 1);
    printf("mix 0 bypasses the stage\n");
    CHECK(within_db(d, m, 0.01), "heavy drive at mix 0 == no distortion (rms %.1f vs %.1f)", m, d);

    // And mix 1 at the same drive must not, for the same reason as above.
    const char *mix_one[] = {"v0w3f110l1GC1GD8GM1Z"};
    double m1 = scene_rms(mix_one, 1);
    printf("mix 1 does not\n");
    CHECK(!within_db(d, m1, 0.5), "drive 8 wet differs from dry (rms %.1f vs %.1f)", m1, d);

    printf(failures ? "FAILURES: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
