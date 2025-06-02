// usb.h
// tinyusb stuff

#ifdef AMY_MCU

#include "class/audio/audio.h"
#include "class/midi/midi.h"
#include "tusb.h"
#include "device/dcd.h"

// Initialise TinyUSB device.
static inline void amy_usbd_init_tud(void) {
    tusb_init();
    tud_cdc_configure_fifo_t cfg = { .rx_persistent = 0, .tx_persistent = 1 };
    tud_cdc_configure_fifo(&cfg);
}

// Run the TinyUSB device task
void amy_usbd_task(void);

// Schedule a call to mp_usbd_task(), even if no USB interrupt has occurred
void amy_usbd_schedule_task(void);

extern void amy_usbd_port_get_serial_number(char *buf);

// Most ports need to write a hexadecimal serial number from a byte array. This
// is a helper function for this. out_str must be long enough to hold a string of total
// length (2 * bytes_len + 1) (including NUL terminator).
void amy_usbd_hex_str(char *out_str, const uint8_t *bytes, size_t bytes_len);

// Built-in USB device and configuration descriptor values
extern const tusb_desc_device_t amy_usbd_builtin_desc_dev;
extern const uint8_t amy_usbd_builtin_desc_cfg[CONFIG_TOTAL_LEN_MIDI];

static inline void amy_usbd_init(void) {
    mp_usbd_init_tud();
}

#endif