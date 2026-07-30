// es8311.c
// Minimal ES8311 mono audio-codec driver for AMY on ESP32.
//
// Ported from Espressif's reference ES8311 driver (ESPRESSIF MIT License) down
// to just the playback path AMY needs, and re-plumbed onto the ESP-IDF
// driver_ng I2C master API (driver/i2c_master.h) so it coexists with Arduino's
// Wire, exactly like the PCM9211 setup in i2s.c.
//
// Assumptions (matching AMY's default ESP I2S setup in i2s.c):
//   * The ES8311 is the I2S slave; AMY (the ESP) is the master and supplies
//     MCLK, BCLK and WS.
//   * The codec's internal MCLK comes from the MCLK pin (not BCLK).
//   * MCLK = sample_rate * 256 (AMY's default I2S_STD_CLK_DEFAULT_CONFIG).
//   * Data is 32-bit, MSB / left-justified (I2S_STD_MSB_SLOT_DEFAULT_CONFIG).
//     Define AMY_ES8311_I2S_PHILIPS to use standard (Philips) I2S framing
//     instead, should AMY's slot config ever change.

#if defined(ESP_PLATFORM)

#include <stdio.h>
#include "es8311.h"
#include "driver/i2c_master.h"
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
#define ES8311_GP_REG45             0x45
#define ES8311_CHD1_REGFD           0xFD   // chip ID 1
#define ES8311_CHD2_REGFE           0xFE   // chip ID 2

// Clock coefficient row: how to derive the codec's internal clocks from a given
// (MCLK, sample-rate) pair.  Copied verbatim from the Espressif reference driver.
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

static const struct coeff_div coeff_div[] = {
    // mclk      rate   prediv  mult  adcdiv dacdiv fsmode lrch  lrcl  bckdiv adcosr dacosr
    /* 8k */
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
    /* 11.025k */
    {11289600, 11025, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800 , 11025, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400 , 11025, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200 , 11025, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    /* 12k */
    {12288000, 12000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 12000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 12000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 12000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    /* 16k */
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
    /* 22.05k */
    {11289600, 22050, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800 , 22050, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400 , 22050, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200 , 22050, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    /* 24k */
    {12288000, 24000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 24000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 24000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 24000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 24000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    /* 32k */
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
    /* 44.1k */
    {11289600, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800 , 44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400 , 44100, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200 , 44100, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    /* 48k */
    {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 48000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 48000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 48000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    /* 96k */
    {12288000, 96000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 96000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000 , 96000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000 , 96000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000 , 96000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
};

static i2c_master_dev_handle_t es_dev = NULL;

static esp_err_t es_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    esp_err_t ret = i2c_master_transmit(es_dev, buf, 2, 100);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: write reg 0x%02x failed: %s\n", reg, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t es_read(uint8_t reg, uint8_t *val) {
    *val = 0;
    esp_err_t ret = i2c_master_transmit_receive(es_dev, &reg, 1, val, 1, 100);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: read reg 0x%02x failed: %s\n", reg, esp_err_to_name(ret));
    }
    return ret;
}

static int get_coeff(uint32_t mclk, uint32_t rate) {
    for (unsigned i = 0; i < sizeof(coeff_div) / sizeof(coeff_div[0]); i++) {
        if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
            return (int)i;
    }
    return -1;
}

// Program the clock-manager registers for the given (mclk, rate) pair.
// Codec is a slave, MCLK sourced from the MCLK pin (not inverted).
static esp_err_t es8311_config_clock(int coeff) {
    uint8_t regv;
    esp_err_t ret;

    // Pre-divider / pre-multiplier (reg02). pre_multi 1/2/4/8 -> 0/1/2/3.
    uint8_t datmp = 0;
    switch (coeff_div[coeff].pre_multi) {
        case 1: datmp = 0; break;
        case 2: datmp = 1; break;
        case 4: datmp = 2; break;
        case 8: datmp = 3; break;
        default: break;
    }
    ret = es_read(ES8311_CLK_MANAGER_REG02, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0x07;
    regv |= (coeff_div[coeff].pre_div - 1) << 5;
    regv |= datmp << 3;
    ret = es_write(ES8311_CLK_MANAGER_REG02, regv);
    if (ret != ESP_OK) return ret;

    // ADC/DAC clock dividers (reg05).
    regv = (coeff_div[coeff].adc_div - 1) << 4;
    regv |= (coeff_div[coeff].dac_div - 1) << 0;
    ret = es_write(ES8311_CLK_MANAGER_REG05, regv);
    if (ret != ESP_OK) return ret;

    // ADC over-sample rate + fs mode (reg03).
    ret = es_read(ES8311_CLK_MANAGER_REG03, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0x80;
    regv |= coeff_div[coeff].fs_mode << 6;
    regv |= coeff_div[coeff].adc_osr << 0;
    ret = es_write(ES8311_CLK_MANAGER_REG03, regv);
    if (ret != ESP_OK) return ret;

    // DAC over-sample rate (reg04).
    ret = es_read(ES8311_CLK_MANAGER_REG04, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0x80;
    regv |= coeff_div[coeff].dac_osr << 0;
    ret = es_write(ES8311_CLK_MANAGER_REG04, regv);
    if (ret != ESP_OK) return ret;

    // LRCK divider high/low (reg07/reg08).
    ret = es_read(ES8311_CLK_MANAGER_REG07, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0xC0;
    regv |= coeff_div[coeff].lrck_h << 0;
    ret = es_write(ES8311_CLK_MANAGER_REG07, regv);
    if (ret != ESP_OK) return ret;
    regv = coeff_div[coeff].lrck_l;
    ret = es_write(ES8311_CLK_MANAGER_REG08, regv);
    if (ret != ESP_OK) return ret;

    // BCLK divider (reg06).
    ret = es_read(ES8311_CLK_MANAGER_REG06, &regv);
    if (ret != ESP_OK) return ret;
    regv &= 0xE0;
    if (coeff_div[coeff].bclk_div < 19)
        regv |= (coeff_div[coeff].bclk_div - 1) << 0;
    else
        regv |= (coeff_div[coeff].bclk_div) << 0;
    ret = es_write(ES8311_CLK_MANAGER_REG06, regv);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

amy_err_t amy_es8311_init(int i2c_port, int8_t sda, int8_t scl, uint8_t i2c_addr,
                          int8_t pa_enable_gpio, uint8_t pa_active_low,
                          uint32_t sample_rate, uint32_t mclk_hz, uint8_t volume) {
    // The default ESP I2S path drives 32-bit MSB (left-justified) slots.
#ifdef AMY_ES8311_I2S_PHILIPS
    const uint8_t fmt_bits = 0x00;   // standard I2S (Philips)
#else
    const uint8_t fmt_bits = 0x01;   // MSB / left-justified
#endif
    const uint8_t len_bits = 0x10;   // 32-bit word length

    int coeff = get_coeff(mclk_hz, sample_rate);
    if (coeff < 0) {
        fprintf(stderr, "ES8311: no clock coefficients for %luHz MCLK at %luHz\n",
                (unsigned long)mclk_hz, (unsigned long)sample_rate);
        return -1;
    }

    // Bring up the I2C master bus + codec device.
    i2c_master_bus_config_t bus_conf = {
        .i2c_port = i2c_port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_conf, &bus);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: I2C bus init failed: %s\n", esp_err_to_name(ret));
        return -1;
    }
    i2c_device_config_t dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400000,
    };
    ret = i2c_master_bus_add_device(bus, &dev_conf, &es_dev);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: I2C add device 0x%02x failed: %s\n", i2c_addr, esp_err_to_name(ret));
        i2c_del_master_bus(bus);
        return -1;
    }

    // From this point on, any hardware-access failure tears down the temporary
    // I2C bus and returns an error without enabling the power amplifier.
#define ES8311_INIT_CHECK(call) do { \
        ret = (call); \
        if (ret != ESP_OK) goto init_failed; \
    } while (0)

    // Presence check (ES8311 chip id is 0x83 0x11).
    uint8_t id1, id2, regv;
    ES8311_INIT_CHECK(es_read(ES8311_CHD1_REGFD, &id1));
    ES8311_INIT_CHECK(es_read(ES8311_CHD2_REGFE, &id2));
    if (!(id1 == 0x83 && id2 == 0x11)) {
        fprintf(stderr, "ES8311: unexpected chip id 0x%02x%02x (expected 0x8311)\n", id1, id2);
        ret = ESP_FAIL;
        goto init_failed;
    }

    // ---- Codec init (clock manager + power) ----
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG01, 0x30));
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG02, 0x00));
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG03, 0x10));
    ES8311_INIT_CHECK(es_write(ES8311_ADC_REG16, 0x24));
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG04, 0x10));
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG05, 0x00));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG0B, 0x00));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG0C, 0x00));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG10, 0x1F));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG11, 0x7F));
    ES8311_INIT_CHECK(es_write(ES8311_RESET_REG00, 0x80));   // power up, CSM/clock on

    // Slave mode: clear bit6 of reg00.
    ES8311_INIT_CHECK(es_read(ES8311_RESET_REG00, &regv));
    ES8311_INIT_CHECK(es_write(ES8311_RESET_REG00, regv & 0xBF));
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG01, 0x3F));
    // MCLK from the MCLK pin (not BCLK): clear bit7 of reg01.
    ES8311_INIT_CHECK(es_read(ES8311_CLK_MANAGER_REG01, &regv));
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG01, regv & 0x7F));

    ES8311_INIT_CHECK(es8311_config_clock(coeff));

    // MCLK / SCLK not inverted.
    ES8311_INIT_CHECK(es_read(ES8311_CLK_MANAGER_REG01, &regv));
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG01, regv & ~0x40));
    ES8311_INIT_CHECK(es_read(ES8311_CLK_MANAGER_REG06, &regv));
    ES8311_INIT_CHECK(es_write(ES8311_CLK_MANAGER_REG06, regv & ~0x20));

    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG13, 0x10));
    ES8311_INIT_CHECK(es_write(ES8311_ADC_REG1B, 0x0A));
    ES8311_INIT_CHECK(es_write(ES8311_ADC_REG1C, 0x6A));

    // ---- Serial data-port format: 32-bit, left-justified (or Philips) ----
    // DAC port (reg09): word length in bits [4:2], format in bits [1:0].
    ES8311_INIT_CHECK(es_write(ES8311_SDPIN_REG09, (len_bits) | fmt_bits));
    ES8311_INIT_CHECK(es_write(ES8311_SDPOUT_REG0A, (len_bits) | fmt_bits));

    // ---- Start playback (DAC) path ----
    // Un-mute the DAC serial port (clear bit6 of reg09).
    ES8311_INIT_CHECK(es_read(ES8311_SDPIN_REG09, &regv));
    ES8311_INIT_CHECK(es_write(ES8311_SDPIN_REG09, regv & ~0x40));
    ES8311_INIT_CHECK(es_write(ES8311_ADC_REG17, 0xBF));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG0E, 0x02));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG12, 0x00));   // enable DAC
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG14, 0x1A));   // analog PGA / not DMIC
    ES8311_INIT_CHECK(es_read(ES8311_SYSTEM_REG14, &regv));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG14, regv & ~0x40));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG0D, 0x01));
    ES8311_INIT_CHECK(es_write(ES8311_ADC_REG15, 0x40));
    ES8311_INIT_CHECK(es_write(ES8311_DAC_REG37, 0x48));
    ES8311_INIT_CHECK(es_write(ES8311_GP_REG45, 0x00));

    // ---- Volume + un-mute ----
    if (volume > 100) volume = 100;
    ES8311_INIT_CHECK(es_write(ES8311_DAC_REG32, (uint8_t)((volume * 2550) / 1000)));  // 0..255
    // Un-mute: clear DAC mute bits (reg31 [6:5]) and clear the system mute (reg12).
    ES8311_INIT_CHECK(es_read(ES8311_DAC_REG31, &regv));
    ES8311_INIT_CHECK(es_write(ES8311_DAC_REG31, regv & 0x9F));
    ES8311_INIT_CHECK(es_write(ES8311_SYSTEM_REG12, 0x00));

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
    ret = i2c_master_bus_rm_device(es_dev);
    es_dev = NULL;
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: I2C device cleanup failed: %s\n", esp_err_to_name(ret));
        i2c_del_master_bus(bus);
        return -1;
    }
    ret = i2c_del_master_bus(bus);
    if (ret != ESP_OK) {
        fprintf(stderr, "ES8311: I2C bus cleanup failed: %s\n", esp_err_to_name(ret));
        return -1;
    }

#undef ES8311_INIT_CHECK
    return AMY_OK;

init_failed:
    if (es_dev != NULL) {
        i2c_master_bus_rm_device(es_dev);
        es_dev = NULL;
    }
    if (bus != NULL) i2c_del_master_bus(bus);
    return -1;
}

#endif // ESP_PLATFORM
