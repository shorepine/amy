// BillieJeanDrumsBass - Use different synthesizers to add a bass line along with the drums
// see https://github.com/shorepine/amy/blob/main/docs/billie_jean.md

#include <AMY-Arduino.h>

void setup() {
  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  // Install the default_synths on synths (MIDI chans) 1, 2, and 10 (this is the default).
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

  // Set up synth 2 as monophonic bass
  amy_event e = amy_default_event();
  e.synth = 2;
  e.patch_number = 30;  // Juno A47 Funky I
  e.num_voices = 1;
  amy_add_event(&e);
}

struct timed_note {
  float start_time;  // In ticks
  int channel;       // 10 = drums, 2 = bass
  int note;
  float velocity;
};

// Notes must be sorted in start_time order.
timed_note notes[] = {
  { 0.0, 2, 43, 0.2},   // bass G2
  { 0.0, 10, 42, 1.0},  //  0 HH + BD
  { 0.0, 10, 35, 1.0},
  { 1.0, 2, 38, 0.2},   // bass D2
  { 1.0, 10, 42, 1.0},  //  1 HH
  { 2.0, 2, 41, 0.2},   // bass F2
  { 2.0, 10, 42, 1.0},  //  2 HH + SN
  { 2.0, 10, 37, 1.0},
  { 3.0, 2, 43, 0.2},   // bass G2
  { 3.0, 10, 42, 1.0},  //  3 HH
  { 4.0, 2, 41, 0.2},   // bass F2
  { 4.0, 10, 42, 1.0},  //  4 HH + BD
  { 4.0, 10, 35, 1.0},
  { 5.0, 2, 38, 0.2},   // bass D2
  { 5.0, 10, 42, 1.0},  //  5 HH
  { 6.0, 2, 36, 0.2},   // bass C2
  { 6.0, 10, 42, 1.0},  //  6 HH + SN
  { 6.0, 10, 37, 1.0},
  { 7.0, 2, 38, 0.2},   // bass D2
  { 7.0, 10, 46, 1.0},  //  7 OH
};
// Time (in ticks) at which we reset to the start of the table.
float cycle_len = 8.0;

float millis_per_tick = 250;
float base_tick = 0;  // Time of beginning of current cycle.

int note_tab_index = 0;
int note_tab_len = sizeof(notes) / sizeof(timed_note);

void loop() {
  // Let amy do its processing for this moment.
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
    // Time to play the note.
    amy_event e = amy_default_event();
    e.synth = notes[note_tab_index].channel;
    e.midi_note = notes[note_tab_index].note;
    e.velocity = notes[note_tab_index].velocity;
    amy_add_event(&e);
    note_tab_index++;
  }
}
