#include <AMY-Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <I2S.h>

/*
  To use this, make sure you're using Earl Philhower's RP2040 board
  And install the MIDI Library library (by Francois Best, lathoub)
  And set the USB Stack to Adafruit TinyUSB
*/

// USB MIDI object
Adafruit_USBD_MIDI usb_midi;

// Create a new instance of the Arduino MIDI Library,
// and attach usb_midi as the transport.
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

I2S i2s(OUTPUT);
AMY amy;

void setup() {

  // Set your I2S pins. 
  i2s.setBCLK(9); // BCLK = SCLK. MCLK = SCLK + 1
  i2s.setDATA(8); // DATA = DIN, SD, SDOUT, DOUT
  i2s.setBitsPerSample(16); 
  i2s.setBuffers(2, AMY_BLOCK_SIZE*AMY_NCHANS*BYTES_PER_SAMPLE/4, 0);
  i2s.begin(AMY_SAMPLE_RATE);

#if defined(ARDUINO_ARCH_MBED) && defined(ARDUINO_ARCH_RP2040)
  // Manual begin() is required on core without built-in support for TinyUSB such as mbed rp2040
  TinyUSB_Device_Init(0);
#endif

  // Initialize MIDI, and listen to all MIDI channels
  // This will also call usb_midi's begin()
  MIDI.begin(MIDI_CHANNEL_OMNI);

  // Attach the handleNoteOn function to the MIDI Library. It will
  // be called whenever the Bluefruit receives MIDI Note On messages.
  MIDI.setHandleNoteOn(handleNoteOn);

  // Do the same for MIDI Note Off messages.
  MIDI.setHandleNoteOff(handleNoteOff);

  // wait until device mounted
  while( !TinyUSBDevice.mounted() ) delay(1);

  // Start up AMY
  amy.begin(1, 0, 0);

  // Setup the voice with a Juno patch
  struct event e = amy.default_event();
  e.load_patch = 0; // Juno Patch 0
  strcpy(e.voices, "0");
  amy.add_event(e);
}



void handleNoteOn(byte channel, byte pitch, byte velocity)
{
  struct event e = amy.default_event();
  strcpy(e.voices, "0");
  e.midi_note = pitch;
  e.velocity = (float)velocity/127.0;
  amy.add_event(e);

}

void handleNoteOff(byte channel, byte pitch, byte velocity)
{
  // Log when a note is released.
}

void loop() {
  // In your loop you have to get the buffer of samples and then play it out your device
  uint8_t * samples = (uint8_t*)amy.render_to_buffer();
  // Block until ready
  while(i2s.availableForWrite()<AMY_BLOCK_SIZE*BYTES_PER_SAMPLE*AMY_NCHANS);
  size_t written = i2s.write((const uint8_t*)samples, (size_t)AMY_BLOCK_SIZE*BYTES_PER_SAMPLE*AMY_NCHANS);
  MIDI.read();  

}

