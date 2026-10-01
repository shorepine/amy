// LockTiming - how long a patch load holds the render lock (claude/render-lock),
// and what that does to the render task, on an AMYboard (or any ESP32 board).
//
// Build with -DAMY_LOCK_TIMING (and -DARDUINO_SPEEDTEST for RENDER_LOAD lines):
//   arduino-cli compile --fqbn esp32:esp32:amyboard --library <amy> \
//     --build-property "compiler.c.extra_flags=-DAMY_LOCK_TIMING -DARDUINO_SPEEDTEST" \
//     tools/arduino_loadsweep/LockTiming
//
// Background: synth 1 holds a Juno chord so the render is busy. Then synth 2 is
// reloaded through a fixed cycle of loads, one per second, in two stages:
//   stage 1  6 Juno notes in the background
//   stage 2  plus a 4-note DX7 chord on synth 3, for a much heavier render
// The loads: Juno 1 (6 voices), DX7 130 (6 voices), grow to 8 voices with no
// patch (clone on grow: snapshots the live voice), drum kit 258 (one 38-osc
// voice; its patch string loads a patch itself, so it nests).
//
// Serial output (stderr):
//   LOADLOCK ...    one per load (and per nested load): wait_us = blocked on a
//                   render in progress; flush_us = the part main also runs;
//                   hold_us = what a render that wants the lock waits for.
//   RENDERWAIT ...  every 500 ms from the fill task: wait_max_us (longest the
//                   render waited on a load), busy_max_us (longest block, vs
//                   block_us), i2s_block_min_us (~0 = DMA ring ran dry).
//   RENDER_LOAD ... the usual smoothed render time (ARDUINO_SPEEDTEST).

#include <AMY-Arduino.h>

#ifndef CYCLES_PER_STAGE
#define CYCLES_PER_STAGE (5)
#endif
#ifndef LOAD_GAP_MS
#define LOAD_GAP_MS (1000)
#endif

static const char *loads[] = {
  "i2iv6K1Z",     // Juno, 6 voices
  "i2iv6K130Z",   // DX7, 6 voices
  "i2iv8Z",       // grow to 8 voices, no patch: clone on grow
  "i2iv1K258Z",   // drum kit: 1 voice of 38 oscs, nested load
};
static const int num_loads = sizeof(loads) / sizeof(loads[0]);

static void send(const char *m) { amy_add_message((char *)m); }

void setup() {
  amy_config_t amy_config = amy_default_config();
  #ifndef AMYBOARD_ARDUINO
  amy_config.audio = AMY_AUDIO_IS_I2S;
  amy_config.i2s_bclk = 8;
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;
  #endif
  amy_start(amy_config);

  send("S8192Z");            // reset
  send("i1iv6K1Z");          // background synth: Juno, 6 voices
  for (int k = 0; k < 6; ++k) {
    char m[24];
    snprintf(m, sizeof m, "i1n%dl0.5Z", 40 + 3 * k);
    send(m);
  }
  fprintf(stderr, "LOCKTIMING_START ms=%lu stage=1 cycles=%d gap_ms=%d\n",
          (unsigned long)millis(), CYCLES_PER_STAGE, LOAD_GAP_MS);
}

static int step = 0;
static unsigned long last_ms = 0;

void loop() {
  amy_update();
  if (millis() - last_ms < LOAD_GAP_MS) return;
  last_ms = millis();

  int total = 2 * CYCLES_PER_STAGE * num_loads;
  if (step >= total) {
    if (step == total) {
      fprintf(stderr, "LOCKTIMING_DONE ms=%lu\n", (unsigned long)millis());
      ++step;
    }
    return;
  }
  if (step == CYCLES_PER_STAGE * num_loads) {
    // Stage 2: a DX7 chord on synth 3 on top of the Juno chord.
    send("i3iv4K130Z");
    for (int k = 0; k < 4; ++k) {
      char m[24];
      snprintf(m, sizeof m, "i3n%dl0.5Z", 52 + 4 * k);
      send(m);
    }
    fprintf(stderr, "LOCKTIMING_STAGE ms=%lu stage=2\n", (unsigned long)millis());
  }
  const char *msg = loads[step % num_loads];
  fprintf(stderr, "LOAD i=%d stage=%d msg=%s ms=%lu\n",
          step, step < CYCLES_PER_STAGE * num_loads ? 1 : 2, msg, (unsigned long)millis());
  send(msg);
  send("i2n48l0.4Z");        // play the freshly loaded synth
  ++step;
}
