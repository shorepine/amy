# AMY - A high-performance fixed-point Music synthesizer librarY for microcontrollers

AMY is a fast, small and accurate music synthesizer library written in C with Python and Arduino bindings that deals with combinations of many oscillators very well. It can easily be embedded into almost any program, architecture or microcontroller. We've run AMY on [the web](https://shorepine.github.io/amy/), Mac, Linux, ESP32, ESP32S3 and ESP32P4, Teensy 3.6, Teensy 4.1, the Raspberry Pi, the Playdate, Pi Pico RP2040, the Pi Pico 2 RP2350, iOS devices, the Electro-Smith Daisy (ARM Cortex M7), and more to come. is highly optimized for polyphony and poly-timbral operation on even the lowest power and constrained RAM microcontroller but can scale to as many oscillators as you want. 

It can be used as a very good analog-type synthesizer (Juno-6 style) a FM synthesizer (DX7 style), a partial breakpoint synthesizer (Alles machine or Atari AMY), a [very good synthesized piano](https://shorepine.github.io/amy/piano.html), a sampler (where you load in your own PCM data), a drum machine (808-style PCM samples are included), or as a lower level toolkit to make your own combinations of oscillators, filters, LFOs and effects. 

AMY now supports MIDI internally and can manage synthesizer note messages for you, including voice stealing and controller changes. 

AMY powers the multi-speaker mesh synthesizer [Alles](https://github.com/shorepine/alles), as well as the [Tulip Creative Computer](https://github.com/shorepine/tulipcc). Let us know if you use AMY for your own projects and we'll add it here!

AMY was built by [DAn Ellis](https://research.google/people/DanEllis/) and [Brian Whitman](https://notes.variogram.com), and would love your contributions.

[![shore pine sound systems discord](https://raw.githubusercontent.com/shorepine/tulipcc/main/docs/pics/shorepine100.png) **Chat about AMY on our Discord!**](https://discord.gg/TzBFkUb8pG)

## More information

 * [**Interactive AMY tutorial**](https://shorepine.github.io/amy/tutorial.html)
 * [**AMY API**](docs/api.md)
 * [**AMY Synthesizer Details**](docs/synth.md)
 * [**AMY's MIDI specification**](docs/midi.md)
 * [**AMY in Arduino Getting Started**](docs/arduino.md)
 * [**Other AMY web demos**](https://shorepine.github.io/amy/)

AMY supports

 * MIDI input support and synthesizer voice management, including voice stealing, controllers and per-channel multi-timbral operation
 * A strong Juno-6 style analog synthesizer
 * An operator / algorithm-based frequency modulation (FM) synth, modeled after the DX-7
 * PCM sampler, reading from a baked-in buffer of percussive and misc samples, or by loading samples with looping and base midi note
 * karplus-strong string with adjustable feedback 
 * An arbitrary number of band-limited oscillators, each with adjustable frequency, pan, phase, amplitude:
   * pulse (+ adjustable duty cycle), sine, saw (up and down), triangle, noise 
 * Stereo audio input or audio buffers in code can be used as an oscillator for real time audio effects
 * Biquad low-pass, bandpass or hi-pass filters with cutoff and resonance, can be assigned to any oscillator
 * Reverb, echo and chorus effects, set globally
 * An additive partial synthesizer
 * Each oscillator has 2 envelope generators, which can modify any combination of amplitude, frequency, PWM duty, filter cutoff, or pan over time
 * Each oscillator can also act as an modulator to modify any combination of parameters of another oscillator, for example, a bass drum can be indicated via a half phase sine wave at 0.25Hz modulating the frequency of another sine wave. 
 * Control of overall gain and 3-band EQ
 * 300+ built in preset patches for PCM, DX7, piano and Juno-6
 * A front end for DX7 and Juno-6 SYSEX patches and conversion setup commands 
 * Built-in event clock and pattern sequencer, using hardware real time timers on microcontrollers
 * Multi-core (including microcontrollers) for rendering if available

The FM synth provides a Python library, [`fm.py`](https://github.com/shorepine/amy/blob/main/amy/fm.py) that can convert any DX7 patch into an AMY patch.

The Juno-6 emulation provides [`juno.py`](https://github.com/shorepine/amy/blob/main/amy/juno.py) and can read in Juno-6 SYSEX patches and convert them into AMY patches.

[The partials-driven piano voice and the code to generate the partials are described here](https://shorepine.github.io/amy/piano.html).

## Using AMY in Arduino

AMY will run on many modern microcontrollers under Arduino. On most platforms, we handle sending audio out to an I2S interface and handling MIDI input. Some platforms support more features than others. 

**Please see our more detailed [Getting Started on Arduino](docs/arduino.md) page for more details.**

## Using AMY in Python on any platform

You can `import amy` in Python and have it render either out to your speakers or to a buffer of samples you can process on your own. To install the `amy` library, run `pip install .`. You can also run `make test` to install the library and run a series of tests.

[**Please see our interactive AMY tutorial for more tips on using AMY**](https://shorepine.github.io/amy/tutorial.html)

## Using AMY on the web

We provide an `emscripten` port of AMY that runs in Javascript. [See the AMY web demos](https://shorepine.github.io/amy/). To build for the web, use `make docs/amy.js`. It will generate `amy.js` in `docs/`.  

## Using AMY in any other software

To use AMY in your own software, simply copy the .c and .h files in `src` to your program and compile them. No other libraries should be required to synthesize audio in AMY. 

To run a simple C example on many platforms:

```
make
./amy-example # you should hear tones out your default speaker, use ./amy-example -h for options
```

# AMY quickstart

[**Please see our interactive AMY tutorial for more tips on using AMY**](https://shorepine.github.io/amy/tutorial.html)

## MIDI mode

AMY provides a [MIDI mode](docs/midi.md) by default that lets you control many parts of AMY over MIDI. You can even control the underlying oscillators over SYSEX. See our [MIDI documentation](docs/midi.md) for more details. The simplest way to use AMY is to start it and them play MIDI notes to it. By default, AMY boots with a Juno-6 patch 0 on MIDI channel 1.

In Python:

```python
>>> import amy; amy.live()
>>> # play MIDI notes using system MIDI
```

In C: 

```c
amy_config = amy_default_config()
amy_start(amy_config);
amy_live_start();
// play MIDI notes using system MIDI or UART MIDI on microcontrollers
```

In Javascript (see [minimal.html](docs/minimal.html) for the full example): 

```html
<script type="text/javascript" src="amy.js"></script>
<script type="text/javascript" src="amy_connector.js"></script>
<script>
    // You have to start AMY on a user click for audio to work 
    document.body.addEventListener('click', amy_js_start, true); 
</script>
<!-- Now play MIDI notes over webMIDI -->
```

AMY supports [note commands, some MIDI controllers, and program changes to change the patch.](docs/midi.md)


## Controlling AMY in code

Presumably you'd like to explicitly tell AMY what to play. You can control AMY from almost anything. We mostly work in Python, C or Javascript, but AMY has been built to work with anything that can send a string.

AMY has two API interfaces: _wire messages_ and `amy_event`. An AMY wire message is a string that looks like `v0n50l1K130r0Z`, with each letter corresponding to a field (like `v0` means `oscillator 0`, `n50` means midi note 50, `K130` means patch number 130, etc.) Wire messages are converted into `amy_event`s within AMY once received. 

So in C, or JS, you'd fill an `amy_event` struct to define a single event of the synthesizer. For example, that wire message above is:

```c
amy_event e = amy_default_event();
e.osc = 0;
e.patch_number = 130;
e.velocity = 1;
e.midi_note = 50;
e.voices[0] = 0;
amy_add_event(&e);
```

In Python, we provide the `amy` package that generates wire messages from a Pythonic `amy.send(**kwargs)`. In Python, you'd do

```python
amy.send(osc=0, patch = 130, vel = 1, note = 50, voices = [0])
```

Wire messages are used in AMY as a compact serialization of AMY events and become useful when communicating between AMY and other programs that may not be linked together. For example, [Alles](https://github.com/shorepine/alles) uses wire messages over Wi-Fi UDP to control a mesh of AMY synthesizers. [Tulip Web](https://tulip.computer/run) sends wire messages from the Micropython web process to the AudioWorklet running AMY on the web. We also store the Juno-6 and DX7 patches within AMY itself using wire messages, which helps keep the code size down. 

You can also send wire messages over SYSEX to AMY, if you want to control AMY over MIDI beyond the default MIDI mode. [See our MIDI documentation for more details.](docs/midi.md)

It's good to understand what wire messages are but you don't need to construct them directly if you're linking AMY in your software. Use `amy_event` or `amy.send()` in Python to control AMY for almost all use cases.

# More information

 * [**Interactive AMY tutorial**](https://shorepine.github.io/amy/tutorial.html)
 * [**AMY API**](docs/api.md)
 * [**AMY Synthesizer Details**](docs/synth.md)
 * [**AMY's MIDI specification**](docs/midi.md)
 * [**AMY in Arduino Getting Started**](docs/arduino.md)
 * [**Other AMY web demos**](https://shorepine.github.io/amy/)

 [![shore pine sound systems discord](https://raw.githubusercontent.com/shorepine/tulipcc/main/docs/pics/shorepine100.png) **Chat about AMY on our Discord!**](https://discord.gg/TzBFkUb8pG)


