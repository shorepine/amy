// i2s.c
// handle i2s audio in & out on many platforms
// esp32 --> esp32, esp32-s3, esp32-p4 
// AMYBOARD, which is a esp32s3 but with special i2s setup
// rp2350, rp2040
// teensy 3.6, 4.0, 4.1

// Only run this code on MCUs
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO) 

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

// default ESP setup i2s
amy_err_t setup_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    if(amy_global.config.has_audio_in) {
        i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    } else {
        i2s_new_channel(&chan_cfg, &tx_handle, NULL);        
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AMY_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
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
    /* Initialize the channel */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if(amy_global.config.has_audio_in) i2s_channel_init_std_mode(rx_handle, &std_cfg);

    /* Before writing data, start the TX channel first */
    i2s_channel_enable(tx_handle);
    if(amy_global.config.has_audio_in) i2s_channel_enable(rx_handle);
    return AMY_OK;
}

#else
// AMYBOARD i2s setup, which is weird
amy_err_t setup_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_SLAVE);  // ************* I2S_ROLE_SLAVE - needs external I2S clock input.
    //fprintf(stderr, "creating I2S channel\n");
    i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);

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
        output_sample_type *block = amy_fill_buffer();
        AMY_PROFILE_STOP(AMY_ESP_FILL_BUFFER)

        size_t written = 0;
        i2s_channel_write(tx_handle, block, AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS, &written, portMAX_DELAY);
        if(written != AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS) {
            fprintf(stderr,"i2s underrun: %d vs %d\n", written, AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS);
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


#elif (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2530)

#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico-audio/audio_i2s.h"
#include "pico/binary_info.h"


struct audio_buffer_pool *ap;

int32_t await_message_from_other_core() {
     while (!(sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)) {
         __wfe();
     }
     int32_t msg = sio_hw->fifo_rd;
     __sev();
     return msg;
 }

 // Send 32-bit message to other core
 void send_message_to_other_core(int32_t t) {
     while (!(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS)) {
         __wfe();
     }
     sio_hw->fifo_wr = t;
     __sev();
 }

static inline uint32_t _millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

void pi_pico_fill_audio_buffer() {
    
    amy_prepare_buffer();
    send_message_to_other_core(32);
    amy_render(0, AMY_OSCS/2, 0);
    await_message_from_other_core(64);
    int16_t *block = amy_fill_buffer();
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
    while(1) {
        int32_t ret = 0;
        while(ret!=32) ret = await_message_from_other_core();
        amy_render(AMY_OSCS/2, AMY_OSCS, 1);
        send_message_to_other_core(64);
    }

}

amy_err_t i2s_amy_init() {
    set_sys_clock_khz(250000000 / 1000, false); 
    multicore_launch_core1(core1_main);
    sleep_ms(500);
    ap = init_audio();
    while(1) {
        pi_pico_fill_audio_buffer();
    }
    return AMY_OK;
}




#else // teensy? m7? etc


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