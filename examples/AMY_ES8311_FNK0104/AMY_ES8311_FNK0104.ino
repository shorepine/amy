// AMY on a Freenove FNK0104 ESP32-S3 display board.
//
// The FNK0104 drives its speaker through an ES8311 I2S codec + Class-D power
// amplifier, so it needs an I2C register setup that AMY's plain PCM5102-style
// path doesn't do.  AMY ships a small ES8311 driver (src/es8311.c); this sketch
// starts AMY's I2S output and then configures the codec over I2C.
//
// FNK0104 audio wiring (2.8" FNK0104AB):
//   I2S : MCLK=4  BCLK=5  WS/LRCLK=7  DOUT=8   (DIN=6, unused here)
//   I2C : SDA=16  SCL=15  (ES8311 control, address 0x18)
//   Amp : enable on GPIO 1
//
// No special build flags are required: amy_es8311_init() is part of the AMY
// library on ESP32 (prototype in src/es8311.h, pulled in by AMY-Arduino.h), so
// we just call it from setup() after amy_start().
// (If you can pass compiler flags, e.g. in PlatformIO, building AMY with
//  -DAMY_CODEC_ES8311 makes AMY configure the codec inside amy_start() instead;
//  this sketch then just reports amy_es8311_status().)
//
// Ordering matters on this board: the ES8311 shares SDA=16 / SCL=15 with the
// FT6336U touch controller, and the codec driver needs exclusive ownership of
// that I2C port while it runs.  Configure the codec *first*, then Wire.begin()
// for the touch panel / anything else on the bus.

#include <AMY-Arduino.h>

// FNK0104 I2S pins.  The codec-control side (I2C SDA/SCL, codec address, amp
// enable, startup volume) already defaults to the FNK0104 as the AMY_ES8311_*
// defines in es8311.h -- on a different ES8311 board, #define those before the
// AMY-Arduino.h include above.
#define FNK_I2S_MCLK   4
#define FNK_I2S_BCLK   5
#define FNK_I2S_WS     7
#define FNK_I2S_DOUT   8

void setup() {
  amy_config_t amy_config = amy_default_config();
  amy_config.features.default_synths = 0;
  amy_config.features.audio_in = 0;

  // Split rendering across both cores.  We deliberately leave multithread at 0:
  // with a background render thread, the amy_add_event() calls in loop() below
  // would race that thread over voice/oscillator allocation (AMY's queue lock
  // covers the delta list, not the allocation state).  With multithread = 0,
  // amy_update() executes events on this task, so loop() is the only writer.
  amy_config.platform.multicore = 1;
  amy_config.platform.multithread = 0;

  // Let AMY own the I2S output.
  amy_config.audio = AMY_AUDIO_IS_I2S;
  amy_config.i2s_mclk = FNK_I2S_MCLK;   // ES8311 needs MCLK
  amy_config.i2s_bclk = FNK_I2S_BCLK;
  amy_config.i2s_lrc  = FNK_I2S_WS;
  amy_config.i2s_dout = FNK_I2S_DOUT;

  // Starts I2S: MCLK/BCLK/WS are running after this returns.
  amy_start(amy_config);

#ifdef AMY_CODEC_ES8311
  // AMY already configured the codec (and the amp) from inside amy_start().
  amy_err_t rc = amy_es8311_status();
#else
  // Configure the ES8311 over I2C and switch on the power amplifier.  MCLK is
  // i2s_mclk_mult * sample rate -- take it from the config so this can't drift
  // away from what AMY's I2S peripheral is actually generating.
  amy_err_t rc = amy_es8311_init(AMY_ES8311_I2C_PORT, AMY_ES8311_I2C_SDA, AMY_ES8311_I2C_SCL,
                                 AMY_ES8311_I2C_ADDR, AMY_ES8311_PA_GPIO, AMY_ES8311_PA_ACTIVE_LOW,
                                 AMY_SAMPLE_RATE,
                                 (uint32_t)AMY_SAMPLE_RATE * amy_config.i2s_mclk_mult,
                                 AMY_ES8311_VOLUME);
#endif
  if (rc == AMY_OK) {
    printf("ES8311 codec init OK\n");
  } else {
    // Codec didn't come up — check wiring / I2C address, and that nothing else
    // (Wire.begin() for the touch panel) claimed SDA/SCL first.
    printf("ES8311 codec init FAILED (%d)\n", rc);
  }

  // Set up a simple synth (Juno patch 1) on channel 1 so we have something to play.
  amy_event e = amy_default_event();
  e.reset_osc = RESET_AMY;
  amy_add_event(&e);
  e = amy_default_event();
  e.synth = 1;
  e.patch_number = 1;
  e.num_voices = 6;
  amy_add_event(&e);
}

static uint32_t last_note_ms = 0;
static const int notes[] = { 60, 64, 67, 72 };
static const int num_notes = sizeof(notes) / sizeof(notes[0]);
static int note_idx = 0;
static int playing_note = -1;  // -1 until the first note is played

void loop() {
  // Required: pump AMY every block.
  amy_update();

  // Play a note from the little arpeggio once a second so the speaker is audible.
  // Unsigned math, so this keeps working across the ~49-day millis() rollover.
  uint32_t now = millis();
  if (now - last_note_ms > 1000) {
    last_note_ms = now;

    // Release the previously-playing note (skip on the very first note).
    if (playing_note >= 0) {
      amy_event off = amy_default_event();
      off.synth = 1;
      off.midi_note = playing_note;
      off.velocity = 0;
      amy_add_event(&off);
    }

    // Start the next note.
    amy_event on = amy_default_event();
    on.synth = 1;
    on.midi_note = notes[note_idx];
    on.velocity = 1;
    amy_add_event(&on);

    playing_note = notes[note_idx];
    note_idx = (note_idx + 1) % num_notes;
  }
}
