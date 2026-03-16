// BillieJeanDrums - Making a simple drum pattern with AMY.
// see https://github.com/shorepine/amy/blob/main/docs/billie_jean.md

#include <AMY-Arduino.h>

void setup() {
  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  // Install the default_synths on synths (MIDI chans) 1, 2, and 10.
  amy_config.features.default_synths = 1;

  // If running an AMYboard, we set these parameters for you automatically. 
  // For your own boards, set the pins and features you want.
  #ifndef AMYBOARD_ARDUINO
  amy_config.audio = AMY_AUDIO_IS_I2S;
  // Pins for i2s board
  amy_config.i2s_bclk = 8;
  amy_config.i2s_lrc = 9;
  amy_config.i2s_dout = 10;
  #endif
  
  amy_start(amy_config);
}

struct timed_note {
  float start_time;  // In ticks
  int note;
  float velocity;
};

// 35 is kick, 37 is snare, 42 is closed hat, 46 is open hat
// Notes must be sorted in start_time order.
timed_note notes[] = {
  { 0.0, 42, 1.0},  //  0 HH + BD
  { 0.0, 35, 1.0},
  { 1.0, 42, 1.0},  //  1 HH
  { 2.0, 42, 1.0},  //  2 HH + SN
  { 2.0, 37, 1.0},
  { 3.0, 42, 1.0},  //  3 HH
  { 4.0, 42, 1.0},  //  4 HH + BD
  { 4.0, 35, 1.0},
  { 5.0, 42, 1.0},  //  5 HH
  { 6.0, 42, 1.0},  //  6 HH + SN
  { 6.0, 37, 1.0},
  { 7.0, 46, 1.0},  //  7 OH
};
// Time (in ticks) at which we reset to the start of the table.
float cycle_len = 8.0;

float millis_per_tick = 250;
float base_tick = 0;  // Time of beginning of current cycle.

int note_tab_index = 0;
int note_tab_len = sizeof(notes) / sizeof(timed_note);

void loop() {
  // Your loop() must contain this call to amy:
  amy_update();

  // Calculate "tick time" and choose note.
  float tick_in_cycle = millis() / millis_per_tick - base_tick;
  if (tick_in_cycle >= cycle_len) {
    // Start the next cycle - reset the cycle base_tick, reset the note_tab index.
    tick_in_cycle -= cycle_len;
    base_tick += cycle_len;
    note_tab_index = 0;
  }

  // Play any notes for this moment from the note table.
  while(note_tab_index < note_tab_len 
        && tick_in_cycle >= notes[note_tab_index].start_time) {
    // Grab the note parameters
    int midi_note = notes[note_tab_index].note;
    float velocity = notes[note_tab_index].velocity;
    // Time to play the note.
    amy_event e = amy_default_event();
    e.synth = 10;  // drums channel
    e.midi_note = midi_note;
    e.velocity = velocity;
    amy_add_event(&e);
    note_tab_index++;
  }
}
