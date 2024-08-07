#include <AMY-Arduino.h>
#include <ESP_I2S.h>

AMY amy;
I2SClass I2S;

void setup() {
  // This is if you're using an Alles board, you have to poke 5V_EN to turn on the speaker
  // You can ignore this if you're not using an Alles
  pinMode(21, OUTPUT);
  digitalWrite(21, 1);

  // Set your I2S pins. Data/SD/DIN/DOUT, SCK/BLCK, FS/WS/LRCLK. 
//  int8_t bclk, int8_t ws, int8_t dout,
  I2S.setPins(4, 6, 5, -1, -1);
  I2S.begin(I2S_MODE_STD, AMY_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO); // I2S.begin(I2S_PHILIPS_MODE, AMY_SAMPLE_RATE, BYTES_PER_SAMPLE8);

  // Start up AMY
  amy.begin(/* cores= */ 1, /* reverb= */ 0, /* chorus= */ 0);

  // Send AMY messages in two ways: first, using the wire protocol:
  // This sends a sine wave at 220Hz on osc 0 with velocity 1
  amy.send_message("v0f220l1");
  
  // The second way is to use the structure directly 
  // We fill in an event struct with what we want and "schedule" it for 2.5 seconds from now


  long clock = amy.sysclock();
  struct event e = amy.default_event();
  e.osc = 0;
  e.wave = SINE;
  e.freq_coefs[COEF_CONST] = 440;
  e.velocity = 1;
  e.time = clock+2500;
  amy.add_event(e);

  // Run an example at 5s from now
  amy.voice_chord(clock+5000, 0);
  // Run an example drum loop to start 10s from now, play twice, stop
  amy.drums(clock+7500, 2);

}


void loop() {
  // In your loop you have to get the buffer of samples and then play it out your device
  short * samples = amy.render_to_buffer();
  I2S.write((uint8_t*)samples, AMY_BLOCK_SIZE*AMY_NCHANS*BYTES_PER_SAMPLE);
}

