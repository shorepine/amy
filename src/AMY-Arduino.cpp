// AMY-Arduino.cpp
// connector for Arduino

#include "Arduino.h"
#include "AMY-Arduino.h"

#ifdef ARDUINO_ARCH_RP2040

bi_decl(bi_3pins_with_names(8, "I2S DIN", 9, "I2S BCK", 10, "I2S LRCK"));

extern "C" int32_t await_message_from_other_core();
extern "C" void send_message_to_other_core(int32_t t);

static inline uint32_t _millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}
void delay_ms(uint32_t ms) {
    uint32_t now = _millis();
    while(_millis() < now+ms) {}
}

void rp2040_fill_audio_buffer(struct audio_buffer_pool *ap) {
    int16_t *block = fill_audio_buffer_task();
    size_t written = 0;
    struct audio_buffer *buffer = take_audio_buffer(ap, true);
    int16_t *samples = (int16_t *) buffer->buffer->bytes;
    for (uint i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; i++) {
        samples[i] = block[i]; // (vol * sine_wave_table[pos >> 16u]) >> 8u;
    }
    buffer->sample_count = AMY_BLOCK_SIZE;
    give_audio_buffer(ap, buffer);

}

struct audio_buffer_pool *init_audio() {
    static audio_format_t audio_format = {
            .sample_freq = AMY_SAMPLE_RATE,
            .format = AUDIO_BUFFER_FORMAT_PCM_S16,
            .channel_count = AMY_NCHANS,
    };

    static struct audio_buffer_format producer_format = {
            .format = &audio_format,
            .sample_stride = sizeof(int16_t) * AMY_NCHANS,
    };

    struct audio_buffer_pool *producer_pool = audio_new_producer_pool(&producer_format, 3, AMY_BLOCK_SIZE);

    bool __unused ok;
    const struct audio_format *output_format;
    struct audio_i2s_config config = {
            .data_pin = 8,
            .clock_pin_base = 9,
            .dma_channel = 0,
            .pio_sm = 0,
    };

    output_format = audio_i2s_setup(&audio_format, &config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    audio_i2s_set_enabled(true);

    return producer_pool;
}


void core1_main() {
    while(1) {
        int32_t ret = 0;
        while(ret !=32) ret = multicore_fifo_pop_blocking();
        render_task(AMY_OSCS/2, AMY_OSCS, 1);
        multicore_fifo_push_blocking(64);
    }

}


#endif

#ifdef ESP_PLATFORM

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_intr_alloc.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "driver/i2s.h"

#define I2S_SAMPLE_TYPE I2S_BITS_PER_SAMPLE_16BIT

#define ESP_INTR_FLAG_DEFAULT 0
#define CONFIG_I2S_LRCLK 4
#define CONFIG_I2S_BCLK 1
#define CONFIG_I2S_DIN 2
#define CONFIG_I2S_NUM 0 

#define AMY_EXAMPLE_RENDER_TASK_PRIORITY (ESP_TASK_PRIO_MIN + 1)
#define AMY_EXAMPLE_FILL_BUFFER_TASK_PRIORITY (ESP_TASK_PRIO_MIN + 4)

#define AMY_EXAMPLE_RENDER_0_TASK_COREID (1)
#define AMY_EXAMPLE_RENDER_1_TASK_COREID (0)
#define AMY_EXAMPLE_FILL_BUFFER_TASK_COREID (1)

#define AMY_EXAMPLE_RENDER_TASK_STACK_SIZE (8 * 1024)
#define AMY_EXAMPLE_FILL_BUFFER_TASK_STACK_SIZE (4 * 1024)

#define AMY_EXAMPLE_RENDER_0_TASK_NAME      "amy_r0_task"
#define AMY_EXAMPLE_RENDER_1_TASK_NAME      "amy_r1_task"
#define AMY_EXAMPLE_FILL_BUFFER_TASK_NAME   "amy_fb_task"

SemaphoreHandle_t xQueueSemaphore;
TaskHandle_t amy_render_handle[AMY_CORES]; // one per core
TaskHandle_t amy_fill_buffer_handle;

void delay_ms(uint32_t ms) {
      vTaskDelay(pdMS_TO_TICKS(ms));
}



// Wrap AMY's renderer into 2 FreeRTOS tasks, one per core
void esp_render_task( void * pvParameters) {
    uint8_t which = *((uint8_t *)pvParameters);
    uint8_t start = 0; 
    uint8_t end = AMY_OSCS;
    if (AMY_CORES == 2) {
        start = (AMY_OSCS/2); 
        end = AMY_OSCS;
        if(which == 0) { start = 0; end = (AMY_OSCS/2); } 
    }
    fprintf(stderr,"I'm renderer #%d on core #%d and i'm handling oscs %d up until %d\n", which, xPortGetCoreID(), start, end);
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        render_task(start, end, which);
        xTaskNotifyGive(amy_fill_buffer_handle);
    }
}

// Make AMY's FABT run forever , as a FreeRTOS task, writing to i2s
void esp_fill_audio_buffer_task(void*) {
    while(1) {
        int16_t *block = fill_audio_buffer_task();
        size_t written = 0;
        i2s_write((i2s_port_t)CONFIG_I2S_NUM, block, AMY_BLOCK_SIZE * BYTES_PER_SAMPLE, &written, portMAX_DELAY); 
        if(written != AMY_BLOCK_SIZE*BYTES_PER_SAMPLE) {
            fprintf(stderr,"i2s underrun: %d vs %d\n", written, AMY_BLOCK_SIZE*BYTES_PER_SAMPLE);
        }
    }
}


// init AMY from the esp. wraps some amy funcs in a task to do multicore rendering on the ESP32 
amy_err_t esp_amy_init() {
    amy_start();
    // We create a mutex for changing the event queue and pointers as two tasks do it at once
    xQueueSemaphore = xSemaphoreCreateMutex();

    // Create rendering threads, one per core so we can deal with dan ellis float math
    static uint8_t zero = 0;
    static uint8_t one = 1;
    xTaskCreatePinnedToCore(&esp_render_task, AMY_EXAMPLE_RENDER_0_TASK_NAME, AMY_EXAMPLE_RENDER_TASK_STACK_SIZE, &zero, AMY_EXAMPLE_RENDER_TASK_PRIORITY, &amy_render_handle[0], AMY_EXAMPLE_RENDER_0_TASK_COREID);
    if(AMY_CORES>1) xTaskCreatePinnedToCore(&esp_render_task, AMY_EXAMPLE_RENDER_1_TASK_NAME, AMY_EXAMPLE_RENDER_TASK_STACK_SIZE, &one, AMY_EXAMPLE_RENDER_TASK_PRIORITY, &amy_render_handle[1], AMY_EXAMPLE_RENDER_1_TASK_COREID);
    // Wait for the render tasks to get going before starting the i2s task
    delay_ms(100);

    // And the fill audio buffer thread, combines, does volume & filters
    xTaskCreatePinnedToCore(&esp_fill_audio_buffer_task, AMY_EXAMPLE_FILL_BUFFER_TASK_NAME, AMY_EXAMPLE_FILL_BUFFER_TASK_STACK_SIZE, NULL, AMY_EXAMPLE_FILL_BUFFER_TASK_PRIORITY, &amy_fill_buffer_handle, AMY_EXAMPLE_FILL_BUFFER_TASK_COREID);
    return AMY_OK;
}


// Setup I2S on esp32
amy_err_t setup_i2s(void) {
    //i2s configuration
    i2s_config_t i2s_config = {
         .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
         .sample_rate = AMY_SAMPLE_RATE,
         .bits_per_sample = I2S_SAMPLE_TYPE,
         .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
         .communication_format = I2S_COMM_FORMAT_STAND_MSB,
         .intr_alloc_flags = 0, 
         .dma_buf_count = 2, 
         .dma_buf_len = 1024, 
         .tx_desc_auto_clear = true,
        };
        
    i2s_pin_config_t pin_config = {
        .bck_io_num = CONFIG_I2S_BCLK, 
        .ws_io_num = CONFIG_I2S_LRCLK,  
        .data_out_num = CONFIG_I2S_DIN, 
        .data_in_num = -1   //Not used
    };

    i2s_driver_install((i2s_port_t)CONFIG_I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin((i2s_port_t)CONFIG_I2S_NUM, &pin_config);
    i2s_set_sample_rates((i2s_port_t)CONFIG_I2S_NUM, AMY_SAMPLE_RATE);
    return AMY_OK;
}



#endif



// TODO -- see how other synth libraries deal with different audio output & pins, I2S, DAC, analog, etc
AMY::AMY()
{

}

struct event AMY::default_event() {
    return amy_default_event();
}

void AMY::add_event(struct event e) {
    amy_add_event(e);
}

int64_t AMY::sysclock() {
    return amy_sysclock();
}

void AMY::begin()
{

#ifdef ARDUINO_ARCH_RP2040
    set_sys_clock_khz(250000000 / 1000, false); 
    #if AMY_CORES == 2
        multicore_launch_core1(core1_main);
    #endif   
    sleep_ms(500);
    amy_start();
    ap = init_audio();
#elif ESP_PLATFORM

    setup_i2s();
    esp_amy_init();


#endif
}

void AMY::fm(int64_t start) {
    example_multimbral_fm(start);
}

void AMY::send_message(char * message) {

}

void AMY::render() {
#ifdef ARDUINO_ARCH_RP2040
    rp2040_fill_audio_buffer(ap);
#elif ESP_PLATFORM
    // nothing, it just happens
#endif
}





