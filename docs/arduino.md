# Arduino Setup for AMY



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

