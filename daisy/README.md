# Daisy example using AMY

This is an example of using AMY with an Electrosmith Daisy: https://electro-smith.com

It supports audio in, serial MIDI in, AMY sequencer.

To use, set your location of AMY in the `Makefile`, then set up your Daisy dev environment, then put the board into DFU mode, and run

```
make clean && make && make program-boot; sleep 1.5; make program-dfu
```

After it flashes, you should hear the AMY startup chime from the Daisy's line out.  If you have a MIDI serial interface hooked up to the Daisy (D14 USART1Rx) you should be able to play the default Juno synth on channel 1.
