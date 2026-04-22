#include <AMY-Arduino.h>

// Example of using the AMY "custom oscillator" from Arduino.

// Custom osc types are defined by 7 functions:

void my_custom_init() {
  // Called once at synth setup.
}

void my_custom_deinit() {
  // Called at synth teardown, to release resources.
}

void my_custom_note_on(uint16_t osc, float freq) {
  // Called when note-on received, to set up any per-note state, with initial frequency value.
}

// Magic number AMY uses to indicate "unset" clocks.
#define CLOCK_UNSET UINT32_MAX

void my_custom_note_off(uint16_t osc) {
  // Called when note-off received.
  // If you want the built-in ADSR to work, the note-off has to signal that we've moved into release phase.
  synth[osc]->note_on_clock = CLOCK_UNSET;
  synth[osc]->note_off_clock = amy_global.total_blocks * AMY_BLOCK_SIZE;  // time (in samples) that release began.
}

void my_custom_mod_trigger(uint16_t osc) {
  // Called when this osc is the mod_osc to another osc which receives a note-on.
}

// Non-anti-aliased square wave as an example custom oscillator.
SAMPLE my_custom_render(SAMPLE *buf, uint16_t osc) {
  // Called to render the next block of AMY_BLOCK_SIZE samples.  They should be summed into buf.  Return the peak value.
  // We have access to the msynth[osc] parameters for this frame, and the persistent synth[osc] state.
  // Calculate the phase advance per sample.  PHASOR is S.31 fixed point value, F2P converts a float to the PHASOR domain.
  float freq = freq_of_logfreq(msynth[osc]->logfreq);  // Current pitch, including effects like pitch bend etc.
  PHASOR step = F2P(freq / (float)AMY_SAMPLE_RATE);    // Phase advance per sample: cycles per sec / samples per sec -> cycles per sample
  SAMPLE amp = F2S(msynth[osc]->amp);                  // msynth params are floats, but buffer etc are in S8.23 fixed point.
  SAMPLE max_value = 0;
  for (int i = 0; i < AMY_BLOCK_SIZE; ++i) {
    SAMPLE value = MUL8_SS(amp,                        // MUL8_SS is fixed-point multiply, to apply ADSR envelope scaling in amp.
                           (synth[osc]->phase >= F2P(0.5f)) ? F2S(0.5f) : F2S(-0.5f));  // Naive square wave.  Causes harsh alias tones.
    buf[i] += value;                                   // Add it into the output buffer.
    if (value < 0)  value = -value;   // i.e. calculate ABS(value)
    if (value > max_value)  max_value = value;  // track the max
    // Step on the phase variable.
    synth[osc]->phase = P_WRAPPED_SUM(synth[osc]->phase, step);
  }
  return max_value;
}

SAMPLE my_custom_compute_mod(uint16_t osc) {
  // Return the one value per block generated when this is mod_osc.
  return 0;
}

// -------------------------------------

void setup() {
  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  amy_config.features.default_synths = 0;

  // If running an AMYboard, we set these parameters for you automatically. 
  // For your own boards, set the pins and features you want.
  #ifndef AMYBOARD_ARDUINO
  // Pins for i2s board
  // Note: On the Teensy, all these settings are ignored, and blck = 21, lrc = 20, dout = 7.
  amy_config.audio = AMY_AUDIO_IS_I2S;
  amy_config.i2s_mclk = 7;
  amy_config.i2s_bclk = 8;
  // On Pi Pico (RP2040, RP2350), i2s_lrc has to be i2s_bclk + 1, otherwise code will stop on an assert.
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;
  amy_config.i2s_din = 11;
  // If you want MIDI over UART (5-pin or 3-pin serial MIDI)
  amy_config.midi = AMY_MIDI_IS_UART;
  // Pins for UART MIDI
  // Note: On the Teensy, these are ignored and midi_out = 35, midi_in = 34.
  amy_config.midi_out = 4;
  amy_config.midi_in = 5;
  #endif

  amy_config.features.custom = 1;  // We are using a custom oscillator.

  // Install the custom oscillator functions.
  struct custom_oscillator custom = {
    my_custom_init,
    my_custom_deinit,
    my_custom_note_on,
    my_custom_note_off,
    my_custom_mod_trigger,
    my_custom_render,
    my_custom_compute_mod,
  };
  amy_set_custom(&custom);

  amy_start(amy_config);

  // Set up a single-osc synth using the custom osc.
  amy_event e = amy_default_event();
  e.synth = 1;
  e.oscs_per_voice = 1;
  e.num_voices = 4;
  amy_add_event(&e);

  e = amy_default_event();
  e.synth = 1;
  e.osc = 0;
  e.wave = CUSTOM;
  // Setup decaying ADSR
  e.eg0_times[0] = 10;      // attack (in ms)
  e.eg0_times[1] = 1000;    // decay
  e.eg0_times[2] = 1000;    // release
  e.eg0_values[0] = 1.0f;   // (attack target level)
  e.eg0_values[1] = 0.3f;   // sustain
  e.eg0_values[2] = 0;      // (release target level) 
  amy_add_event(&e);
}

void loop() {
  // Your loop() must contain this call to amy:
  amy_update();
}
