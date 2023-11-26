/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>
#include "amy.h"
#if PICO_ON_DEVICE

#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"

#endif

#include "pico/stdlib.h"
#include "pico/audio_i2s.h"
/*
#define AMY_I2S_DATA_PIN 8
#define AMY_I2S_BCK_PIN 9
#define AMY_I2S_LRC_PIN 10
*/

#if PICO_ON_DEVICE
#include "pico/binary_info.h"
bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCK"));
#endif

//#define SINE_WAVE_TABLE_LEN 2048
//#define SAMPLES_PER_BUFFER 256

//static int16_t sine_wave_table[SINE_WAVE_TABLE_LEN];

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
            .data_pin = PICO_AUDIO_I2S_DATA_PIN,
            .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
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

int main() {

    stdio_init_all();

    set_sys_clock_khz(250000000 / 1000, false); //

    sleep_ms(500);

    printf("Clock is set to %d\n", clock_get_hz(clk_sys));

    amy_start();

    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    {
        gpio_put(LED_PIN, 1);
        printf("Clock is set to %d\n", clock_get_hz(clk_sys));
        printf("LED ON !\n");
        sleep_ms(250);

        gpio_put(LED_PIN, 0);
        printf("LED OFF !\n");
        sleep_ms(250);
    }

    /*
    for (int i = 0; i < SINE_WAVE_TABLE_LEN; i++) {
        sine_wave_table[i] = 32767 * cosf(i * 2 * (float) (M_PI / SINE_WAVE_TABLE_LEN));
    }
    */
    struct audio_buffer_pool *ap = init_audio();

    // Play a few notes in FM
    struct event e = amy_default_event();
    int64_t start = amy_sysclock();
    int osc_inc;
    e.time = start;
    e.osc = 0;

    e.wave = ALGO;
    e.patch = 17;
    osc_inc = 9;
    
    //e.wave = SAW_DOWN;
    //e.filter_freq = 2500.0;
    //e.resonance = 5.0;
    //e.filter_type = FILTER_LPF;
    //e.breakpoint_target[0] = TARGET_FILTER_FREQ;
    //osc_inc = 1;
    
    //e.breakpoint_target[0] = TARGET_AMP;

    //int notes[] = {46, 40, 44, 48, 58, 52, 56, 60, 70, 64, 68, 72, 82, 76, 80, 84, 94, 88, 92, 96, 106, 100, 104, 108, 118, 112, 116, 120};
    int notes[] = {60, 70, 64, 68, 72, 82, 76};
    //int notes[] = {72};

    //e = amy_default_event();
    e.velocity = 0.2;

    // amy.send(osc=0, bp0="1000,0.2,200,0")
    char bp0msg[] = "v0A0,1,1000,0.01,200,0\0";
    for (int i = 0; i < sizeof(notes) / sizeof(int); ++i) {
        // Don't setup the EG when using FM voices.
        if (osc_inc == 1 && e.osc < 10) {
            bp0msg[1] = '0' + e.osc;
            amy_play_message(bp0msg);
        }
        e.midi_note = notes[i];
        e.pan = 0.5 + 0.5 * ((2 * (i %2)) - 1);
        amy_add_event(e);
        e.osc += osc_inc;
        e.time += 500;
    }

/*
    uint32_t step = 0x200000;
    uint32_t pos = 0;
    uint32_t pos_max = 0x10000 * SINE_WAVE_TABLE_LEN;
    uint vol = 16;
*/
    for (int i = 0; i < 2000; ++i) {
        rp2040_fill_audio_buffer(ap);
    }

/*
        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == '-' && vol) vol -= 4;
            if ((c == '=' || c == '+') && vol < 255) vol += 4;
            if (c == '[' && step > 0x10000) step -= 0x10000;
            if (c == ']' && step < (SINE_WAVE_TABLE_LEN / 16) * 0x20000) step += 0x10000;
            if (c == 'q') break;
            printf("vol = %d, step = %d      \r", vol, step >> 16);
        }
        struct audio_buffer *buffer = take_audio_buffer(ap, true);
        int16_t *samples = (int16_t *) buffer->buffer->bytes;
        for (uint i = 0; i < buffer->max_sample_count; i++) {
            samples[i] = (vol * sine_wave_table[pos >> 16u]) >> 8u;
            pos += step;
            if (pos >= pos_max) pos -= pos_max;
        }
        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(ap, buffer);
    }
    puts("\n");
    */

    while(true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(250);

        gpio_put(LED_PIN, 0);
        sleep_ms(250);
    }
    return 0;
}

