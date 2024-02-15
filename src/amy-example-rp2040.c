#if PICO_ON_DEVICE
#ifndef ARDUINO

/*
gh repo clone raspberrypi/pico-extras
gh repo clone raspberrypi/pico-sdk
# Do whatever installs you need for the pico-sdk
gh repo clone bwhitman/amy
cd amy/src; mkdir build; cd build
export PICO_SDK_PATH=../../../pico-sdk
export PICO_EXTRAS_PATH=../../../pico-extras
cmake ..
make && ~/outside/picotool/build/picotool load -F amy_example.elf &&  ~/outside/picotool/build/picotool reboot
*/


#include <stdio.h>
#include <math.h>

#include "amy.h"
#include "examples.h"

#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "pico/multicore.h"


#include "pico/stdlib.h"
#include "pico/audio_i2s.h"


#include "pico/binary_info.h"
bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCK"));

#define CPU0_METER 2
#define CPU1_METER 3

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




void rp2040_fill_audio_buffer() {
    
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


void core1_main() {
    while(1) {
        int32_t ret = 0;
        while(ret!=32) ret = await_message_from_other_core();
        amy_render(AMY_OSCS/2, AMY_OSCS, 1);
        send_message_to_other_core(64);
    }

}

// delay and feed audio buffer while you wait
void delay_ms(uint32_t ms) {
    uint32_t start = amy_sysclock();
    while(amy_sysclock() - start < ms) {
        rp2040_fill_audio_buffer();
    }
}


int main() {
    set_sys_clock_khz(250000000 / 1000, false); 
    stdio_init_all();
    multicore_launch_core1(core1_main);
    

    sleep_ms(500);
    printf("Clock is set to %d\n", clock_get_hz(clk_sys));

    amy_start(/* cores= */ 2, /* reverb= */ 0, /* chorus= */ 1);

    gpio_init(CPU0_METER);
    gpio_set_dir(CPU0_METER, GPIO_OUT);
    gpio_put(CPU0_METER, 0);

    gpio_init(CPU1_METER);
    gpio_set_dir(CPU1_METER, GPIO_OUT);
    gpio_put(CPU1_METER, 0);

    printf("Clock is set to %d\n", clock_get_hz(clk_sys));
    sleep_ms(250);

    example_reverb();
    example_chorus();

    ap = init_audio();

    uint8_t dx7 = 0;
    uint8_t multimbral = 0;
    uint8_t stop = 0;


    example_voice_chord(0); // juno patch 0
    while(1) {
        delay_ms(100);
        if(amy_sysclock() > 4000 && !dx7) {
            example_voice_chord(130); // dx7 patch 2
            dx7 = 1;
        }
        if(amy_sysclock() > 8000 && !multimbral) {
            example_multimbral_fm();
            multimbral = 1;
        }
        if(amy_sysclock() > 15000 && !stop) {
            amy_reset_oscs();
            stop = 1;
        }
    }
    return 0;
}

#endif
#endif
