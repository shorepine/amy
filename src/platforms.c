// platforms.c
// functions to start AMY in esp32, esp32s3, esp32p4, rp2040, rp2530, posix, webassembly

#include "platforms.h"

uint8_t external_map[AMY_OSCS];

#ifdef ESP_PLATFORM
// mutex that locks writes to the delta queue
SemaphoreHandle_t xQueueSemaphore;

#else

#endif

// AMY synth states
extern struct state global;
extern uint32_t event_counter;
extern uint32_t message_counter;



#ifdef ESP_PLATFORM
#include "driver/i2s_std.h"
#include "driver/gpio.h"
i2s_chan_handle_t tx_handle;


// Render the second core
void esp_render_task( void * pvParameters) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        amy_render(0, AMY_OSCS/2, 1);
        xTaskNotifyGive(alles_fill_buffer_handle);
    }
}

// Make AMY's FABT run forever , as a FreeRTOS task 
void esp_fill_audio_buffer_task() {
    while(1) {
        AMY_PROFILE_START(AMY_ESP_FILL_BUFFER)

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
        AMY_PROFILE_STOP(AMY_ESP_FILL_BUFFER)

        size_t written = 0;
        i2s_channel_write(tx_handle, block, AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS, &written, portMAX_DELAY);
        if(written != AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS) {
            fprintf(stderr,"i2s underrun: %d vs %d\n", written, AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS);
        }
    }
}

#else

#endif


extern char *message_start_pointer;
extern int16_t message_length;


#ifdef ESP_PLATFORM
// init AMY from the esp. wraps some amy funcs in a task to do multicore rendering on the ESP32 
amy_err_t esp_amy_init() {
    sync_init();
    amy_start(2,1,1,1);
    // We create a mutex for changing the event queue and pointers as two tasks do it at once
    xQueueSemaphore = xSemaphoreCreateMutex();

    // Create the second core rendering task
    xTaskCreatePinnedToCore(&esp_render_task, ALLES_RENDER_TASK_NAME, ALLES_RENDER_TASK_STACK_SIZE, NULL, ALLES_RENDER_TASK_PRIORITY, &amy_render_handle, ALLES_RENDER_TASK_COREID);

    // Wait for the render tasks to get going before starting the i2s task
    delay_ms(100);

    // And the fill audio buffer thread, combines, does volume & filters
    xTaskCreatePinnedToCore(&esp_fill_audio_buffer_task, ALLES_FILL_BUFFER_TASK_NAME, ALLES_FILL_BUFFER_TASK_STACK_SIZE, NULL, ALLES_FILL_BUFFER_TASK_PRIORITY, &alles_fill_buffer_handle, ALLES_FILL_BUFFER_TASK_COREID);
    return AMY_OK;
}
#else

extern void *miniaudio_run(void *vargp);

#include <pthread.h>
amy_err_t unix_amy_init() {
    sync_init();
    amy_start(1,1,1,1);
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, miniaudio_run, NULL);
    return AMY_OK;
}
#endif


#ifdef ESP_PLATFORM

amy_err_t setup_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AMY_SAMPLE_RATE),
        //#ifndef TDECK
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        //#else
        //.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        //#endif
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_I2S_BCLK,
            .ws = CONFIG_I2S_LRCLK,
            .dout = CONFIG_I2S_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* Initialize the channel */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);

    /* Before writing data, start the TX channel first */
    i2s_channel_enable(tx_handle);
    fprintf(stderr, "i2s started blck %d dout %d lrck %d\n",CONFIG_I2S_BCLK,CONFIG_I2S_DIN,CONFIG_I2S_LRCLK );
    return AMY_OK;
}


#endif



#ifdef ESP_PLATFORM
#include "driver/i2c.h"

// An AMY hook to send values out to a CV DAC over i2c, only on ESP 
uint8_t external_cv_render(uint16_t osc, SAMPLE * buf, uint16_t len) {
    if(external_map[osc]>0) {
        float volts = S2F(buf[0])*2.5f + 2.5f;
        // do the thing
        uint16_t value_int = (uint16_t)((volts/10.0) * 65535.0);
        uint8_t bytes[3];
        bytes[2] = (value_int & 0xff00) >> 8;
        bytes[1] = (value_int & 0x00ff);

        uint8_t ch = 0x02;
        uint8_t addr = 89;
        uint8_t channel = external_map[osc]-1;
        if(channel == 1) ch = 0x04;
        if(channel == 2) addr = 88;
        if(channel == 3) {ch = 0x04; addr=88; }
        bytes[0] = ch;
        i2c_master_write_to_device(I2C_NUM_0, addr, bytes, 3, pdMS_TO_TICKS(10));
        return 1;
    }
    return 0;

}
void run_alles() {
    check_init(&esp_event_loop_create_default, "Event");
    check_init(&setup_i2s, "i2s");
    // clear the external map
    for(uint16_t i=0;i<AMY_OSCS;i++) external_map[i] = 0;
    amy_external_render_hook = external_cv_render;

    esp_amy_init();
    amy_reset_oscs();
}

#else

void * alles_start(void *vargs) {
    alles_local_ip = malloc(256);
    alles_local_ip[0] = 0;
    unix_amy_init();
    amy_reset_oscs();
    // Spin this core until the power off button is pressed, parsing events and making sounds
    while(status & RUNNING) {
        delay_ms(10);
    }
    return 0;
}

#endif

#ifdef ESP_PLATFORM
// Make AMY's parse task run forever, as a FreeRTOS task (with notifications)
void esp_parse_task() {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        alles_parse_message(message_start_pointer, message_length);
        xTaskNotifyGive(alles_receive_handle);
    }
}
#endif






