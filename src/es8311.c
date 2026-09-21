// es8311.c
// Minimal ES8311 mono audio-codec driver for AMY on ESP32.
//
// Ported from Espressif's reference ES8311 driver (ESPRESSIF MIT License) down
// to just the playback path AMY needs, and re-plumbed onto the ESP-IDF
// driver_ng I2C master API (via amy_i2c.h) so it coexists with Arduino's Wire,
// exactly like the PCM9211 setup in i2s.c.
//
// Assumptions (matching AMY's default ESP I2S setup in i2s.c):
//   * The ES8311 is the I2S slave; AMY (the ESP) is the master and supplies
//     MCLK, BCLK and WS.
//   * The codec's internal MCLK comes from the MCLK pin (not BCLK).
//   * MCLK = sample_rate * amy_config.i2s_mclk_mult (256 by default).  The
//     caller passes the resulting frequency in, so what we program here can't
//     drift away from what the I2S peripheral actually generates.
//   * Data is 32-bit, MSB / left-justified (I2S_STD_MSB_SLOT_DEFAULT_CONFIG).

#if defined(ESP_PLATFORM)

#include <stdio.h>
#include "es8311.h"
#include "amy_i2c.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us

// ---- ES8311 register map (subset used here) ----
#define ES8311_RESET_REG00          0x00
#define ES8311_CLK_MANAGER_REG01    0x01
#define ES8311_CLK_MANAGER_REG02    0x02
#define ES8311_CLK_MANAGER_REG03    0x03
#define ES8311_CLK_MANAGER_REG04    0x04
#define ES8311_CLK_MANAGER_REG05    0x05
#define ES8311_CLK_MANAGER_REG06    0x06
#define ES8311_CLK_MANAGER_REG07    0x07
#define ES8311_CLK_MANAGER_REG08    0x08
#define ES8311_SDPIN_REG09          0x09   // DAC serial digital port
#define ES8311_SDPOUT_REG0A         0x0A   // ADC serial digital port
#define ES8311_SYSTEM_REG0B         0x0B
#define ES8311_SYSTEM_REG0C         0x0C
#define ES8311_SYSTEM_REG0D         0x0D
#define ES8311_SYSTEM_REG0E         0x0E
#define ES8311_SYSTEM_REG10         0x10
#define ES8311_SYSTEM_REG11         0x11
#define ES8311_SYSTEM_REG12         0x12
#define ES8311_SYSTEM_REG13         0x13
#define ES8311_SYSTEM_REG14         0x14
#define ES8311_ADC_REG15            0x15
#define ES8311_ADC_REG16            0x16
#define ES8311_ADC_REG17            0x17
#define ES8311_ADC_REG1B            0x1B
#define ES8311_ADC_REG1C            0x1C
#define ES8311_DAC_REG31            0x31   // DAC mute
#define ES8311_DAC_REG32            0x32   // DAC volume
#define ES8311_DAC_REG37            0x37
#define ES8311_GPIO_REG44           0x44   // GPIO / I2C noise immunity
#define ES8311_GP_REG45             0x45
#define ES8311_CHD1_REGFD           0xFD   // chip ID 1
#define ES8311_CHD2_REGFE           0xFE   // chip ID 2

#define ES8311_I2C_FREQ             400000

// reg32 (DAC volume) steps 0.5 dB: 0x00 = -95.5 dB, 0xBF = 0 dB, and anything
// above 0xBF is *digital gain* (up to +32 dB at 0xFF), which would clip a signal
// that already reaches full scale.
#define ES8311_DAC_VOL_0DB          0xBF
// AMY's output on this platform can't reach full scale: amy_fill_buffer's soft
// clipper bounds the sample to SAMPLE_MAX and then drops
// AMY_OUTPUT_HEADROOM_BITS bits (see amy.h), so the I2S stream is ceilinged
// ~6.02 dB below full scale per dropped bit no matter what's playing.  Giving
// exactly that back here (12 half-dB steps per bit) restores a full-scale analog
// output and still cannot clip -- and it tracks amy.h, so removing AMY's bit
// drop automatically drops this gain back to unity.
#define ES8311_DAC_VOL_MAX          (ES8311_DAC_VOL_0DB + 12 * AMY_OUTPUT_HEADROOM_BITS)

// Clock coefficient row: how to derive the codec's internal clocks from a given
// (MCLK, sample-rate) pair.  Copied from the Espressif reference driver.
struct coeff_div {
    uint32_t mclk;
    uint32_t rate;
    uint8_t pre_div;
    uint8_t pre_multi;
    uint8_t adc_div;
    uint8_t dac_div;
    uint8_t fs_mode;
    uint8_t lrck_h;
    uint8_t lrck_l;
    uint8_t bclk_div;
    uint8_t adc_osr;
    uint8_t dac_osr;
};

// AMY_SAMPLE_RATE is fixed at compile time, so only the rows for that one rate
// can ever be selected; the reference driver's other ~55 rows would be a
// kilobyte of dead flash on an MCU.  Every MCLK variant of our rate stays,
// because amy_config.i2s_mclk_mult picks which one we feed the codec.  (A build
// at a rate the table doesn't list compiles to an empty table, and get_coeff()
// reports the miss.)
#define ES8311_WANT(r) (AMY_SAMPLE_RATE == (r))

static const struct coeff_div coeff_div[] = {
    // mclk      rate   prediv  mult  adcdiv dacdiv fsmode lrch  lrcl  bckdiv adcosr dacosr
#if ES8311_WANT(8000)
    {12288000, 8000 , 0x06, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 8000 , 0x03, 0x02, 0x03, 0x03, 0x00, 0x05, 0xff, 0x18, 0x10, 0x10},
    {16384000, 8000 , 0x08, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000 , 8000 , 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 8000 , 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000 , 8000 , 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 8000 , 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000 , 8000 , 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 8000 , 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000 , 8000 , 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(11025)
    {11289600, 11025, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800 , 11025, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400 , 11025, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200 , 11025, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(12000)
    {12288000, 12000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 12000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 12000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 12000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(16000)
    {12288000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 16000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x10},
    {16384000, 16000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000 , 16000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 16000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000 , 16000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 16000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000 , 16000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 16000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000 , 16000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(22050)
    {11289600, 22050, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800 , 22050, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400 , 22050, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200 , 22050, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(24000)
    {12288000, 24000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 24000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 24000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 24000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 24000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(32000)
    {12288000, 32000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 32000, 0x03, 0x04, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x10},
    {16384000, 32000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000 , 32000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 32000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000 , 32000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 32000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000 , 32000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 32000, 0x03, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
    {1024000 , 32000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(44100)
    {11289600, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800 , 44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400 , 44100, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200 , 44100, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(48000)
    {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 48000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 48000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 48000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
#endif
#if ES8311_WANT(96000)
    {12288000, 96000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 96000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 96000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 96000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 96000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
#endif
};

static esp_err_t es_write(const amy_i2c_dev_t *d, uint8_t reg, uint8_t val) {
    esp_err_t ret = amy_i2c_write_reg(d, reg, val);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: write reg 0x%02x failed: %s\n", reg, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t es_read(const amy_i2c_dev_t *d, uint8_t reg, uint8_t *val) {
    esp_err_t ret = amy_i2c_read_reg(d, reg, val);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: read reg 0x%02x failed: %s\n", reg, esp_err_to_name(ret));
    }
    return ret;
}

static const struct coeff_div *get_coeff(uint32_t mclk, uint32_t rate) {
    for (unsigned i = 0; i < sizeof(coeff_div) / sizeof(coeff_div[0]); i++) {
        if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
            return &coeff_div[i];
    }
    return NULL;
}

int amy_es8311_supports_mclk(uint32_t mclk_hz, uint32_t sample_rate) {
    return get_coeff(mclk_hz, sample_rate) != NULL;
}

// Program the clock-manager registers for the given coefficient row.
// Codec is a slave, MCLK sourced from the MCLK pin (not inverted).
static esp_err_t es8311_config_clock(const amy_i2c_dev_t *d, const struct coeff_div *c) {
    uint8_t regv;
    esp_err_t ret;

    // Pre-divider / pre-multiplier (reg02). pre_multi 1/2/4/8 -> 0/1/2/3.
    uint8_t datmp = 0;
    switch (c->pre_multi) {
        case 1: datmp = 0; break;
        case 2: datmp = 1; break;
        case 4: datmp = 2; break;
        case 8: datmp = 3; break;
        default: break;
    }
    ret = es_read(d, ES8311_CLK_MANAGER_REG02, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0x07;
    regv |= (c->pre_div - 1) << 5;
    regv |= datmp << 3;
    ret = es_write(d, ES8311_CLK_MANAGER_REG02, regv);
    if (ret != ESP_OK) return ret;

    // ADC/DAC clock dividers (reg05).
    regv = (c->adc_div - 1) << 4;
    regv |= (c->dac_div - 1) << 0;
    ret = es_write(d, ES8311_CLK_MANAGER_REG05, regv);
    if (ret != ESP_OK) return ret;

    // ADC over-sample rate + fs mode (reg03).
    ret = es_read(d, ES8311_CLK_MANAGER_REG03, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0x80;
    regv |= c->fs_mode << 6;
    regv |= c->adc_osr << 0;
    ret = es_write(d, ES8311_CLK_MANAGER_REG03, regv);
    if (ret != ESP_OK) return ret;

    // DAC over-sample rate (reg04).
    ret = es_read(d, ES8311_CLK_MANAGER_REG04, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0x80;
    regv |= c->dac_osr << 0;
    ret = es_write(d, ES8311_CLK_MANAGER_REG04, regv);
    if (ret != ESP_OK) return ret;

    // LRCK divider high/low (reg07/reg08).
    ret = es_read(d, ES8311_CLK_MANAGER_REG07, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0xC0;
    regv |= c->lrck_h << 0;
    ret = es_write(d, ES8311_CLK_MANAGER_REG07, regv);
    if (ret != ESP_OK) return ret;
    regv = c->lrck_l;
    ret = es_write(d, ES8311_CLK_MANAGER_REG08, regv);
    if (ret != ESP_OK) return ret;

    // BCLK divider (reg06).
    ret = es_read(d, ES8311_CLK_MANAGER_REG06, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0xE0;
    if (c->bclk_div < 19)
        regv |= (c->bclk_div - 1) << 0;
    else
        regv |= (c->bclk_div) << 0;
    ret = es_write(d, ES8311_CLK_MANAGER_REG06, regv);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

// The bring-up proper.  Wrapped by amy_es8311_init below, which records the
// result so amy_es8311_status() can report it to a caller that did not do the
// bring-up itself.
static amy_err_t es8311_init_impl(int i2c_port, int8_t sda, int8_t scl, uint8_t i2c_addr,
                                  int8_t pa_enable_gpio, uint8_t pa_active_low,
                                  uint32_t sample_rate, uint32_t mclk_hz, uint8_t volume) {
    const struct coeff_div *coeff = get_coeff(mclk_hz, sample_rate);
    if (coeff == NULL) {
        fprintf(stderr, "ES8311: no clock coefficients for %luHz MCLK at %luHz\n",
                (unsigned long)mclk_hz, (unsigned long)sample_rate);
        return -1;
    }

    // Bring up the I2C master bus + codec device.
    amy_i2c_dev_t d;
    esp_err_t ret = amy_i2c_open_device(&d, i2c_port, sda, scl, i2c_addr, ES8311_I2C_FREQ);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: I2C open (port %d, sda %d, scl %d, addr 0x%02x) failed: %s\n"
                        "        If something else already owns that bus -- e.g. Wire.begin() for a\n"
                        "        touch panel sharing those pins -- configure the codec first.\n",
                i2c_port, sda, scl, i2c_addr, esp_err_to_name(ret));
        return -1;
    }

    // From this point on, any hardware-access failure tears down the temporary
    // I2C bus and returns an error without enabling the power amplifier.
#define ES8311_INIT_CHECK(call) do { \
        ret = (call); \
        if (ret != ESP_OK) goto init_failed; \
    } while (0)

    // The ES8311 can occasionally NACK its first I2C transaction.  Espressif's
    // reference sequence writes reg44 twice both to enable the chip's I2C noise
    // immunity and to make the first write a disposable warm-up.  Require the
    // second write to succeed before treating subsequent accesses as reliable.
    (void)amy_i2c_write_reg(&d, ES8311_GPIO_REG44, 0x08);
    ES8311_INIT_CHECK(es_write(&d, ES8311_GPIO_REG44, 0x08));

    // Presence check (ES8311 chip id is 0x83 0x11).
    uint8_t id1, id2, regv;
    ES8311_INIT_CHECK(es_read(&d, ES8311_CHD1_REGFD, &id1));
    ES8311_INIT_CHECK(es_read(&d, ES8311_CHD2_REGFE, &id2));
    if (!(id1 == 0x83 && id2 == 0x11)) {
        fprintf(stderr, "ES8311: unexpected chip id 0x%02x%02x (expected 0x8311)\n", id1, id2);
        ret = ESP_FAIL;
        goto init_failed;
    }

    // ---- Codec init (clock manager + power) ----
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG01, 0x30));
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG02, 0x00));
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG03, 0x10));
    ES8311_INIT_CHECK(es_write(&d, ES8311_ADC_REG16, 0x24));
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG04, 0x10));
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG05, 0x00));
    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG0B, 0x00));
    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG0C, 0x00));
    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG10, 0x1F));
    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG11, 0x7F));
    ES8311_INIT_CHECK(es_write(&d, ES8311_RESET_REG00, 0x80));   // power up, CSM/clock on

    // Slave mode: clear bit6 of reg00.
    ES8311_INIT_CHECK(es_read(&d, ES8311_RESET_REG00, &regv));
    ES8311_INIT_CHECK(es_write(&d, ES8311_RESET_REG00, regv & 0xBF));
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG01, 0x3F));
    // MCLK from the MCLK pin (not BCLK): clear bit7 of reg01.
    ES8311_INIT_CHECK(es_read(&d, ES8311_CLK_MANAGER_REG01, &regv));
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG01, regv & 0x7F));

    ES8311_INIT_CHECK(es8311_config_clock(&d, coeff));

    // MCLK / SCLK not inverted.
    ES8311_INIT_CHECK(es_read(&d, ES8311_CLK_MANAGER_REG01, &regv));
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG01, regv & ~0x40));
    ES8311_INIT_CHECK(es_read(&d, ES8311_CLK_MANAGER_REG06, &regv));
    ES8311_INIT_CHECK(es_write(&d, ES8311_CLK_MANAGER_REG06, regv & ~0x20));

    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG13, 0x10));
    ES8311_INIT_CHECK(es_write(&d, ES8311_ADC_REG1B, 0x0A));
    ES8311_INIT_CHECK(es_write(&d, ES8311_ADC_REG1C, 0x6A));

    // ---- Serial data-port format ----
    // reg09/reg0A: serial-port mute in bit6 (so 0 here also un-mutes the port),
    // word length in bits [4:2], format in bits [1:0].  0x11 = 32-bit words,
    // MSB / left-justified, matching AMY's I2S_STD_MSB_SLOT_DEFAULT_CONFIG.
    ES8311_INIT_CHECK(es_write(&d, ES8311_SDPIN_REG09, 0x11));
    ES8311_INIT_CHECK(es_write(&d, ES8311_SDPOUT_REG0A, 0x11));

    // ---- Start playback (DAC) path ----
    ES8311_INIT_CHECK(es_write(&d, ES8311_ADC_REG17, 0xBF));
    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG0E, 0x02));
    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG12, 0x00));   // enable DAC
    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG14, 0x1A));   // analog PGA, not DMIC (bit6 = 0)
    ES8311_INIT_CHECK(es_write(&d, ES8311_SYSTEM_REG0D, 0x01));
    ES8311_INIT_CHECK(es_write(&d, ES8311_ADC_REG15, 0x40));
    ES8311_INIT_CHECK(es_write(&d, ES8311_DAC_REG37, 0x48));
    ES8311_INIT_CHECK(es_write(&d, ES8311_GP_REG45, 0x00));

    // ---- Volume + un-mute ----
    if (volume > 100) volume = 100;
    ES8311_INIT_CHECK(es_write(&d, ES8311_DAC_REG32,
                               (uint8_t)(((uint32_t)volume * ES8311_DAC_VOL_MAX + 50) / 100)));
    // Un-mute: clear the DAC mute bits (reg31 [6:5]).
    ES8311_INIT_CHECK(es_read(&d, ES8311_DAC_REG31, &regv));
    ES8311_INIT_CHECK(es_write(&d, ES8311_DAC_REG31, regv & 0x9F));

    // Give the DAC ramp a moment before the amp comes up (avoids a turn-on pop).
    esp_rom_delay_us(2000);

    // ---- Enable the on-board power amplifier ----
    // Many of these boards (e.g. the FNK0104) gate the amp with an active-low
    // enable, so driving it HIGH holds the amp in shutdown -> silence.  Drive it
    // to the level that turns the amp ON.
    if (pa_enable_gpio >= 0) {
        ES8311_INIT_CHECK(gpio_set_direction((gpio_num_t)pa_enable_gpio, GPIO_MODE_OUTPUT));
        ES8311_INIT_CHECK(gpio_set_level((gpio_num_t)pa_enable_gpio, pa_active_low ? 0 : 1));
    }

    // The codec now runs off I2S; release the I2C bus so Arduino Wire (touch
    // panel, etc.) can reuse it, mirroring the PCM9211 setup in i2s.c.
    amy_i2c_close_device(&d);

#undef ES8311_INIT_CHECK
    return AMY_OK;

init_failed:
    amy_i2c_close_device(&d);
    return -1;
}

static amy_err_t es8311_last_status = AMY_ES8311_NOT_STARTED;

// What we last configured.  amy_es8311_init() hands the I2C pins back when it's
// done, so shutting the codec down again means re-opening the control bus.
static struct {
    int i2c_port;
    int8_t sda;
    int8_t scl;
    uint8_t i2c_addr;
    int8_t pa_gpio;
    uint8_t pa_active_low;
} es8311_hw;

amy_err_t amy_es8311_init(int i2c_port, int8_t sda, int8_t scl, uint8_t i2c_addr,
                          int8_t pa_enable_gpio, uint8_t pa_active_low,
                          uint32_t sample_rate, uint32_t mclk_hz, uint8_t volume) {
    // If a codec is already up (a second esp32_setup_i2s(), a stop/start cycle),
    // mute it and drop the amp first so we re-configure a quiet chip instead of
    // reprogramming clocks underneath a live amplifier.
    if (es8311_last_status == AMY_OK) amy_es8311_deinit();

    es8311_last_status = es8311_init_impl(i2c_port, sda, scl, i2c_addr,
                                         pa_enable_gpio, pa_active_low,
                                         sample_rate, mclk_hz, volume);
    if (es8311_last_status == AMY_OK) {
        es8311_hw.i2c_port = i2c_port;
        es8311_hw.sda = sda;
        es8311_hw.scl = scl;
        es8311_hw.i2c_addr = i2c_addr;
        es8311_hw.pa_gpio = pa_enable_gpio;
        es8311_hw.pa_active_low = pa_active_low;
    }
    return es8311_last_status;
}

amy_err_t amy_es8311_deinit(void) {
    if (es8311_last_status != AMY_OK) return AMY_OK;   // nothing of ours is running
    es8311_last_status = AMY_ES8311_NOT_STARTED;

    // Shut the power amplifier down *first*: muting the DAC -- or, worse, having
    // the caller stop MCLK/BCLK/WS -- while the amp is live puts a pop, or a
    // sustained DC thump, through the speaker.
    if (es8311_hw.pa_gpio >= 0) {
        gpio_set_level((gpio_num_t)es8311_hw.pa_gpio, es8311_hw.pa_active_low ? 1 : 0);
        esp_rom_delay_us(2000);   // let the amp actually get there
    }

    // Then quiet the codec, so a stopped I2S clock (or the next init) meets a
    // muted, powered-down DAC.  Re-open the control bus we handed back in init.
    amy_i2c_dev_t d;
    esp_err_t ret = amy_i2c_open_device(&d, es8311_hw.i2c_port, es8311_hw.sda, es8311_hw.scl,
                                       es8311_hw.i2c_addr, ES8311_I2C_FREQ);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: I2C open for shutdown failed: %s "
                        "(amp is off, DAC left as it was)\n", esp_err_to_name(ret));
        return -1;
    }
    uint8_t regv;
    es_write(&d, ES8311_DAC_REG32, 0x00);              // volume down to -95.5 dB
    if (es_read(&d, ES8311_DAC_REG31, &regv) == ESP_OK)
        es_write(&d, ES8311_DAC_REG31, regv | 0x60);   // mute the DAC
    es_write(&d, ES8311_SYSTEM_REG12, 0x02);           // DAC off
    es_write(&d, ES8311_SYSTEM_REG0D, 0xFA);           // power down the analog blocks
    amy_i2c_close_device(&d);
    return AMY_OK;
}

amy_err_t amy_es8311_status(void) { return es8311_last_status; }

#endif // ESP_PLATFORM
