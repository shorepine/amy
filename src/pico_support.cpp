// pico_support.cpp

#if (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2350)

// ------------- I2S -------------------
#include <AMY-Arduino.h>
#include <I2S.h>

// Create the I2S port using a PIO state machine
I2S i2s(INPUT_PULLUP);

extern "C" {
    void pico_setup_i2s(amy_config_t *config);
    void pico_teardown_i2s(amy_config_t *config);
    void pico_i2s_read_write_buffer(int16_t *in_samples, const int16_t *out_samples, int nframes);
}

// Setting of system clock/sampling rate.
// On RPi Pico, the system clock rate needs to be a multiple of 512.Fs so the PIO clock can generate 256.Fs MCLK without jitter.
// The sysclk also has to be divided down from a multiple of 12 MHz in the range 750..1600 MHz, and the divider is implemented by two div(1..7) dividers.
// As a result, we can't hit 44,100 exactly (but we can get within 0.2%), and the exact sys_clk and sample_rate vary.
// We predefine settings for each of the possible clock rates from Arduino menu - 50 100 120 125 128 133 150 176 200 225 240 250 276 300
#if AMY_SAMPLE_RATE != 44100
  #error "Pico clock setting logic is defined only for sample_rate == 44100."
#endif
#ifndef F_CPU
  #define F_CPU 276000000
#endif

#if F_CPU == 300000000 || F_CPU == 276000000
// works
  #define F_CPU_MOD_KHZ 271200
  #define F_SAMP 44140
  #define VCO_FREQ 1356000000
  #define PLL_PD1 5
  #define PLL_PD2 1
#elif F_CPU == 250000000 || F_CPU == 240000000
// works
  #define F_CPU_MOD_KHZ 248000
  #define F_SAMP 44034
  #define VCO_FREQ 1488000000
  #define PLL_PD1 6
  #define PLL_PD2 1
#elif F_CPU == 225000000
// works
  #define F_CPU_MOD_KHZ 226000
  #define F_SAMP 44140
  #define VCO_FREQ 1356000000
  #define PLL_PD1 6
  #define PLL_PD2 1
#elif F_CPU == 200000000
// works.  LRCK is 43.9 kHz
  #define F_CPU_MOD_KHZ 203143
  #define F_SAMP 44084
  #define VCO_FREQ 1422000000
  #define PLL_PD1 7
  #define PLL_PD2 1
#elif F_CPU == 176000000
// works.  LRCK is 43.9 kHz
  #define F_CPU_MOD_KHZ 180750
  #define F_SAMP 44128
  #define VCO_FREQ 1446000000
  #define PLL_PD1 4
  #define PLL_PD2 2
#elif F_CPU == 150000000
// works as long as CPU clock is modified *before* MIDI UART is initialized.  LRCK is 44.1 kHz.  MCLK is sync'd
// If MIDI UART is initialized *before* sys_set_clock_pll in pico_setup_i2s, baud rates are wrong and MIDI doesn't work.
  #define F_CPU_MOD_KHZ 158000
  #define F_SAMP 44084
  #define VCO_FREQ 948000000
  #define PLL_PD1 6
  #define PLL_PD2 1
#elif F_CPU == 133000000 || F_CPU == 128000000 || F_CPU == 125000000
// works
  #define F_CPU_MOD_KHZ 135428
  #define F_SAMP 44084
  #define VCO_FREQ 948000000
  #define PLL_PD1 7
  #define PLL_PD2 1
#elif F_CPU == 120000000 || F_CPU == 100000000
// works
  #define F_CPU_MOD_KHZ 112800
  #define F_SAMP 44062
  #define VCO_FREQ 1128000000
  #define PLL_PD1 5
  #define PLL_PD2 2
#elif F_CPU == 50000000
// works. rp2350 can only run 2 simultaneous juno patch 0s.  
  #define F_CPU_MOD_KHZ 45142
  #define F_SAMP 44084
  #define VCO_FREQ 948000000
  #define PLL_PD1 7
  #define PLL_PD2 3
#endif


void pico_setup_i2s(amy_config_t *config) {
    // Hardware limitation to pico
    assert(config->i2s_lrc == config->i2s_bclk + 1);
 
    i2s.setDOUT(config->i2s_dout);
    if (config->i2s_din != -1)
        i2s.setDIN(config->i2s_din);
    i2s.setBCLK(config->i2s_bclk);
    // mlck must be set before setFrequency() and i2s.begin()
    if (config->i2s_mclk != -1) {
        i2s.setMCLK(config->i2s_mclk);
        // MCLK_mult must be set before i2s.begin()
        i2s.setMCLKmult(config->i2s_mclk_mult);
    }
    i2s.setBitsPerSample(32);  // We always run with 32 bit I2S words.
    // Buffer size is in units of 32 bit words, our I2S samples are 32 bits.
    i2s.setBuffers(6, AMY_BLOCK_SIZE * AMY_NCHANS, 0);
    // Set the system clock to a compatible value to the samplerate.
    // Best to do this before starting anything clock-dependent.
    // The system clock rate must be an integer multiple of Fs * MCLKmult * 2, e.g. 22.58 MHz
    // but it also has to be something we can generate with the sysclk PLL i.e. a multiple of
    // 12 MHz in range 750..1600 MHz, which then divides down by two dividers of 1..7.
    //set_sys_clock_khz(F_CPU_MOD_KHZ, false);
    // VCO_FREQ, PLL_PD1, PLL_PD2 from e.g. python pico-sdk/src/rp2_common/hardware_clocks/scripts/vcocalc.py 135.428
    set_sys_clock_pll(VCO_FREQ, PLL_PD1, PLL_PD2); 
    // start I2S at the sample rate
    //i2s.setFrequency(AMY_SAMPLE_RATE);
    i2s.setFrequency(F_SAMP);  // Requested sample rate must be <= effective sample rate coming from sysclk because of int division (?).
    i2s.begin();
}

void pico_teardown_i2s(amy_config_t *config) {
    i2s.end();
}

static int32_t buffer32[AMY_BLOCK_SIZE * AMY_NCHANS];

void pico_i2s_read_write_buffer(int16_t *in_samples, const int16_t *out_samples, int nframes) {
    // write the same sample twice, once for left and once for the right channel
    assert(nframes <= AMY_BLOCK_SIZE);
    i2s.read((uint8_t *)buffer32, (size_t)(nframes * AMY_NCHANS * sizeof(int32_t)));
    for (int i = 0; i < nframes * AMY_NCHANS; ++i) {
        in_samples[i] = (int16_t)(buffer32[i] >> 16L);
    }
    for (int i = 0; i < nframes * AMY_NCHANS; ++i) {
        buffer32[i] = (((int32_t)out_samples[i]) << 16);
    }
    while (i2s.availableForWrite() == 0)
        ;  // block.
    i2s.write((const uint8_t *)buffer32, (size_t)(nframes * AMY_NCHANS * sizeof(int32_t)));
}

// ---- USB gadget ----

// USB MIDI gadget needs arduino-pico's Tools > USB Stack > "Adafruit TinyUSB",
// which defines USE_TINYUSB. With the default "Pico SDK" stack, the Pico is a
// CDC serial port only and AMY_MIDI_IS_USB_GADGET does nothing.
#ifdef USE_TINYUSB
#include <Adafruit_TinyUSB.h>
static Adafruit_USBD_MIDI usb_midi;
#endif

extern "C" {
    void convert_midi_bytes_to_messages(uint8_t * data, size_t len, uint8_t usb);

    void pico_setup_midi() {
#ifdef USE_TINYUSB
        // arduino-pico normally starts TinyUSB before setup(); begin() here
        // only in case it didn't.
        if (!TinyUSBDevice.isInitialized()) {
            TinyUSBDevice.begin(0);
        }
        usb_midi.setStringDescriptor("AMY Synthesizer");
        usb_midi.begin();
        // Interfaces added after enumeration only appear once the host
        // re-enumerates us (e.g. if the sketch already waited on Serial).
        if (TinyUSBDevice.mounted()) {
            TinyUSBDevice.detach();
            delay(10);
            TinyUSBDevice.attach();
        }
#endif
    }

    void pico_teardown_midi() {
        // There's no TinyUSBDevice.end(), so just leave it initialized.
    }

    void pico_process_midi() {
#ifdef USE_TINYUSB
#ifdef TINYUSB_NEED_POLLING_TASK
        // Manual call tud_task since it isn't called by Core's background
        TinyUSBDevice.task();
#endif
        while (tud_midi_available()) {
            uint8_t packet[4];
            tud_midi_packet_read(packet);
            convert_midi_bytes_to_messages(packet + 1, 3, 1);
        }
#endif
    }

    void pico_midi_out(uint8_t *bytes, uint16_t len) {
#ifdef USE_TINYUSB
        // Only when a host has enumerated us; tud_midi_stream_write returns 0
        // forever otherwise (see midi_out() in amy_midi.c).
        if (!tud_ready()) return;
        uint32_t sent = 0;
        int stall_ticks = 0;
        while (sent < len) {
            uint32_t n = tud_midi_stream_write(0, bytes + sent, len - sent);
            if (n == 0) {
#ifdef TINYUSB_NEED_POLLING_TASK
                TinyUSBDevice.task();
#endif
                delay(1);
                if (++stall_ticks > 1000) break;
            } else {
                stall_ticks = 0;
            }
            sent += n;
        }
#endif
    }
}
#endif
