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

#define SINE_WAVE_TABLE_LEN 2048
#define SAMPLES_PER_BUFFER 256

static int16_t sine_wave_table[SINE_WAVE_TABLE_LEN];

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
    for (uint i = 0; i < BLOCK_SIZE; i++) {
        samples[i] = block[i]; // (vol * sine_wave_table[pos >> 16u]) >> 8u;
    }
    buffer->sample_count = BLOCK_SIZE;
    give_audio_buffer(ap, buffer);
}

struct audio_buffer_pool *init_audio() {

    static audio_format_t audio_format = {
            .format = AUDIO_BUFFER_FORMAT_PCM_S16,
            .sample_freq = SAMPLE_RATE,
            .channel_count = 1,
    };

    static struct audio_buffer_format producer_format = {
            .format = &audio_format,
            .sample_stride = 2
    };

    struct audio_buffer_pool *producer_pool = audio_new_producer_pool(&producer_format, 3, BLOCK_SIZE); 
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

    printf("Clock is set to %d\n", clock_get_hz(clk_sys));
    set_sys_clock_khz(250000000 / 1000, false); //

    amy_start();
    /*
    for (int i = 0; i < SINE_WAVE_TABLE_LEN; i++) {
        sine_wave_table[i] = 32767 * cosf(i * 2 * (float) (M_PI / SINE_WAVE_TABLE_LEN));
    }
    */
    struct audio_buffer_pool *ap = init_audio();


    // Play a few notes in FM
    struct event e = amy_default_event();
    int64_t start = amy_sysclock();
    e.time = start;
    e.velocity = 0.25;
    e.wave = SINE;
    e.patch = 15;
    e.midi_note = 70;
    amy_add_event(e);

    e.time = start + 500;
    e.osc += 9; // remember that an FM patch takes up 9 oscillators
    e.midi_note = 64;
    amy_add_event(e);

    e.time = start + 1000;
    e.osc += 9;
    e.midi_note = 68;
    amy_add_event(e);

    e.time = start + 1500;
    e.osc += 9;
    e.midi_note = 72;
    amy_add_event(e);

    e.time = start + 2000;
    e.osc += 9;
    e.midi_note = 58;
    amy_add_event(e);

/*
    uint32_t step = 0x200000;
    uint32_t pos = 0;
    uint32_t pos_max = 0x10000 * SINE_WAVE_TABLE_LEN;
    uint vol = 16;
*/
    while (true) {
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

    return 0;
}

