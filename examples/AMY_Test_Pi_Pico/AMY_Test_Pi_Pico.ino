#include <AMY-Arduino.h>
#include <I2S.h>

I2S i2s(OUTPUT);
AMY amy;

void setup() {

  // Set your I2S pins. 
  i2s.setBCLK(9); // BCLK = SCLK. MCLK = SCLK + 1
  i2s.setDATA(8); // DATA = DIN, SD, SDOUT, DOUT
  i2s.setBitsPerSample(16); 
  i2s.setBuffers(2, AMY_BLOCK_SIZE*AMY_NCHANS*BYTES_PER_SAMPLE/4, 0);
  i2s.begin(AMY_SAMPLE_RATE);


  Serial.begin(115200);
  while (!Serial && millis() < 10000UL);
  Serial.println("Welcome to AMY example");

  // Start up AMY
  amy.begin(1, 0, 0);

  // Send AMY messages in two ways: first, using the wire protocol:
  // This sends a sine wave at 220Hz on osc 0 with velocity 1
  amy.send_message((char*)"v0f220l1");
  
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
  uint8_t * samples = (uint8_t*)amy.render_to_buffer();
  // Block until ready
  while(i2s.availableForWrite()<AMY_BLOCK_SIZE*BYTES_PER_SAMPLE*AMY_NCHANS);
  size_t written = i2s.write((const uint8_t*)samples, (size_t)AMY_BLOCK_SIZE*BYTES_PER_SAMPLE*AMY_NCHANS);
  Serial.println(written);
}

