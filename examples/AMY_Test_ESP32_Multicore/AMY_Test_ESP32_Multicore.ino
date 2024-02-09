#include <AMY-Arduino.h>
#include <I2S.h>

#define MAX_NOTES 12
#define TIME_BETWEEN_NOTES_MS 2000
int note_on_ms = 0;
int note_counter = 0;


AMY amy;

TaskHandle_t render0;
TaskHandle_t render1;

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

  // Set up the rendering tasks on the ESP.
  xTaskCreatePinnedToCore(render_0, "render_0",4096,NULL, configMAX_PRIORITIES-2,&render0,0);                          
  xTaskCreatePinnedToCore(render_1, "render_1",4096,NULL, configMAX_PRIORITIES-2,&render1,1); 
  amy.begin(2, 0, 0);
}

void render_0( void * pvParameters ){
  // Loop forever
  for(;;){
    // Prepare to render this block
    amy.prepare();

    // Tell render_1 it's time to render
    xTaskNotifyGive(render1);

    // Render half or so of the oscillators
    amy.render(0,AMY_OSCS/2,0);

    // Wait for render_1 to come back
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

    // Get the buffer and play it.
    short * samples = amy.fill_buffer();
    I2S.write_blocking(samples, AMY_BLOCK_SIZE*AMY_NCHANS*BYTES_PER_SAMPLE);
  } 
}

void render_1( void * pvParameters ){
  // Loop forever
  for(;;){
    // Wait for render_0 to tell us to go
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

    // Render the other half of the processors
    amy.render(AMY_OSCS/2, AMY_OSCS, 1);

    // Tell render_0 we're done
    xTaskNotifyGive(render0);
  }
}

void loop() {
  // Update some notes
  // First, play some notes if it's time to.
  if(note_counter < MAX_NOTES) {
    if(amy.sysclock() > note_on_ms) {
      note_on_ms = amy.sysclock() + TIME_BETWEEN_NOTES_MS;
      struct event e = amy.default_event();
      e.load_patch = note_counter + 128; // dx7 patches start at 128
      sprintf(e.voices, "%d", note_counter);
      e.midi_note = 50 + (note_counter*2);
      e.velocity = 1;
      amy.add_event(e);
      note_counter++;
    }
  }
}
