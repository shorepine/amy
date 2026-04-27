#include <AMY-Arduino.h>

// AMY_Spresense_Analog
//
// Minimal AMY example for Sony Spresense analog output.
// Spresense backend in src/spresense_support.cpp handles DAC output.

void setup() {
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
#endif

  Serial.begin(115200);
  delay(1000);
  Serial.println("AMY Spresense analog output");

  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  amy_config.features.default_synths = 1;

  // Default synth set may require around 90+ oscillators; keep headroom.
  amy_config.max_oscs = 128;

  // Use Spresense platform audio path (analog DAC output backend).
  // To use I2S output instead, uncomment the next line.
  // amy_config.spresense_output_device = SPRESENSE_OUTPUT_I2S;
  amy_config.audio = AMY_AUDIO_IS_SPRESENSE;
  amy_config.midi = AMY_MIDI_IS_NONE;

  amy_start(amy_config);

}

void loop() {
  amy_update();

  static uint32_t last_ms = 0;
  uint32_t now = millis();
  if (now - last_ms > 600) {
    last_ms = now;
    static int root = 60;
    static bool play_chord = false;
    uint32_t t = amy_sysclock();

    if (!play_chord) {
      Serial.print("single note ");
      Serial.print(root);
      Serial.print(" at ");
      Serial.println(t);

      amy_event on = amy_default_event();
      on.time = t;
      on.synth = 1;
      on.midi_note = root;
      on.velocity = 0.8f;
      amy_add_event(&on);

      amy_event off = amy_default_event();
      off.time = t + 220;
      off.synth = 1;
      off.midi_note = root;
      off.velocity = 0.0f;
      amy_add_event(&off);
    } else {
      const int chord[3] = {root, root + 4, root + 7};
      Serial.print("chord ");
      Serial.print(chord[0]);
      Serial.print(",");
      Serial.print(chord[1]);
      Serial.print(",");
      Serial.print(chord[2]);
      Serial.print(" at ");
      Serial.println(t);

      for (int i = 0; i < 3; ++i) {
        amy_event on = amy_default_event();
        on.time = t;
        on.synth = 1;
        on.midi_note = chord[i];
        on.velocity = 0.7f;
        amy_add_event(&on);

        amy_event off = amy_default_event();
        off.time = t + 220;
        off.synth = 1;
        off.midi_note = chord[i];
        off.velocity = 0.0f;
        amy_add_event(&off);
      }
    }

    play_chord = !play_chord;
    root += 2;
    if (root > 72) {
      root = 60;
    }
  }
}
