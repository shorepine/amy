// es8311.h
// Minimal ES8311 mono audio-codec driver for AMY on ESP32 (Arduino / ESP-IDF).
//
// The ES8311 is an I2S audio codec that must be configured over I2C before it
// will pass any audio.  It's found on, e.g., the Freenove FNK0104 ESP32-S3
// display boards, which pair it with a Class-D power amplifier driving a small
// speaker.  AMY drives the codec as an I2S *master* (the ES8311 is the I2S
// slave) and feeds it MCLK from the ESP's I2S MCLK pin.  Only the playback
// (DAC) path is implemented here.
//
// To use it, build AMY with -DAMY_CODEC_ES8311 and set the I2S pins in your
// amy_config before amy_start().  On the FNK0104 that is:
//     amy_config.i2s_mclk = 4;
//     amy_config.i2s_bclk = 5;
//     amy_config.i2s_lrc  = 7;   // WS
//     amy_config.i2s_dout = 8;
// The I2C control pins, codec address, power-amp enable GPIO and startup volume
// default to the FNK0104 and can be overridden with -D defines (see i2s.c).

#ifndef AMY_ES8311_H
#define AMY_ES8311_H

#ifdef ESP_PLATFORM

#include <stdint.h>
#include "amy.h"   // amy_err_t, AMY_OK

// Configure and power up an ES8311 for I2S playback.
//   i2c_port       : I2C_NUM_0 / I2C_NUM_1
//   sda, scl       : I2C GPIOs wired to the codec's control port
//   i2c_addr       : 7-bit codec address (0x18 with CE low, 0x19 with CE high)
//   pa_enable_gpio : GPIO that enables the on-board power amplifier (-1 if none)
//   pa_active_low  : 1 if the amp-enable pin is active-low (driven LOW to turn the
//                    amp ON, e.g. the Freenove FNK0104), 0 if active-high
//   sample_rate    : audio sample rate in Hz (normally AMY_SAMPLE_RATE)
//   mclk_hz        : MCLK frequency AMY feeds the codec (sample_rate * mclk mult)
//   volume         : startup DAC volume, 0-100
// The ESP-side I2S channel must already be enabled (so MCLK/BCLK are running)
// before this is called.  Returns AMY_OK on success, -1 on failure.
amy_err_t amy_es8311_init(int i2c_port, int8_t sda, int8_t scl, uint8_t i2c_addr,
                          int8_t pa_enable_gpio, uint8_t pa_active_low,
                          uint32_t sample_rate, uint32_t mclk_hz, uint8_t volume);

#endif // ESP_PLATFORM
#endif // AMY_ES8311_H
