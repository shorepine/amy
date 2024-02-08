#include <AMY-Arduino.h>
#include <Audio.h>

// AMY Test for Teensy
// Tested on:
// Teensy 4.1 + external I2S stereo DAC (BCLK -> 21, DIN -> 7, LRCLK -> 20)
// Teensy 3.6 + external I2S stereo DAC (BCLK -> 9, DIN -> 22, LRCLK -> 23)
// I could not get a 3.2 to work. This may not be us, as their official Queue example also fails on the 3.2. TODO 


// For Teensy, we take advantage of the Audio Library to pass buffers to I2S. 
// You can easily change the output mode using the Audio Library Design Tool,
// or even add effects and other things. 
AudioPlayQueue           queue_l;
AudioPlayQueue           queue_r;
AudioOutputI2S           i2s1;
AudioConnection          patchCord1(queue_r, 0, i2s1, 1);
AudioConnection          patchCord2(queue_l, 0, i2s1, 0);

int16_t samples_l[AMY_BLOCK_SIZE];
int16_t samples_r[AMY_BLOCK_SIZE];

AMY amy;

void setup() {


  AudioMemory(10);
  queue_l.setMaxBuffers(4);
  queue_r.setMaxBuffers(4);

  Serial.begin(115200);
  while (!Serial && millis() < 10000UL);
  Serial.println("Welcome to AMY example");

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
  int16_t * samples = amy.render_to_buffer();
  for(int16_t i=0;i<AMY_BLOCK_SIZE;i++) {
    samples_l[i] = samples[i*2];
    samples_r[i] = samples[i*2+1];
  }
  queue_l.play(samples_l, AMY_BLOCK_SIZE);
  queue_r.play(samples_r, AMY_BLOCK_SIZE);
}

