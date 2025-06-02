#include <AMY-Arduino.h>

// Simple AMY synth setup that just sets up the default MIDI synth.
// Plug a MIDI keyboard into the MIDI in port and play notes, send program changes, etc
// We've tested this with the following boards:
// 
// esp32s3 [n32r8]
// rp2040 (Pi Pico) -- use 250Mhz and -O3 for 6 note juno polyphony and turn off serial debug if you have it on
// rp2350 (Pi Pico)
// teensy4 
// Please see https://github.com/shorepine/amy/issues/354 for the full list


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

void setup() {
  amy_config_t amy_config = amy_default_config();


  // If you want MIDI over UART (5-pin or 3-pin serial MIDI)
  amy_config.has_midi_uart = 1;

  // Pins for i2s board
  amy_config.i2s_bclk = 8;
  // On Pi Pico (RP2040, RP2350), i2s_lrc has to be i2s_bclk + 1, and what you set here is ignored
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;

  // Pins for UART MIDI
  amy_config.midi_out = 4;
  amy_config.midi_in = 5;

  // Features
  amy_config.has_chorus = 1;
  amy_config.has_reverb = 1;
  amy_config.has_echo = 1;


  amy_start(amy_config);
  amy_live_start();

  //test_sequencer();
}

void loop() {
  // Your loop() must contain this call to amy:
  amy_update(); 

  // put your main code here, to run repeatedly:
}
