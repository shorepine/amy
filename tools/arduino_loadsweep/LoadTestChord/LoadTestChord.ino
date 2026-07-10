// LoadTestChord - fixed render-load test signal for the arduino_loadsweep tool.
//
// Resets AMY, sets up synth 1 as 8 voices of patch 1 (Juno), then adds one
// held note to the chord every 2 seconds: 40, 42, ... 54.  Nothing is ever
// released, so by t=16s all 8 voices are sounding and the render load has
// stepped up 8 times.  The sweep's i2s.c patch prints RENDER_LOAD lines on
// stderr (the debug UART); this sketch adds NOTE markers so the load steps
// can be aligned to note count.
//
// Only uses amy_event fields that exist across the whole sweep window
// (2026-04-01 / v1.2.4 onwards): synth, patch_number, num_voices, midi_note,
// velocity, reset_osc.

#include <AMY-Arduino.h>

void setup() {
  amy_config_t amy_config = amy_default_config();

  // If running an AMYboard, pins/features are set automatically.
  #ifndef AMYBOARD_ARDUINO
  amy_config.audio = AMY_AUDIO_IS_I2S;
  amy_config.i2s_bclk = 8;
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;
  #endif

  amy_start(amy_config);

  // amy.reset()
  amy_event e = amy_default_event();
  e.reset_osc = RESET_AMY;
  amy_add_event(&e);

  // amy.send(synth=1, num_voices=8, patch=1)
  e = amy_default_event();
  e.synth = 1;
  e.patch_number = 1;
  e.num_voices = 8;
  amy_add_event(&e);

  fprintf(stderr, "LOADTEST_START ms=%lu\n", (unsigned long)millis());
}

static int note_i = 0;
static unsigned long last_note_ms = 0;

void loop() {
  amy_update();

  // Every 2 seconds: amy.send(synth=1, note=40+(i*2), vel=0.2), held forever.
  if (note_i < 6 && millis() - last_note_ms >= 2000) {
    last_note_ms = millis();
    amy_event e = amy_default_event();
    e.synth = 1;
    e.midi_note = 40 + (note_i * 3);
    e.velocity = 0.2f;
    amy_add_event(&e);
    fprintf(stderr, "NOTE i=%d note=%d ms=%lu\n",
            note_i, 40 + (note_i * 2), (unsigned long)millis());
    note_i++;
  }
}
