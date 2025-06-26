// i2s.c
// handle i2s audio in & out on many platforms
// esp32 --> esp32, esp32-s3, esp32-p4 
// AMYBOARD, which is a esp32s3 but with special i2s setup
// rp2350, rp2040
// teensy 3.6, 4.0, 4.1

// Only run this code on MCUs
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO) || defined(ARDUINO_ARCH_RP2350)

#include "amy.h"


#ifdef ESP_PLATFORM
#include <esp_task.h>

///////////////////////////////////////////////////////////////
// ESP32, S3, P4 (maybe others)


TaskHandle_t amy_render_handle;
TaskHandle_t amy_fill_buffer_handle;

#include "driver/i2s_std.h"
i2s_chan_handle_t tx_handle;
i2s_chan_handle_t rx_handle;


#ifndef AMYBOARD
#warning NOT_AMYBOARD
// default ESP setup i2s
amy_err_t setup_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    if(AMY_HAS_AUDIO_IN) {
        i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    } else {
        i2s_new_channel(&chan_cfg, &tx_handle, NULL);        
    }
// PCM5101 DAC works at either 32 bit or (default) 16 bit
// PCM1808 ADC needs I2S_32BIT to work
#define I2S_32BIT
#ifdef I2S_32BIT
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AMY_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (amy_global.config.i2s_mclk == -1)? I2S_GPIO_UNUSED : amy_global.config.i2s_mclk,
            .bclk = amy_global.config.i2s_bclk,
            .ws = amy_global.config.i2s_lrc,
            .dout = amy_global.config.i2s_dout,
            .din = amy_global.config.i2s_din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
#else // 16 bit I2S
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AMY_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (amy_global.config.i2s_mclk == -1)? I2S_GPIO_UNUSED : amy_global.config.i2s_mclk,
            .bclk = amy_global.config.i2s_bclk,
            .ws = amy_global.config.i2s_lrc,
            .dout = amy_global.config.i2s_dout,
            .din = amy_global.config.i2s_din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
#endif
    /* Initialize the channel */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if(AMY_HAS_AUDIO_IN) i2s_channel_init_std_mode(rx_handle, &std_cfg);

    /* Before writing data, start the TX channel first */
    i2s_channel_enable(tx_handle);
    if(AMY_HAS_AUDIO_IN) i2s_channel_enable(rx_handle);
    return AMY_OK;
}

#else
// AMYBOARD i2s setup, which is weird
#warning AMYBOARD
amy_err_t setup_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_SLAVE);  // ************* I2S_ROLE_SLAVE - needs external I2S clock input.
    i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);

#define I2S_32BIT
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AMY_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_EXTERNAL,
            .ext_clk_freq_hz = AMY_SAMPLE_RATE * 512,
            .mclk_multiple = 512, 
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = 32,
            .ws_pol = false, // false in STD_PHILIPS macro
            .bit_shift = false, // true for STD_PHILIPS macro, but that results in *2* bits delay of dout vs lrclk in Follower mode. false gives 1 bit delay, as expected for i2s.
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = amy_global.config.i2s_mclk, 
            .bclk = amy_global.config.i2s_bclk,
            .ws = amy_global.config.i2s_lrc,
            .dout = amy_global.config.i2s_dout,
            .din = amy_global.config.i2s_din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = true, // invert bclk for pcm9211 
                .ws_inv = false,
            },
        },
    };
    /* Initialize the channel */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);
    i2s_channel_init_std_mode(rx_handle, &std_cfg);

    /* Before writing data, start the TX channel first */
    i2s_channel_enable(tx_handle);
    i2s_channel_enable(rx_handle);
    return AMY_OK;
}

#endif

// Render the second core
void esp_render_task( void * pvParameters) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        amy_render(0, AMY_OSCS/2, 1);
        xTaskNotifyGive(amy_fill_buffer_handle);
    }
}

#ifdef I2S_32BIT
  static int32_t block32[AMY_BLOCK_SIZE * AMY_NCHANS];
  #define I2S_BYTES_PER_SAMPLE 4
#else
  #define I2S_BYTES_PER_SAMPLE AMY_BYTES_PER_SAMPLE
#endif
extern output_sample_type * amy_in_block;

// Make AMY's FABT run forever , as a FreeRTOS task 
void esp_fill_audio_buffer_task() {
    while(1) {
        AMY_PROFILE_START(AMY_ESP_FILL_BUFFER)
        if(AMY_HAS_AUDIO_IN) {
            size_t read = 0;
#ifdef I2S_32BIT
            i2s_channel_read(rx_handle, block32, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int32_t), &read, portMAX_DELAY);
            for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)
                amy_in_block[i] = (block32[i] >> 16);
#else
	    i2s_channel_read(rx_handle, amy_in_block, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(output_sample_type), &read, portMAX_DELAY);
#endif
	    if(read != AMY_BLOCK_SIZE * AMY_NCHANS * I2S_BYTES_PER_SAMPLE) {
		fprintf(stderr,"i2s input underrun: %d vs %d\n", read, AMY_BLOCK_SIZE * AMY_NCHANS * I2S_BYTES_PER_SAMPLE);
	    }
	}
        // Get ready to render
        amy_execute_deltas();
        // Tell the other core to start rendering
        xTaskNotifyGive(amy_render_handle);
        // Render me
        amy_render(AMY_OSCS/2, AMY_OSCS, 0);
    // Wait for the other core to finish
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Write to i2s
        output_sample_type *block = amy_fill_buffer();

	AMY_PROFILE_STOP(AMY_ESP_FILL_BUFFER)

        size_t written = 0;

#ifdef I2S_32BIT // including AMYBOARD
	// Convert to 32 bits
	for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)
	    block32[i] = ((int32_t)block[i]) << 16;
        i2s_channel_write(tx_handle, block32, AMY_BLOCK_SIZE * AMY_NCHANS * I2S_BYTES_PER_SAMPLE, &written, portMAX_DELAY);
#else  // 16 bit I2S
        i2s_channel_write(tx_handle, block, AMY_BLOCK_SIZE * AMY_NCHANS * I2S_BYTES_PER_SAMPLE, &written, portMAX_DELAY);

#endif

        if(written != AMY_BLOCK_SIZE * AMY_NCHANS * I2S_BYTES_PER_SAMPLE) {
            fprintf(stderr,"i2s output underrun: %d vs %d\n", written, AMY_BLOCK_SIZE * AMY_NCHANS * I2S_BYTES_PER_SAMPLE);
        }
    }
}

// init AMY from the esp. wraps some amy funcs in a task to do multicore rendering on the ESP32 
amy_err_t i2s_amy_init() {
    // Start i2s
    setup_i2s();

    // Create the second core rendering task
    xTaskCreatePinnedToCore(&esp_render_task, AMY_RENDER_TASK_NAME, AMY_RENDER_TASK_STACK_SIZE, NULL, AMY_RENDER_TASK_PRIORITY, &amy_render_handle, AMY_RENDER_TASK_COREID);

    // And the fill audio buffer thread, combines, does volume & filters
    xTaskCreatePinnedToCore(&esp_fill_audio_buffer_task, AMY_FILL_BUFFER_TASK_NAME, AMY_FILL_BUFFER_TASK_STACK_SIZE, NULL, AMY_FILL_BUFFER_TASK_PRIORITY, &amy_fill_buffer_handle, AMY_FILL_BUFFER_TASK_COREID);
    return AMY_OK;
}

void amy_update() {
    // does nothing on esp
}

#elif (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2350)

#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico-audio/audio_i2s.h"
#include "pico/binary_info.h"
#include "pico/util/queue.h"

struct audio_buffer_pool *ap;

static inline uint32_t _millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

typedef struct
{
    int32_t (*func)(int32_t);
    int32_t data;
} queue_entry_t;

queue_t call_queue;
queue_t results_queue;


int32_t render_other_core(int32_t data) {
    amy_render(AMY_OSCS/2, AMY_OSCS, 1);
    return AMY_OK;
}

extern void on_pico_uart_rx();


void amy_poll_tasks() {
    //if (ap->free_list == NULL) {
    amy_execute_deltas();
    if(amy_global.config.midi & AMY_MIDI_IS_UART) on_pico_uart_rx();
#ifdef TUD_USB_GADGET
    if(amy_global.config.midi & AMY_MIDI_IS_USB_GADGET) on_pico_uart_rx();
#endif
    //}
}

int16_t *amy_render_audio() {
    //if (ap->free_list != NULL) {
    int32_t res;
    queue_entry_t entry = {render_other_core, AMY_OK};
    queue_add_blocking(&call_queue, &entry);
    amy_render(0, AMY_OSCS/2, 0);
    queue_remove_blocking(&results_queue, &res);
    int16_t *block = amy_fill_buffer();
    return block;
}

void amy_pass_to_i2s(int16_t *block) {
    size_t written = 0;
    struct audio_buffer *buffer = take_audio_buffer(ap, true);
    int16_t *samples = (int16_t *) buffer->buffer->bytes;
    for (uint i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; i++) {
	samples[i] = block[i]; // (vol * sine_wave_table[pos >> 16u]) >> 8u;
    }
    buffer->sample_count = AMY_BLOCK_SIZE;
    give_audio_buffer(ap, buffer);
}

void amy_update() {
    // Single function to update buffers.
    amy_poll_tasks();
    int16_t *block = amy_render_audio();
    amy_pass_to_i2s(block);
}


struct audio_buffer_pool *init_audio() {
    static audio_format_t audio_format = {
            .format = AUDIO_BUFFER_FORMAT_PCM_S16,
            .sample_freq = AMY_SAMPLE_RATE,
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
            .data_pin = amy_global.config.i2s_dout,
            .clock_pin_base = amy_global.config.i2s_bclk,
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
    while (1) {
        queue_entry_t entry;
        queue_remove_blocking(&call_queue, &entry);
        int32_t result = entry.func(entry.data);
        queue_add_blocking(&results_queue, &result);
    }
}


amy_err_t i2s_amy_init() {
    queue_init(&call_queue, sizeof(queue_entry_t), 2);
    queue_init(&results_queue, sizeof(int32_t), 2);
    uint32_t * core1_separate_stack_address = (uint32_t*)malloc(0x2000);
    multicore_launch_core1_with_stack(core1_main, core1_separate_stack_address, 0x2000);
    sleep_ms(500);
    ap = init_audio();
    return AMY_OK;
}




#elif defined __IMXRT1062__


extern void teensy_setup_i2s();
extern void teensy_i2s_send(int16_t *samples);

extern int16_t teensy_get_serial_byte();

amy_err_t i2s_amy_init() {
    teensy_setup_i2s();
    return AMY_OK;
}

void amy_update() {
    if(amy_config.global.midi & AMY_MIDI_IS_UART) {
        // do midi in here
        uint8_t bytes[1];
        int t;
        while((t = teensy_get_serial_byte()) >= 0) {
            bytes[0] = t;
            convert_midi_bytes_to_messages(bytes,1,0);
        }
    }
    amy_execute_deltas();
    amy_render(0, AMY_OSCS, 0);
    int16_t *block = amy_fill_buffer();
    teensy_i2s_send(block);
}




#else

//...

#endif



void amy_live_start() {
    amy_global.running = 1;
    i2s_amy_init();
}


void amy_live_stop() {
    amy_global.running = 0;
    // Not sure we do anythign on mcus for stopping amy live
}


#endif
