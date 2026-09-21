// amy_i2c.h
// Tiny shared helper for AMY's ESP32 codec control buses.
//
// Several boards need an audio codec configured over I2C before I2S will pass
// any audio (the PCM9211 on AMYboard, the ES8311 on the Freenove FNK0104, ...).
// They all follow the same pattern: open an ESP-IDF driver_ng I2C master bus
// (driver/i2c_master.h -- the legacy driver would fight Arduino's Wire), write a
// handful of registers, then tear the bus back down so Wire, a touch panel or
// anything else sharing those pins can claim them.  Getting that ownership
// dance wrong fails silently (no audio, no error), so it lives in one place
// here rather than being copied per codec.  Implemented in i2s.c.

#ifndef AMY_I2C_H
#define AMY_I2C_H

#ifdef ESP_PLATFORM

#include <stdint.h>
#include "driver/i2c_master.h"

// A temporarily-opened I2C master bus plus the one device we talk to on it.
typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
} amy_i2c_dev_t;

// Open i2c_port on sda/scl and attach the 7-bit device at addr.  On failure
// nothing is left open and *d is zeroed, so the caller can just report the error
// (calling amy_i2c_close_device() anyway is harmless).
esp_err_t amy_i2c_open_device(amy_i2c_dev_t *d, int i2c_port, int8_t sda, int8_t scl,
                              uint8_t addr, uint32_t scl_speed_hz);

// Detach the device and delete the bus, releasing the pins.  Safe to call on a
// zeroed or partly-opened amy_i2c_dev_t, and safe to call twice.
void amy_i2c_close_device(amy_i2c_dev_t *d);

// Single register transactions.  Both return ESP_ERR_INVALID_STATE, rather than
// handing a NULL handle to the IDF, if d isn't open.
esp_err_t amy_i2c_write_reg(const amy_i2c_dev_t *d, uint8_t reg, uint8_t val);
esp_err_t amy_i2c_read_reg(const amy_i2c_dev_t *d, uint8_t reg, uint8_t *val);

#endif // ESP_PLATFORM
#endif // AMY_I2C_H
