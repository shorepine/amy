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
// default to the FNK0104 (below) and can be overridden with -D defines, or by
// defining them before including this header (AMY-Arduino.h pulls it in).

#ifndef AMY_ES8311_H
#define AMY_ES8311_H

#ifdef ESP_PLATFORM

#include <stdint.h>
#include "amy.h"   // amy_err_t, AMY_OK

// Codec control-bus wiring defaults (the Freenove FNK0104).  Used by
// esp32_setup_i2s() under -DAMY_CODEC_ES8311, and handy for sketches that call
// amy_es8311_init() themselves.
#ifndef AMY_ES8311_I2C_PORT
#define AMY_ES8311_I2C_PORT   0      // I2C_NUM_0
#endif
#ifndef AMY_ES8311_I2C_SDA
#define AMY_ES8311_I2C_SDA    16     // FNK0104 codec-control SDA
#endif
#ifndef AMY_ES8311_I2C_SCL
#define AMY_ES8311_I2C_SCL    15     // FNK0104 codec-control SCL
#endif
#ifndef AMY_ES8311_I2C_ADDR
#define AMY_ES8311_I2C_ADDR   0x18   // ES8311 with CE tied low
#endif
#ifndef AMY_ES8311_PA_GPIO
#define AMY_ES8311_PA_GPIO    1      // FNK0104 power-amplifier enable
#endif
#ifndef AMY_ES8311_PA_ACTIVE_LOW
#define AMY_ES8311_PA_ACTIVE_LOW 1   // FNK0104 amp enable is active-low
#endif
#ifndef AMY_ES8311_VOLUME
#define AMY_ES8311_VOLUME     100    // 0-100; 100 = loudest AMY can drive it
                                     // without clipping (see amy_es8311_init)
#endif

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
//   volume         : startup DAC volume, 0-100.  100 is as loud as AMY can drive
//                    the codec without clipping: AMY's output stage keeps
//                    ~6 dB of headroom on this platform (see
//                    AMY_OUTPUT_HEADROOM_BITS in amy.h), and 100 gives exactly
//                    that back as codec digital gain, so peaks land at full
//                    scale and never above it.  Below 100 the scale is linear in
//                    dB at ~0.5 dB per step, down to ~ -95 dB (silence) at 0 --
//                    so 50 is very quiet indeed, not "half volume".
// The ESP-side I2S channel must already be enabled (so MCLK/BCLK are running)
// before this is called.  Returns AMY_OK on success, -1 on failure.
// Note that the driver takes exclusive ownership of i2c_port for the duration of
// the call and hands the pins back afterwards, so anything else on that bus
// (Arduino Wire for a touch panel, say) must be initialised *after* this.
amy_err_t amy_es8311_init(int i2c_port, int8_t sda, int8_t scl, uint8_t i2c_addr,
                          int8_t pa_enable_gpio, uint8_t pa_active_low,
                          uint32_t sample_rate, uint32_t mclk_hz, uint8_t volume);

// True if the driver's clock-coefficient table has a row for this (MCLK,
// sample-rate) pair -- i.e. amy_es8311_init() with these values can succeed.
// The table is the single source of truth; i2s.c uses this to pick an MCLK
// multiple the codec can actually take.
int amy_es8311_supports_mclk(uint32_t mclk_hz, uint32_t sample_rate);

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
