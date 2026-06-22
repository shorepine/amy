#include <AMY-Arduino.h>

// This example shows how AMY's input and external buffers
// can be used to implement your own audio processing.
// It records audio from the audio input,
// applies a bit reduction effect, and passes it
// to the output (see loop() for the effect itself)

// setup_oscillators sets up the AMY instruments: 
void setup_oscillators() {
  amy_event e = amy_default_event();
  e.reset_osc = RESET_AMY;
  amy_add_event(&e);

  // Reserve synth 18 for our effect.
  // It consists of one voice with 
  // one oscillator for each channel:
  e = amy_default_event();
  e.synth = 18;
  e.num_voices = 1;
  e.oscs_per_voice = 2;
  amy_add_event(&e);

  // Set the first oscillator to play 
  // from the left channel of the external buffer:
  e = amy_default_event();
  e.synth = 18;
  e.osc = 0;
  e.wave = AUDIO_EXT0;

  // Pan it all the way to the left:
  e.pan_coefs[COEF_CONST] = 0.0f;

  // Since audio input on the AMYboard can be somewhat quiet, 
  // apply a little gain
  // (but not too much, the bit reduction can be loud...)
  e.amp_coefs[COEF_CONST] = 5.0f; 
  e.velocity = 1.0f;
  amy_add_event(&e);
  
  // Set the second oscillator to play
  // from the right channel:
  e = amy_default_event();
  e.synth = 18;
  e.osc = 1;
  e.wave = AUDIO_EXT1;
  e.pan_coefs[COEF_CONST] = 1.0f;
  e.amp_coefs[COEF_CONST] = 5.0f;
  e.velocity = 1.0f;
  amy_add_event(&e);
  
  // By default the chorus is on, turn it off:
  config_chorus(/* bus */ 0, 0, 320, 0.5f, 0.5f);
}

// send_audio_ext_notes tells AMY to activate the synth
// and play audio from the external buffer:
void send_audio_ext_notes()
{
  amy_event e = amy_default_event();
  e.synth = 18;
  e.osc = 0;
  e.wave = AUDIO_EXT0;
  e.midi_note = 60;
  e.velocity = 1;
  amy_add_event(&e);

  e = amy_default_event();
  e.synth = 18;
  e.osc = 1;
  e.wave = AUDIO_EXT1;
  e.midi_note = 60;
  e.velocity = 1;
  amy_add_event(&e);
}

void setup() {
  amy_config_t amy_config = amy_default_config();
  // Keep the startup bleep and default synths on for debugging:
  amy_config.features.startup_bleep = 1;
  amy_config.features.default_synths = 1;

  // Make sure audio input is enabled:
  amy_config.features.audio_in = 1;

  // If you aren't using an AMYboard, this is where you would set up
  // pins for peripherals like the I2S board:
  #ifndef AMYBOARD_ARDUINO
  amy_config.audio = AMY_AUDIO_IS_I2S;
  // Pins for i2s board
  amy_config.i2s_bclk = 8;
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;
  #endif

  // Start AMY, then set up the instruments:  
  amy_start(amy_config);
  setup_oscillators();
  send_audio_ext_notes();
}

// Set aside a buffer for the effect.
// For this simple effect, we can get away with a single block:
output_sample_type effect_buffer[AMY_BLOCK_SIZE*AMY_NCHANS];

void loop() {
  // Fill the buffer with samples from the input:
  amy_get_input_buffer(&effect_buffer[0]);

  // Implement a very simple bit reduction effect.
  // Just clear all of the lower 8 bits:
  for(uint16_t i = 0; i < AMY_BLOCK_SIZE*AMY_NCHANS; i++)
  {
    effect_buffer[i] = (effect_buffer[i] >> 8) << 8; 
  }

  // Make the oscillators with waves AUDIO_EXT0 and AUDIO_EXT1
  // play from our buffer:
  amy_set_external_input_buffer(&effect_buffer[0]);

  // Call amy_update to write the samples to the outputs:
  amy_update();
}
