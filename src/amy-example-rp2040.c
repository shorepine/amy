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
#include "pico/multicore.h"

#endif

#include "pico/stdlib.h"
#include "pico/audio_i2s.h"


#if PICO_ON_DEVICE
#include "pico/binary_info.h"
bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCK"));
#endif

#define CPU0_METER 2
#define CPU1_METER 3

extern int32_t await_message_from_other_core();
extern void send_message_to_other_core(int32_t t);

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

// Emulate the Tulip "drums()" example via event calls.
void drums(int64_t start, int loops) {
    //int64_t start = amy_sysclock();
    struct event e = amy_default_event();
    e.time = start;

    float volume = 0.2;

    int oscs[] = {0, 2, 3, 4, 5, 6};
    int patches[] = {1, 5, 0, 10, 10, 5};
    e.wave = PCM;
    e.freq = 0;
    e.velocity = 0;
    for (int i = 0; i < sizeof(oscs) / sizeof(int); ++i) {
        e.osc = oscs[i];
        e.patch = patches[i];
        amy_add_event(e);
    }
    // Update high cowbell.
    e = amy_default_event();
    e.time = start;
    e.osc = 5;
    e.midi_note = 70;
    amy_add_event(e);

    // osc 7 : bass
    e = amy_default_event();
    e.time = start;
    e.osc = 7;
    e.wave = SAW_DOWN;
    e.filter_freq = 2500.0;
    e.resonance = 5.0;
    e.filter_type = FILTER_LPF;
    e.breakpoint_target[0] = TARGET_AMP + TARGET_FILTER_FREQ;
    amy_add_event(e);

    char bp0msg[] = "v0A0,1,500,0.2,25,0\0";
    bp0msg[1] = '0' + e.osc;
    amy_play_message(bp0msg);

    const int bass = 1 << 0;
    const int snare = 1 << 1;
    const int hat = 1 << 2;
    const int cow = 1 << 3;
    const int hicow = 1 << 4;

    int pattern[] = {bass+hat, hat+hicow, bass+hat+snare, hat+cow, hat, hat+bass, snare+hat, hat};
    int bassline[] = {50, 0, 0, 0, 50, 52, 51, 0};

    e = amy_default_event();
    e.time = start;
    while (loops--) {
        for (int i = 0; i < sizeof(pattern) / sizeof(int); ++i) {
            e.time += 250;

            int x = pattern[i];
            if(x & bass) {
                e.osc = 0;
                e.velocity = 4.0 * volume;
                amy_add_event(e);
            }
            if(x & snare) {
                e.osc = 2;
                e.velocity = 1.5 * volume;
                amy_add_event(e);
            }
            if(x & hat) {
                e.osc = 3;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }
            if(x & cow) {
                e.osc = 4;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }
            if(x & hicow) {
                e.osc = 5;
                e.velocity = 1 * volume;
                amy_add_event(e);
            }

            e.osc = 7;
            if(bassline[i]>0) {
                e.velocity = 0.5 * volume;
                e.midi_note = bassline[i] - 12;
            } else {
                e.velocity = 0;
            }
            amy_add_event(e);
            e.midi_note = -1;
        }
    }
}


void core1_main() {
    while(1) {
        int32_t ret = 0;
        while(ret!=64) ret = await_message_from_other_core();
        gpio_put(CPU1_METER, 1);
        render_task(AMY_OSCS/2, AMY_OSCS, 1);
        gpio_put(CPU1_METER, 0);
        send_message_to_other_core(32);
    }

}

const uint LED_PIN = PICO_DEFAULT_LED_PIN;

int main() {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);

    set_sys_clock_khz(250000000 / 1000, false); 
    stdio_init_all();
    getchar();
    #if AMY_CORES == 2
        multicore_launch_core1(core1_main);
    #endif

    gpio_put(LED_PIN, 0);

    sleep_ms(500);
    printf("Clock is set to %d\n", clock_get_hz(clk_sys));

    amy_start();

    gpio_init(CPU0_METER);
    gpio_set_dir(CPU0_METER, GPIO_OUT);
    gpio_put(CPU0_METER, 0);

    gpio_init(CPU1_METER);
    gpio_set_dir(CPU1_METER, GPIO_OUT);
    gpio_put(CPU1_METER, 0);

    {
        gpio_put(LED_PIN, 1);
        printf("Clock is set to %d\n", clock_get_hz(clk_sys));
        printf("LED ON !\n");
        sleep_ms(250);

        gpio_put(LED_PIN, 0);
        printf("LED OFF !\n");
        sleep_ms(250);
    }

#if AMY_HAS_REVERB == 1
    config_reverb(0, REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING, REVERB_DEFAULT_XOVER_HZ);
#endif

#if AMY_HAS_CHORUS == 1
    config_chorus(0.8, CHORUS_DEFAULT_MAX_DELAY);
#endif

    struct audio_buffer_pool *ap = init_audio();

    // Play a few notes in FM
    struct event e = amy_default_event();
    int64_t start = amy_sysclock();
    int osc_inc;
    e.time = start;
    e.osc = 0;
    e.wave = ALGO;
    e.patch = 20;
    osc_inc = 9;
    int notes[] = {60, 70, 64, 68, 72, 82, 76, 80, 74, 78, 80, 58};
    e.velocity = 0.2;

    if (false) {
        for (int i = 0; i < sizeof(notes) / sizeof(int); ++i) {
            e.midi_note = notes[i];
            e.pan = 0.5 + 0.5 * ((2 * (i %2)) - 1);
            e.patch++;
            amy_add_event(e);
            e.osc += osc_inc;
            e.time += 1000;
        }
    }
 
    drums(e.time + 500, 10 /* loops */);

    for (int i = 0; i < 5000; ++i) {
        rp2040_fill_audio_buffer(ap);
        if (i == 1000) {
            config_reverb(0.7, REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING, REVERB_DEFAULT_XOVER_HZ);
        }

    }

    while(true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(250);

        gpio_put(LED_PIN, 0);
        sleep_ms(250);
    }
    return 0;
}

