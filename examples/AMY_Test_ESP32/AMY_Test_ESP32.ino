#include <AMY-Arduino.h>
#include <I2S.h>

AMY amy;


void setup() {
  // This is if you're using an Alles board, you have to poke 5V_EN to turn on the speaker
  // You can ignore this if you're not using an Alles
  pinMode(21, OUTPUT);
  digitalWrite(21, 1);

  // Set your I2S pins. Data/SD/DIN/DOUT, SCK/BLCK, FS/WS/LRCLK. 
  I2S.setDataPin(27); // 27
  I2S.setSckPin(26); // 26
  I2S.setFsPin(25); // 25
  I2S.begin(I2S_PHILIPS_MODE, AMY_SAMPLE_RATE, BYTES_PER_SAMPLE*8);

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
   
  // Run an example script to start 5s from now
  amy.drums(clock+5000, 2);

}


void loop() {
  // In your loop you have to get the buffer of samples and then play it out your device
  short * samples = amy.render_to_buffer();
  I2S.write_blocking(samples, AMY_BLOCK_SIZE*AMY_NCHANS*BYTES_PER_SAMPLE);
}

