#include <AMY-Arduino.h>

// Simple AMY synth setup that just sets up the default MIDI synth.
// Plug a MIDI keyboard into the MIDI in port and play notes, send program changes, etc
// We've tested this with the following boards:
//
// esp32s3 [n32r8]
// rp2040 (Pi Pico) -- use 250Mhz and -O3 for 6 note juno polyphony and turn off serial debug if you have it on
// rp2350 (Pi Pico)
// teensy4
// Electrosmith Daisy
// Please see https://github.com/shorepine/amy/issues/354 for the full list


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

void test_sequencer() {
  amy_event e = amy_default_event();
  e.sequence[0] = 0;
  e.sequence[1] = 48;
  e.sequence[2] = 1;
  e.velocity = 1;
  e.midi_note = 60;
  e.synth = 1;
  amy_add_event(&e);
}

void test_audio_in() {
  // Copy audio in to out, with echo.
  amy_event e = amy_default_event();
  e.osc = 170;
  e.wave = AUDIO_IN0;
  e.pan_coefs[COEF_CONST] = 0.0f;
  e.velocity = 1.0f;
  amy_add_event(&e);
  e.osc = 171;
  e.wave = AUDIO_IN1;
  e.pan_coefs[COEF_CONST] = 1.0f;
  e.velocity = 1.0f;
  amy_add_event(&e);
  // Add echo to show that audio in is being processed.
  config_echo(0.5f, 150.0f, 160.0f, 0.5f, 0.0f);  // I get errors on ESP32-S3 N8R8 if I go above 160.0ms
  // Turn off chorus, for better audio pass-through.
  config_chorus(0, 320, 0.5f, 0.5f);
}


// -------------------------------------


void setup() {
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 1);
#endif

#ifdef HAS_SERIAL
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Serial.println("AMY_Synth");
#endif

  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  // Install the default_synths on synths (MIDI chans) 1, 2, and 10 (this is the default).
  amy_config.features.default_synths = 1;

  // If you want MIDI over UART (5-pin or 3-pin serial MIDI)
  amy_config.midi = AMY_MIDI_IS_UART;

  // Pins for i2s board
  // Note: On the Teensy, all these settings are ignored, and blck = 21, lrc = 20, dout = 7.
  amy_config.i2s_mclk = 7;
  amy_config.i2s_bclk = 8;
  // On Pi Pico (RP2040, RP2350), i2s_lrc has to be i2s_bclk + 1, otherwise code will stop on an assert.
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;
  amy_config.i2s_din = 11;

  // Pins for UART MIDI
  // Note: On the Teensy, these are ignored and midi_out = 35, midi_in = 34.
  amy_config.midi_out = 4;
  amy_config.midi_in = 5;

  amy_start(amy_config);
  amy_live_start();

  //test_polyphony();
  //test_sequencer();
  test_audio_in();
}

static long last_millis = 0;
static const long millis_interval = 250;

static bool led_state = 0;

void loop() {
  // Your loop() must contain this call to amy:
  amy_update();

  // Flash on-board LED every 250ms
  int now_millis = millis();
  if ((now_millis - last_millis) > millis_interval) {
    last_millis = now_millis;
    led_state = !led_state;
#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, led_state);  // turn the LED on (HIGH is the voltage level)
#endif
  }
}
