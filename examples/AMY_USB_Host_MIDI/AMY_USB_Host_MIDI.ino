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
    void amy_process_single_midi_byte(uint8_t byte, uint8_t from_usb);
}

USBConnection usbMidi;

// Bridge: forward USB-MIDI event packets into AMY's MIDI parser.
// USB-MIDI packets are 4 bytes: [CIN+Cable, MIDI0, MIDI1, MIDI2].
// We skip byte 0 (Cable/CIN) and feed bytes 1-3 with the USB flag,
// which tells AMY to process them as a complete 3-byte group.
void onUsbMidiData(void* ctx, const uint8_t* data, size_t length) {
    for (size_t i = 0; i + 4 <= length; i += 4) {
        uint8_t cin = data[i] & 0x0F;
        if (cin == 0x00) continue;  // Skip empty packets
        amy_process_single_midi_byte(data[i + 1], 1);
        amy_process_single_midi_byte(data[i + 2], 1);
        amy_process_single_midi_byte(data[i + 3], 1);
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
