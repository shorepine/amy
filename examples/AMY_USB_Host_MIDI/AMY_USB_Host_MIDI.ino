#include <AMY-Arduino.h>
#include <USBConnection.h>  // ESP32_Host_MIDI

// USB Host MIDI synth for ESP32-S3 / ESP32-S2 / ESP32-P4.
// Plug a USB MIDI keyboard directly via USB OTG — no shields or adapters needed.
//
// The ESP32's USB OTG peripheral handles device enumeration, endpoint detection,
// and hot-plug. Incoming USB-MIDI event packets are forwarded into AMY's MIDI
// parser, which drives the default Juno-6 polyphonic synth.
//
// Hardware:
//   - ESP32-S3 board (LilyGO T-Display-S3, ESP32-S3 DevKit, etc.)
//   - I2S DAC (PCM5102, UDA1334, or similar)
//   - USB MIDI keyboard via OTG cable
//
// Required library:
//   - ESP32_Host_MIDI  https://github.com/sauloverissimo/ESP32_Host_MIDI
//
// Tested with:
//   - LilyGO T-Display-S3 + PCM5102A DAC + Arturia Minilab 25

extern "C" {
    void convert_midi_bytes_to_messages(uint8_t *data, size_t len, uint8_t usb);
}

USBConnection usbMidi;

// Bridge: forward USB-MIDI event packets into AMY's MIDI parser.
// USB-MIDI packets are 4 bytes: [CIN+Cable, MIDI0, MIDI1, MIDI2].
// We pass bytes 1-3 as a group with the USB flag so AMY's parser can
// exit early after completing the message, skipping padded bytes on
// short messages (e.g. Program Change, Channel Pressure).
void onUsbMidiData(void* ctx, const uint8_t* data, size_t length) {
    for (size_t i = 0; i + 4 <= length; i += 4) {
        if ((data[i] & 0x0F) == 0x00) continue;  // Skip empty packets
        convert_midi_bytes_to_messages((uint8_t*)(data + i + 1), 3, 1);
    }
}

void setup() {
    Serial.begin(115200);

    amy_config_t amy_config = amy_default_config();
    amy_config.features.startup_bleep = 1;
    // Install the default synths on MIDI channels 1, 2, and 10.
    amy_config.features.default_synths = 1;

    // I2S audio output — adjust pins for your board.
    amy_config.audio = AMY_AUDIO_IS_I2S;
    amy_config.i2s_bclk = 8;
    amy_config.i2s_lrc = 9;
    amy_config.i2s_dout = 10;

    // We handle MIDI ourselves via USB Host, so disable AMY's built-in MIDI.
    amy_config.midi = AMY_MIDI_IS_NONE;

    amy_start(amy_config);

    // Start USB Host MIDI and register the bridge callback.
    usbMidi.setMidiCallback(onUsbMidiData, NULL);
    usbMidi.begin();

    Serial.println("AMY USB Host MIDI ready. Plug in a USB MIDI keyboard.");
}

void loop() {
    amy_update();
    usbMidi.task();
}
