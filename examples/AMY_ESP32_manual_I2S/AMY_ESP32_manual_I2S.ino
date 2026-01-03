// AMY_MIDI_Synth modified to test out new amy_update that returns the pointer to samples.
// It then handles passing off to I2S in-sketch rather than in the background.
// ESP32-specific.

#include <AMY-Arduino.h>

// Should we use the transparent I2S driver in AMY?
// Otherwise we'll handle passing samples to I2S ourselves.
#define USE_AMY_FOR_I2S

// If we're not USE_AMY_FOR_I2S, should we install our I2S output function using config.write_samples_fn?
// Otherwise, we'll just get a pointer to samples from amy_update() and pass them on ourselves.
#define USE_WRITE_SAMPLES_FN

// Do we want a background task to do rendering?  It's slightly more efficient under RTOS
#define USE_MULTITHREAD 1

// Do we want to split rendering between two cores?  This works with or without MULTITHREAD.
#define USE_MULTICORE 1

// For convenience, we'll reuse AMY's ESP32 I2S setup and write functions.
extern "C" {
  amy_err_t esp32_setup_i2s();
  size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes);
};

void test_polyphony() {
  // Allocate a synth with lots of voices to test polyphony.
  // Increase the number of voices available to MIDI channel 1.
  amy_event e = amy_default_event();
  e.reset_osc = RESET_AMY;
  amy_add_event(&e);
  e = amy_default_event();
  e.synth = 1;
  e.patch_number = 1;
  e.num_voices = 12;  // I get 12 simultaneous juno patch 0 voices on a 250 MHz RP2040.
  amy_add_event(&e);
}

// Support the NeoPixel on the ESP32-S3 dev board
#include <Adafruit_NeoPixel.h> 
#define PIN 38  // 48 on some ESP32-S3 dev boards
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

enum COLORS {
  BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, YELLOW, WHITE
};

void set_led(int val) {
  uint32_t c;
  switch(val) {
    case 0:  c = pixels.Color(0, 0, 0); break;
    case 1:  c = pixels.Color(0, 0, 100); break;
    case 2:  c = pixels.Color(0, 100, 0); break;
    case 3:  c = pixels.Color(0, 100, 100); break;
    case 4:  c = pixels.Color(100, 0, 0); break;
    case 5:  c = pixels.Color(100, 0, 100); break;
    case 6:  c = pixels.Color(100, 100, 0); break;
    default: c = pixels.Color(100, 100, 100); break;
  }
  pixels.setPixelColor(0, c);
  pixels.show();
}

// -------------------------------------

void setup() {
  pixels.begin();
  pixels.clear();
  set_led(GREEN);

  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  amy_config.features.default_synths = 0;

  // On ESP32/RTOS, multithread runs audio rendering on its own thread.
  amy_config.platform.multithread = USE_MULTITHREAD;
  // On ESP32/RTOS, multicore will run a second rendering thread on core 1 even if multithread = 0.
  amy_config.platform.multicore = USE_MULTICORE;

#ifdef USE_AMY_FOR_I2S
  amy_config.audio = AMY_AUDIO_IS_I2S;
#else
  amy_config.audio = AMY_AUDIO_IS_NONE;
#endif
  amy_config.features.audio_in = 0;
  // Pins for i2s board.
  // Because we're using AMY's setup_i2s, this also configures if !USE_AMY_FOR_I2S.
  amy_config.i2s_mclk = 7;
  amy_config.i2s_bclk = 8;
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;

  // If you want MIDI over UART (5-pin or 3-pin serial MIDI)
  amy_config.midi = AMY_MIDI_IS_UART;
  // Pins for UART MIDI
  amy_config.midi_out = 4;
  amy_config.midi_in = 5;

#ifndef USE_AMY_FOR_I2S
  #ifdef USE_WRITE_SAMPLES_FN
  // amy_update() will automatically pass the new samples to this fn, if provided.
  amy_config.write_samples_fn = amy_i2s_write;
  #endif
#endif

  amy_start(amy_config);

#ifndef USE_AMY_FOR_I2S
  esp32_setup_i2s();  // respects cached amy_config.
#endif

  set_led(MAGENTA);

  test_polyphony();

  set_led(YELLOW);
}

static long last_millis = 0;
static const long millis_interval = 250;

static bool led_state = 0;

void loop() {
  // Your loop() must contain this call to amy:
  int16_t *buf = amy_update();
#ifndef USE_AMY_FOR_I2S
  #ifndef USE_WRITE_SAMPLES_FN
  amy_i2s_write((const uint8_t *)buf, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
  #endif
#endif

  // Toggle on-board LED every 250ms
  int now_millis = millis();
  if ((now_millis - last_millis) > millis_interval) {
    last_millis = now_millis;
    led_state = !led_state;
    if (led_state != 0) set_led(WHITE);
    else                set_led(BLACK);
  }
}
