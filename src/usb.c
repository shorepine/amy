// usb.c
// tinyusb stuff

#include "amy.h"

#if defined AMY_MCU

#define CONFIG_TOTAL_LEN_MIDI  (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_CDC_DESC_LEN)

// For AMYBOARD_ARDUINO, provide defaults for MicroPython-originated macros
#ifdef AMYBOARD_ARDUINO

#ifndef MICROPY_HW_USB_VID
#define MICROPY_HW_USB_VID (0xCAF0)
#endif
#ifndef MICROPY_HW_USB_PID
#define MICROPY_HW_USB_PID (0x0001)
#endif
#ifndef MICROPY_HW_USB_DESC_STR_MAX
#define MICROPY_HW_USB_DESC_STR_MAX (32)
#endif
#ifndef MICROPY_HW_USB_MANUFACTURER_STRING
#define MICROPY_HW_USB_MANUFACTURER_STRING "AMYboard"
#endif
#ifndef MICROPY_HW_USB_PRODUCT_FS_STRING
#define MICROPY_HW_USB_PRODUCT_FS_STRING "AMYboard"
#endif
#ifndef MICROPY_HW_USB_CDC_INTERFACE_STRING
#define MICROPY_HW_USB_CDC_INTERFACE_STRING "AMYboard"
#endif
#ifndef MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE
#define MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE (0)
#endif

#ifndef MICROPY_HW_USB_MSC_INTERFACE_STRING
#define MICROPY_HW_USB_MSC_INTERFACE_STRING "AMYboard"
#endif

#define USBD_STR_MANUF  (0x01)
#define USBD_STR_PRODUCT (0x02)
#define USBD_STR_SERIAL (0x03)
#define USBD_STR_CDC    (0x04)
#define USBD_STR_MSC    (0x05)

#endif // AMYBOARD_ARDUINO

#include "usb.h"
#include "tusb.h"


#define USBD_CDC_CMD_MAX_SIZE (8)
#define USBD_CDC_IN_OUT_MAX_SIZE ((CFG_TUD_MAX_SPEED == OPT_MODE_HIGH_SPEED) ? 512 : 64)

const tusb_desc_device_t amy_usbd_builtin_desc_dev = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = MICROPY_HW_USB_VID,
    .idProduct = MICROPY_HW_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = USBD_STR_MANUF,
    .iProduct = USBD_STR_PRODUCT,
    .iSerialNumber = USBD_STR_SERIAL,
    .bNumConfigurations = 1,
};


enum
{
    ITF_NUM_CDC            = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};


#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_MIDI   0x04

const uint8_t amy_usbd_builtin_desc_cfg[CONFIG_TOTAL_LEN_MIDI] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN_MIDI, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF,
        8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 64),

};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    char serial_buf[MICROPY_HW_USB_DESC_STR_MAX + 1]; // Includes terminating NUL byte
    static uint16_t desc_wstr[MICROPY_HW_USB_DESC_STR_MAX + 1]; // Includes prefix uint16_t
    const char *desc_str = NULL;
    uint16_t desc_len;

    #if MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE
    desc_str = amy_usbd_runtime_string_cb(index);
    #endif

    if (index == 0) {
        // String descriptor 0 is special, see USB 2.0 section 9.6.7 String
        //
        // Expect any runtime value in desc_str to be a fully formed descriptor
        if (desc_str == NULL) {
            desc_str = "\x04\x03\x09\x04"; // Descriptor for "English"
        }
        if (desc_str[0] < sizeof(desc_wstr)) {
            memcpy(desc_wstr, desc_str, desc_str[0]);
            return desc_wstr;
        }
        return NULL; // Descriptor length too long (or malformed), stall endpoint
    }

    // Otherwise, generate a "UNICODE" string descriptor from the C string

    if (desc_str == NULL) {
        // Fall back to the "static" string
        switch (index) {
            case USBD_STR_SERIAL:
                amy_usbd_port_get_serial_number(serial_buf);
                desc_str = serial_buf;
                break;
            case USBD_STR_MANUF:
                desc_str = MICROPY_HW_USB_MANUFACTURER_STRING;
                break;
            case USBD_STR_PRODUCT:
                desc_str = MICROPY_HW_USB_PRODUCT_FS_STRING;
                break;
            #if CFG_TUD_CDC
            case USBD_STR_CDC:
                desc_str = MICROPY_HW_USB_CDC_INTERFACE_STRING;
                break;
            #endif
            #if CFG_TUD_MSC
            case USBD_STR_MSC:
                desc_str = MICROPY_HW_USB_MSC_INTERFACE_STRING;
                break;
            #endif
            default:
                break;
        }
    }

    if (desc_str == NULL) {
        return NULL; // No string, STALL the endpoint
    }

    // Convert from narrow string to wide string
    desc_len = 2;
    for (int i = 0; i < MICROPY_HW_USB_DESC_STR_MAX && desc_str[i] != 0; i++) {
        desc_wstr[1 + i] = desc_str[i];
        desc_len += 2;
    }

    // first byte is length (including header), second byte is string type
    desc_wstr[0] = (TUSB_DESC_STRING << 8) | desc_len;

    return desc_wstr;
}


#ifdef AMYBOARD_ARDUINO

#include "esp_mac.h"
#include "esp32-hal-tinyusb.h"

// Override Arduino's weak descriptor callbacks with amy's MIDI+CDC descriptors
uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&amy_usbd_builtin_desc_dev;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return amy_usbd_builtin_desc_cfg;
}

// Provide serial number from ESP32 MAC address
void amy_usbd_port_get_serial_number(char *buf) {
    uint8_t mac[8];
    esp_efuse_mac_get_default(mac);
    amy_usbd_hex_str(buf, mac, sizeof(mac));
}

void amy_usbd_hex_str(char *out_str, const uint8_t *bytes, size_t bytes_len) {
    for (size_t i = 0; i < bytes_len; i++) {
        uint8_t b = bytes[i];
        uint8_t hi = b >> 4;
        uint8_t lo = b & 0x0F;
        out_str[i * 2] = hi < 10 ? '0' + hi : 'A' + hi - 10;
        out_str[i * 2 + 1] = lo < 10 ? '0' + lo : 'A' + lo - 10;
    }
    out_str[bytes_len * 2] = '\0';
}

void amy_arduino_usb_setup(void) {
    // Use Arduino's tinyusb_init which handles USB PHY setup, tusb_init(),
    // and creates the TinyUSB device task. Our non-weak descriptor callbacks
    // above ensure the host sees amy's MIDI+CDC descriptors.
    tinyusb_device_config_t cfg = {
        .vid = MICROPY_HW_USB_VID,
        .pid = MICROPY_HW_USB_PID,
        .product_name = MICROPY_HW_USB_PRODUCT_FS_STRING,
        .manufacturer_name = MICROPY_HW_USB_MANUFACTURER_STRING,
        .serial_number = "",
        .fw_version = 0x0100,
        .usb_version = 0x0200,
        .usb_class = TUSB_CLASS_MISC,
        .usb_subclass = MISC_SUBCLASS_COMMON,
        .usb_protocol = MISC_PROTOCOL_IAD,
        .usb_attributes = TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        .usb_power_ma = 100,
        .webusb_enabled = false,
        .webusb_url = "",
    };
    tinyusb_init(&cfg);
}

#endif // AMYBOARD_ARDUINO

#endif // AMY_MCU