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
// library on ESP32, so we just call it from setup() after amy_start().
// (If you can pass compiler flags, e.g. in PlatformIO, building AMY with
//  -DAMY_CODEC_ES8311 makes AMY configure the codec automatically and you can
//  drop the amy_es8311_init() call below.)

#include <AMY-Arduino.h>

// ES8311 codec setup (implemented in the AMY library, src/es8311.c).
extern "C" {
  amy_err_t amy_es8311_init(int i2c_port, int8_t sda, int8_t scl, uint8_t i2c_addr,
                            int8_t pa_enable_gpio, uint8_t pa_active_low,
                            uint32_t sample_rate, uint32_t mclk_hz, uint8_t volume);
};

// FNK0104 audio pins.
#define FNK_I2S_MCLK   4
#define FNK_I2S_BCLK   5
#define FNK_I2S_WS     7
#define FNK_I2S_DOUT   8
#define FNK_I2C_SDA    16
#define FNK_I2C_SCL    15
#define FNK_ES8311_ADDR 0x18
#define FNK_PA_ENABLE  1
#define FNK_PA_ACTIVE_LOW 1   // FNK0104 amp enable is active-low (LOW = amp on)

void setup() {
  amy_config_t amy_config = amy_default_config();
  amy_config.features.default_synths = 0;
  amy_config.features.audio_in = 0;

  // Render on core 1 (multicore) plus a dedicated render thread (multithread).
  amy_config.platform.multicore = 1;
  amy_config.platform.multithread = 1;

  // Let AMY own the I2S output.
  amy_config.audio = AMY_AUDIO_IS_I2S;
  amy_config.i2s_mclk = FNK_I2S_MCLK;   // ES8311 needs MCLK
  amy_config.i2s_bclk = FNK_I2S_BCLK;
  amy_config.i2s_lrc  = FNK_I2S_WS;
  amy_config.i2s_dout = FNK_I2S_DOUT;

  // Starts I2S: MCLK/BCLK/WS are running after this returns.
  amy_start(amy_config);

  // Now configure the ES8311 over I2C and switch on the power amplifier.
  // MCLK = 256 * sample rate (AMY's default ESP I2S clock config).
  amy_err_t rc = amy_es8311_init(0 /*I2C_NUM_0*/, FNK_I2C_SDA, FNK_I2C_SCL,
                                 FNK_ES8311_ADDR, FNK_PA_ENABLE, FNK_PA_ACTIVE_LOW,
                                 AMY_SAMPLE_RATE, (uint32_t)AMY_SAMPLE_RATE * 256,
                                 80 /* volume 0-100 */);
  if (rc == AMY_OK) {
    printf("ES8311 codec init OK\n");
  } else {
    // Codec didn't come up — check wiring / I2C address.
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

static long last_note_ms = 0;
static const int notes[] = { 60, 64, 67, 72 };
static int note_idx = 0;
static int playing_note = -1;  // -1 until the first note is played

void loop() {
  // Required: pump AMY every block.
  amy_update();

  // Play a note from the little arpeggio once a second so the speaker is audible.
  long now = millis();
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
    note_idx = (note_idx + 1) % 4;
  }
}
