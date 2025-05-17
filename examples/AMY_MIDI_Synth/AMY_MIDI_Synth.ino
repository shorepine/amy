#include <AMY-Arduino.h>

// Simple AMY synth setup that just sets up the default MIDI synth.
// Plug a MIDI keyboard into the MIDI in port and play notes, send program changes, etc
// We've tested this with the following boards:
// 
// esp32s3 [n32r8]


void setup() {
  amy_config_t amy_config = amy_default_config();

  // If you set has_audio_in you also need to set i2s_din pin
  amy_config.has_audio_in = 0;

  // If you want MIDI over UART (normal 5-pin serial MIDI)
  amy_config.has_midi_uart = 1;

  // If you want the default synths (Juno on channel 1, drums on channel 10)
  amy_config.set_default_synth = 1;
  
  // If using a multicore capable chip, set cores to 2 so that you can have more polyphony
  amy_config.cores = 2;

  // Pins for i2s board
  amy_config.i2s_bclk = 8;
  // On Pi Pico (RP2040, RP2530), i2s_lrc has to be i2s_bclk + 1, and what you set here is ignored
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;

  // Pins for UART MIDI
  amy_config.midi_out = -1;
  amy_config.midi_in = 9;

  amy_start(amy_config);
  amy_live_start();
}

void loop() {
  // put your main code here, to run repeatedly:
}
