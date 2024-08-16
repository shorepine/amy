# Daisy example using AMY

This is a very barebones example of using AMY with an Electrosmith Daisy: https://electro-smith.com

To use, set your location of AMY in the `Makefile`, then set up your Daisy dev environment, then put the board into DFU mode, and run

```
make clean && make && make program-boot; sleep 1.5; make program-dfu
```

After it flashes, you should hear 15 Juno-6 notes out the line out port. 

This example could use some work to work better with the Daisy. [Please let us know if you can help!](https://github.com/shorepine/amy/issues/159)
