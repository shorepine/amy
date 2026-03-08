// BillieJeanScheduled - playing drums, bass, and chords using the AMY scheduler for timing
// see https://github.com/shorepine/amy/blob/main/docs/billie_jean.md

// Note: On the RP2350, using arduino-pico v 5.5.1, I have trouble with this crashing when I compile -O3 (Optimize Even More).
// It runs correctly with -O2 (Optimize More), but then it can't quite keep up during the chords at 150 MHz.
// I ran at 176 MHz and it was fine.

#include <AMY-Arduino.h>

void setup() {
  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  // Install the default_synths on synths (MIDI chans) 1, 2, and 10 (this is the default).
  amy_config.features.default_synths = 1;

  amy_config.audio = AMY_AUDIO_IS_I2S;
  // Pins for i2s board
  amy_config.i2s_bclk = 8;
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;

  amy_start(amy_config);

  // Reconfigure synth 1 as a 6-note polyphonic synth (for chords)
  amy_event e = amy_default_event();
  e.synth = 1;
  e.patch_number = 5;  // Juno A16 Brass & Strings
  e.num_voices = 6;
  amy_add_event(&e);

  // Reconfigure synth 2 as monophonic bass
  e = amy_default_event();
  e.synth = 2;
  e.patch_number = 30;  // Juno A47 Funky I
  e.num_voices = 1;
  amy_add_event(&e);

  // Turn on reverb
  config_reverb(0.5f, 0.85f, 0.5f, 3000.0f);
}

struct timed_note {
  float start_time;  // In ticks
  float duration;    // In ticks
  int note;
  float velocity;
};

// Cycle length (in ticks) for drums + bass
float cycle_len = 8.0;

// Drum notes have durations of 0 for no note-off
timed_note drum_notes[] = {
  { 0.0, 0.0, 42, 1.0},  //  0 HH + BD
  { 0.0, 0.0, 35, 1.0},
  { 1.0, 0.0, 42, 1.0},  //  1 HH
  { 2.0, 0.0, 42, 1.0},  //  2 HH + SN
  { 2.0, 0.0, 37, 1.0},
  { 3.0, 0.0, 42, 1.0},  //  3 HH
  { 4.0, 0.0, 42, 1.0},  //  4 HH + BD
  { 4.0, 0.0, 35, 1.0},
  { 5.0, 0.0, 42, 1.0},  //  5 HH
  { 6.0, 0.0, 42, 1.0},  //  6 HH + SN
  { 6.0, 0.0, 37, 1.0},
  { 7.0, 0.0, 42, 2.0},  //  7 OH
};

timed_note bass_notes[] = {
  { 0.0, 0.6, 43, 0.4},   // bass G2
  { 1.0, 0.6, 38, 0.2},   // bass D2
  { 2.0, 0.6, 41, 0.2},   // bass F2
  { 3.0, 0.6, 43, 0.4},   // bass G2
  { 4.0, 0.6, 41, 0.2},   // bass F2
  { 5.0, 0.6, 38, 0.2},   // bass D2
  { 6.0, 0.6, 36, 0.2},   // bass C2
  { 7.0, 0.6, 38, 0.2},   // bass D2
};

timed_note chord_notes[] = {
  { 0.0, 0.2, 70, 1.0},  // Fmin:1
  { 0.0, 0.2, 74, 1.0},
  { 0.0, 0.2, 79, 1.0},
  { 3.0, 0.2, 72, 1.0},  // Amin:1
  { 3.0, 0.2, 76, 1.0},
  { 3.0, 0.2, 81, 1.0},
  { 8.0, 0.2, 74, 1.0},  // Bb:1
  { 8.0, 0.2, 77, 1.0},
  { 8.0, 0.2, 82, 1.0},
  { 11.0, 0.2, 72, 1.0},  // Amin:1
  { 11.0, 0.2, 76, 1.0},
  { 11.0, 0.2, 81, 1.0},
};

float millis_per_tick = 250;

void schedule_notes(int time, int channel, struct timed_note *notes, int num_notes) {
  amy_event e = amy_default_event();
  e.synth = channel;
  for (int i = 0; i < num_notes; ++i) {
    e.midi_note = notes[i].note;
    e.velocity = notes[i].velocity;
    e.time = time + millis_per_tick * notes[i].start_time;
    amy_add_event(&e);
    // Add note-off too if duration > 0
    if (notes[i].duration > 0) {
      e.time += millis_per_tick * notes[i].duration;
      e.velocity = 0;
      amy_add_event(&e);
    }
  }
}

int start_millis = 3000;
int last_cycle = -1;

void loop() {
  // Let amy do its processing for this moment.
  amy_update();

  int now = millis();
  int current_cycle = floor((now - start_millis) / (millis_per_tick * cycle_len));
  if (current_cycle > last_cycle) {
    // A new cycle began, issue notes.
    // Drums
    schedule_notes(now, 10, drum_notes, sizeof(drum_notes) / sizeof(timed_note));
    // Bass comes in after two cycles of drums
    if (current_cycle >= 2)
      schedule_notes(now, 2, bass_notes, sizeof(bass_notes) / sizeof(timed_note));
    // Chord sequence is 2 cycles long, so only schedule every other cycle
    if ((current_cycle >= 4) && ((current_cycle % 2) == 0))
      schedule_notes(now, 1, chord_notes, sizeof(chord_notes) / sizeof(timed_note));
    last_cycle = current_cycle;
  }
}
