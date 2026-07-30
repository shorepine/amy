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
// To use it, either call amy_es8311_init() yourself right after amy_start() (see
// the AMY_ES8311_FNK0104 example) or build AMY with -DAMY_CODEC_ES8311, which
// does it from inside amy_start().  Either way, set the I2S pins in your
// amy_config first.  On the FNK0104 that is:
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
//   mclk_hz        : MCLK frequency AMY feeds the codec, i.e.
//                    sample_rate * amy_config.i2s_mclk_mult (256 by default).
//                    Must match what I2S actually generates or the codec's
//                    dividers are programmed for a clock it isn't getting.
//   volume         : startup DAC volume, 0-100, where 100 is unity (0 dB) and
//                    each step down attenuates ~0.5 dB (0 is ~ -95 dB, silence).
//                    The codec can apply digital gain above unity, but AMY's
//                    output already reaches full scale, so we don't offer it:
//                    it would just clip inside the DAC.
// The ESP-side I2S channel must already be enabled (so MCLK/BCLK are running)
// before this is called.  Returns AMY_OK on success, -1 on failure.
// Note that the driver takes exclusive ownership of i2c_port for the duration of
// the call and hands the pins back afterwards, so anything else on that bus
// (Arduino Wire for a touch panel, say) must be initialised *after* this.
amy_err_t amy_es8311_init(int i2c_port, int8_t sda, int8_t scl, uint8_t i2c_addr,
                          int8_t pa_enable_gpio, uint8_t pa_active_low,
                          uint32_t sample_rate, uint32_t mclk_hz, uint8_t volume);

// Mute the DAC and switch the power amplifier back off, using the pins/address
// the last successful amy_es8311_init() was given.  Call this *before* stopping
// I2S: pulling MCLK/BCLK/WS out from under a live amplifier thumps the speaker.
// A no-op (returning AMY_OK) if no codec was brought up.  AMY does this itself
// from esp32_teardown_i2s() under -DAMY_CODEC_ES8311.
amy_err_t amy_es8311_deinit(void);

// Result of the last amy_es8311_init() call, for callers that did not make it
// themselves: under -DAMY_CODEC_ES8311 the bring-up happens inside
// esp32_setup_i2s(), which has no way to hand the codec's status back up through
// amy_start().  Without this, a codec that failed to configure is just a silent
// board with no clue why.  AMY_OK / -1 as amy_es8311_init, or
// AMY_ES8311_NOT_STARTED if no codec is currently up (never initialised, or shut
// down again by amy_es8311_deinit()).
#define AMY_ES8311_NOT_STARTED (-2)
amy_err_t amy_es8311_status(void);

#endif // ESP_PLATFORM
#endif // AMY_ES8311_H
