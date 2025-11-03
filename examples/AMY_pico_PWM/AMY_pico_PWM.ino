#include <AMY-Arduino.h>

// AMY_pico_PWM
//
// Runs AMY using arduino-pico's PWM audio output.
// This version responds to serial MIDI input.
// dpwe 2025-10-26

// If you define this, we write to PWM using AMY's write_samples_fn hook
// but leaving it undefined means we pass the sample block in our loop() fn.
//#define USE_AMY_WRITE_SAMPLES_FN

extern "C" {
  extern void example_sequencer_drums_synth(uint32_t start);
}

#include <PWMAudio.h>
PWMAudio pwm(0, true);  // PWM Stereo out on pins 0 and 1.

// Have to introduce this function to work around virtual overloaded write function.
size_t pwm_write(const uint8_t *buffer, size_t nbytes) {
  return pwm.write(buffer, nbytes);
}

void setup() {
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 1);
#endif

  // Setup PWM
  pwm.setBuffers(4, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t) / sizeof(int32_t));
  pwm.begin(44100);

  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  amy_config.features.default_synths = 1;

  amy_config.midi = AMY_MIDI_IS_UART;
  // Pins for UART MIDI
  amy_config.midi_in = 5;

  #ifdef USE_AMY_WRITE_SAMPLES_FN
  // Configure to pass samples to PWM.
  amy_config.write_samples_fn = pwm_write;
  #endif

  amy_start(amy_config);

  // Set a drum loop going, as an example.
  example_sequencer_drums_synth(2000);
}

static long last_millis = 0;
static const long millis_interval = 250;
static bool led_state = 0;

void loop() {
  int16_t *block = amy_update();
#ifndef USE_AMY_WRITE_SAMPLES_FN
  // We have opted to handle our own sample writing.
  pwm_write((uint8_t *)block, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
#endif

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
