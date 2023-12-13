// AMY-Arduino.cpp
// connector for Arduino

#include "Arduino.h"
#include "AMY-Arduino.h"

#ifdef ARDUINO_ARCH_RP2040

bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCK"));

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
#endif

#ifdef ESP_PLATFORM


#endif



// TODO -- see how other synth libraries deal with different audio output & pins, I2S, DAC, analog, etc
AMY::AMY()
{

}

int64_t AMY::sysclock() {
    return amy_sysclock();
}

void AMY::begin()
{
    set_sys_clock_khz(250000000 / 1000, false); 
    amy_start();
    ap = init_audio();

}

void AMY::fm(int64_t start) {
    example_multimbral_fm(start);
}

void AMY::send_message(char * message) {

}

void AMY::render(uint8_t core) {
    int32_t ret = 0;
    if(core == 0) {
        rp2040_fill_audio_buffer(ap);
    } else {
        while(ret!=64) ret = await_message_from_other_core();
        render_task(AMY_OSCS/2, AMY_OSCS, 1);
        send_message_to_other_core(32);
    }
}





