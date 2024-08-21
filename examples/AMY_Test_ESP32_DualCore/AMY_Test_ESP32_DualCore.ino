#include <AMY-Arduino.h>
#include <ESP_I2S.h>

AMY amy;
I2SClass I2S;

#define CONFIG_I2S_BCLK 13 
#define CONFIG_I2S_LRCLK 12
#define CONFIG_I2S_DIN 11


// mutex that locks writes to the delta queue
SemaphoreHandle_t xQueueSemaphore;

// Task handles 
TaskHandle_t amy_render_handle;
TaskHandle_t amy_fill_buffer_handle;

#define AMY_TASK_COREID (1)
#define AMY_RENDER_TASK_COREID (0)
#define AMY_FILL_BUFFER_TASK_COREID (1)
#define AMY_RENDER_TASK_PRIORITY (ESP_TASK_PRIO_MAX-1 )
#define AMY_FILL_BUFFER_TASK_PRIORITY (ESP_TASK_PRIO_MAX-1)
#define AMY_TASK_NAME             "alles_task"
#define AMY_RENDER_TASK_NAME      "alles_r_task"
#define AMY_FILL_BUFFER_TASK_NAME "alles_fb_task"
#define AMY_TASK_STACK_SIZE    (8 * 1024) 
#define AMY_RENDER_TASK_STACK_SIZE (8 * 1024)
#define AMY_FILL_BUFFER_TASK_STACK_SIZE (8 * 1024)

// Play 10 Juno-6 notes 
void polyphony(uint32_t start, uint16_t patch) {
    struct event e = amy.default_event();
    e.time = start;
    e.load_patch = patch;
    strcpy(e.voices, "0,1,2,3,4,5,6,7,8,9");
    amy.add_event(e);
    start += 250;
    uint8_t note = 40;
    for(uint8_t i=0;i<10;i++) {
        e = amy.default_event();
        e.time = start;
        e.velocity=0.5;
        sprintf(e.voices, "%d", i);
        e.midi_note = note;
        amy.add_event(e);
        start += 1000;
        note += 2;
    }
}   

// Render the second core
void* esp_render_task( void *) {
    while(1) {
        // Wait for a message that it's time to start rendering
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        amy_render(0, AMY_OSCS/2, 1);
        // Tell the other core we're done rendering
        xTaskNotifyGive(amy_fill_buffer_handle);
    }
}

// Make AMY's FABT run forever , as a FreeRTOS task 
void* esp_fill_audio_buffer_task(void*) {
    while(1) {
        // Get ready to render
        amy_prepare_buffer();
        // Tell the other core to start rendering
        xTaskNotifyGive(amy_render_handle);
        // Render me
        amy_render(AMY_OSCS/2, AMY_OSCS, 0);
        // Wait for the other core to finish
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Write to i2s
        int16_t *block = amy_fill_buffer();
        I2S.write((uint8_t*)block, AMY_BLOCK_SIZE*AMY_NCHANS*BYTES_PER_SAMPLE);

    }
}


void setup() {
  // This is if you're using an Alles board, you have to poke 5V_EN to turn on the speaker
  // You can ignore this if you're not using an Alles
  pinMode(21, OUTPUT);
  digitalWrite(21, 1);

  // Set your I2S pins. 
  I2S.setPins(CONFIG_I2S_BCLK, CONFIG_I2S_LRCLK, CONFIG_I2S_DIN, -1, -1);
  I2S.begin(I2S_MODE_STD, AMY_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO); // I2S.begin(I2S_PHILIPS_MODE, AMY_SAMPLE_RATE, BYTES_PER_SAMPLE8);

  // Start up AMY
  amy.begin(/* cores= */ 2, /* reverb= */ 1, /* chorus= */ 1);

  // We create a mutex for changing the event queue and pointers as two tasks do it at once
  xQueueSemaphore = xSemaphoreCreateMutex();

  // Create the second core rendering task
  xTaskCreatePinnedToCore((TaskFunction_t)esp_render_task, AMY_RENDER_TASK_NAME, AMY_RENDER_TASK_STACK_SIZE, NULL, AMY_RENDER_TASK_PRIORITY, &amy_render_handle, AMY_RENDER_TASK_COREID);

  // Wait for the render tasks to get going before starting the i2s task
  delay(100);

  // And the fill audio buffer thread, combines, does volume & filters
  xTaskCreatePinnedToCore((TaskFunction_t)esp_fill_audio_buffer_task, AMY_FILL_BUFFER_TASK_NAME, AMY_FILL_BUFFER_TASK_STACK_SIZE, NULL, AMY_FILL_BUFFER_TASK_PRIORITY, &amy_fill_buffer_handle, AMY_FILL_BUFFER_TASK_COREID);

  amy_reset_oscs();
  polyphony(amy.sysclock(), 0);

}


void loop() {
  // nothing -- the audio is rendered in the esp_fill_audio_buffer_task
}

