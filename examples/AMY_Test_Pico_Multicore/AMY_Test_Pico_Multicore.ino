#include <AMY-Arduino.h>
#include <I2S.h>

// This example renders complex FM tones on both processors of a Pi Pico. 
// Set this to CPU 250MHz (overclocked) and -O3
// You can get 11 or 12 multitimbral DX7-style voices at once.

#define CORE0_RENDER_SIGNAL 0x20
#define CORE1_RENDER_SIGNAL 0x10
#define CORE_NO_SIGNAL 0x00
#define MAX_NOTES 12
#define TIME_BETWEEN_NOTES_MS 2000

I2S i2s(OUTPUT);
AMY amy;
int note_on_ms = 0;
int note_counter = 0;

void setup() {
  // Set your I2S pins. 
  i2s.setBCLK(9); // BCLK = SCLK. MCLK = SCLK + 1
  i2s.setDATA(8); // DATA = DIN, SD, SDOUT, DOUT
  i2s.setBitsPerSample(16); 
  i2s.setBuffers(2, AMY_BLOCK_SIZE*AMY_NCHANS*BYTES_PER_SAMPLE/4, 0);
  i2s.begin(AMY_SAMPLE_RATE);

  // Start up AMY
  amy.begin(2, 0, 0);

  amy.volume(1);

}

void setup1() {
  // Nothing needed here
}

// The "main" core prepares the oscillators and then renders half
// When the 2nd core finishes rendering its half, it also joins the renderers and outputs to I2S.
void loop() {

  // First, play some notes if it's time to.
  if(note_counter < MAX_NOTES) {
    if(amy.sysclock() > note_on_ms) {
      note_on_ms = amy.sysclock() + TIME_BETWEEN_NOTES_MS;
      struct event e = amy.default_event();
      e.load_patch = note_counter + 128; // dx7 patches start at 128
      strcpy(e.voices, "0");
      e.time = amy.sysclock(); 
      amy_add_event(e);

      e = amy.default_event();
      e.midi_note = 50 + (note_counter*2);
      e.velocity = 1;
      e.time = amy.sysclock(); // play it now
      amy.add_event(e);
      note_counter++;
    }
  }

  // Now render from this core.

  byte signal = CORE_NO_SIGNAL;
  // For multicore, per block of samples, you have to:
    // prepare the block once (on the main core),
    // render a set of oscillators (split between both cores),
    // get the combined buffer from the main core,
    // play that buffer to audio output / i2s (main core)
  
  amy.prepare();

  // Tell the 2nd core it's ok to render
  rp2040.fifo.push(CORE0_RENDER_SIGNAL);

  // Render half the oscillators. You can give core1 more of these, as it does less work than core0
  // This says: render oscs 0-59 on core 0.
  amy.render(0, 30, 0); 

  // Wait for core1 to be done
  while(signal != CORE1_RENDER_SIGNAL) signal = rp2040.fifo.pop();

  // Now get the completed buffer from both cores
  uint8_t * samples = (uint8_t*)amy.fill_buffer();

  // Write, block until i2s is ready
  while(i2s.availableForWrite()<AMY_BLOCK_SIZE*BYTES_PER_SAMPLE*AMY_NCHANS);
  size_t written = i2s.write((const uint8_t*)samples, (size_t)AMY_BLOCK_SIZE*BYTES_PER_SAMPLE*AMY_NCHANS);
}

// This runs on core1 (the second core), rendering half the oscillators
void loop1() {
  byte signal = CORE_NO_SIGNAL;

  // Wait for a signal from the first core to start rendering
  while(signal != CORE0_RENDER_SIGNAL) signal = rp2040.fifo.pop();
  
  // Render the second half -- render from 60 to 119 on core 1.
  amy.render(30,  AMY_OSCS, 1); 

  // Tell the other core we're done
  rp2040.fifo.push(CORE1_RENDER_SIGNAL);
}
