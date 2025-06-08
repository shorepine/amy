# Arduino Setup for AMY



Copy this repository to your `Arduino/libraries` folder as `Arduino/libraries/amy`, and `#include <AMY-Arduino.h>`. There are examples for the Pi Pico, ESP32 (and variants), and Teensy (works on 4.X and 3.6) Use the File->Examples->AMY Synthesizer menu to find them. 

The examples rely on the following board packages and libraries:
 * RP2040 / Pi Pico: [`arduino-pico`](https://arduino-pico.readthedocs.io/en/latest/install.html#installing-via-arduino-boards-manager)
 * Teensy: [`teensyduino`](https://www.pjrc.com/teensy/td_download.html)
 * ESP32/ESP32-S3/etc: [`arduino-esp32`](https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/installing.html) - use a 2.0.14+ version when installing
 * The USB MIDI example requires the [MIDI Library](https://www.arduino.cc/reference/en/libraries/midi-library/)

You can use both cores of supported chips (RP2040 or ESP32) for more oscillators and voices. We provide Arduino examples for the Arduino ESP32 in multicore, and a `pico-sdk` example for the RP2040 that renders in multicore. If you really want to push the chips to the limit, we recommend using native C code using the `pico-sdk`  or `ESP-IDF`. 

We have a simple [dual core ESP-IDF example available](https://github.com/shorepine/amy_dual_core_esp32) or you can see [Alles](https://github.com/shorepine/alles).


## ESP32, ESP32-S3, ESP32-P4

Tested: Arduino IDE 2.3.6 (mac)



## Teensy 4.0, 4.1

Tested: Arduino IDE 2.3.6 (mac) + [Teensy 1.59.0](https://www.pjrc.com/teensy/teensyduino.html)

USB audio and i2s audio output happen at the same time

if you set USB Type to Audio, it will appear as a USB audio device on your computer, called "AMY Synthesizer"

Note: USB audio from Teensy is fiddly and often slow to enumerate, i have to wait a minute or two before it shows up on my Mac. This seems to be unrelated to AMY. Please ask on the Teensy forums if you're having trouble with USB Audio. Once it is enumerated and running, it does seem stable.

For I2S, you have to use the default i2s1 pins. The pins you set in AMY config are ignored. That's `21 -> BLCK`, `20 -> LRC`, `DOUT -> 7`

For UART MIDI, we use Serial8, the pins you set in AMY config are ignored. That's `MIDI_OUT -> 35`, `MIDI_IN -> 34`.



## Pi Pico RP2040 and Pi Pico 2 RP2350

Tested: Arduino IDE 2.3.6 (mac)


## Electro-Smith Daisy Seed

See [Daisy README](../daisy/README.md)

