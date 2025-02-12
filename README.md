# AMY - A high-performance fixed-point Music synthesizer librarY for microcontrollers

AMY is a fast, small and accurate music synthesizer library written in C with Python and Arduino bindings that deals with combinations of many oscillators very well. It can easily be embedded into almost any program, architecture or microcontroller. We've run AMY on [the web](https://shorepine.github.io/amy/), Mac, Linux, ESP32, ESP32S3 and ESP32P4, Teensy 3.6, Teensy 4.1, the Raspberry Pi, the Playdate, Pi Pico RP2040, the Pi Pico 2 RP2530, iOS devices, the Electro-Smith Daisy (ARM Cortex M7), and more to come. It is highly optimized for polyphony and poly-timbral operation on even the lowest power and constrained RAM microcontroller but can scale to as many cores as you want. 

It can be used as a very good analog-type synthesizer (Juno-6 style) a FM synthesizer (DX7 style), a partial breakpoint synthesizer (Alles machine or Atari AMY), a [very good synthesized piano](https://shorepine.github.io/amy/piano.html), a sampler (where you load in your own PCM data), a drum machine (808-style PCM samples are included), or as a lower level toolkit to make your own combinations of oscillators, filters, LFOs and effects. 

AMY powers the multi-speaker mesh synthesizer [Alles](https://github.com/shorepine/alles), as well as the [Tulip Creative Computer](https://github.com/shorepine/tulipcc). Let us know if you use AMY for your own projects and we'll add it here!

AMY was built by [DAn Ellis](https://research.google/people/DanEllis/) and [Brian Whitman](https://notes.variogram.com), and would love your contributions.

[![shore pine sound systems discord](https://raw.githubusercontent.com/shorepine/tulipcc/main/docs/pics/shorepine100.png) **Chat about AMY on our Discord!**](https://discord.gg/TzBFkUb8pG)


It supports

 * An arbitrary number (compile-time option) of band-limited oscillators, each with adjustable frequency and amplitude:
   * pulse (+ adjustable duty cycle)
   * sine
   * saw (up and down)
   * triangle
   * noise
   * PCM, reading from a baked-in buffer of percussive and misc samples, or by loading samples with looping and base midi note
   * karplus-strong string with adjustable feedback 
   * Stereo audio input can be used as an oscillator for real time audio effects
   * An operator / algorithm-based frequency modulation (FM) synth
 * Biquad low-pass, bandpass or hi-pass filters with cutoff and resonance, can be assigned to any oscillator
 * Reverb, echo and chorus effects, set globally
 * Stereo pan or mono operation 
 * An additive partial synthesizer with an analysis front end to play back long strings of breakpoint-based sine waves
 * Oscillators can be specified by frequency in floating point or midi note 
 * Each oscillator has 2 envelope generators, which can modify any combination of amplitude, frequency, PWM duty, filter cutoff, or pan over time
 * Each oscillator can also act as an modulator to modify any combination of parameters of another oscillator, for example, a bass drum can be indicated via a half phase sine wave at 0.25Hz modulating the frequency of another sine wave. 
 * Control of overall gain and 3-band EQ
 * Built in patches for PCM, DX7, piano, Juno and partials
 * A front end for Juno-6 patches and conversion setup commands 
 * Built-in clock and pattern sequencer
 * Can use multi-core (including microcontrollers) for rendering if available

The FM synth provides a Python library, [`fm.py`](https://github.com/shorepine/amy/blob/main/fm.py) that can convert any DX7 patch into AMY setup commands, and also a pure-Python implementation of the AMY FM synthesizer in [`dx7_simulator.py`](https://github.com/shorepine/amy/blob/main/experiments/dx7_simulator.py).

The partial tone synthesizer provides [`partials.py`](https://github.com/shorepine/amy/blob/main/partials.py), where you can model the partials of any arbitrary audio into AMY setup commands for live partial playback of hundreds of oscillators. 

The Juno-6 emulation is in [`juno.py`](https://github.com/shorepine/amy/blob/main/juno.py) and can read in Juno-6 SYSEX patches and convert them into AMY commands and generate patches.

[The piano voice and the code to generate the partials are described here](https://shorepine.github.io/amy/piano.html).


## Using AMY in Arduino

Copy this repository to your `Arduino/libraries` folder as `Arduino/libraries/amy`, and `#include <AMY-Arduino.h>`. There are examples for the Pi Pico, ESP32 (and variants), and Teensy (works on 4.X and 3.6) Use the File->Examples->AMY Synthesizer menu to find them. 

The examples rely on the following board packages and libraries:
 * RP2040 / Pi Pico: [`arduino-pico`](https://arduino-pico.readthedocs.io/en/latest/install.html#installing-via-arduino-boards-manager)
 * Teensy: [`teensyduino`](https://www.pjrc.com/teensy/td_download.html)
 * ESP32/ESP32-S3/etc: [`arduino-esp32`](https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/installing.html) - use a 2.0.14+ version when installing
 * The USB MIDI example requires the [MIDI Library](https://www.arduino.cc/reference/en/libraries/midi-library/)

You can use both cores of supported chips (RP2040 or ESP32) for more oscillators and voices. We provide Arduino examples for the Arduino ESP32 in multicore, and a `pico-sdk` example for the RP2040 that renders in multicore. If you really want to push the chips to the limit, we recommend using native C code using the `pico-sdk`  or `ESP-IDF`. 

We have a simple [dual core ESP-IDF example available](https://github.com/shorepine/amy_dual_core_esp32) or you can see [Alles](https://github.com/shorepine/alles).

## Using AMY in Python on any platform

You can `import amy` in Python and have it render either out to your speakers or to a buffer of samples you can process on your own. To install the `libamy` library, run `cd src; pip install .`. You can also run `make test` to install the library and run a series of tests.

## Using AMY on the web

We provide an `emscripten` port of AMY that runs in javascript. [See the AMY web demo](https://shorepine.github.io/amy/). To build for the web, use `make docs/amy.js`. It will generate `amy.js` in `docs/`.  You can also do `make docs/amy-audioin.js` to build a version of AMY for the web with audio input support -- but be warned this will ask your users for microphone access.

## Using AMY in any other software

To use AMY in your own software, simply copy the .c and .h files in `src` to your program and compile them. No other libraries should be required to synthesize audio in AMY. You'll want to make sure the configuration in `amy_config.h` is set up for your application / hardware. 

To run a simple C example on many platforms:

```
make
./amy-example # you should hear tones out your default speaker, use ./amy-example -h for options
```

# Using AMY

> This section introduces AMY starting from the primitive oscillators.  If your interest is mainly in using the preset patches to emulate a full synthesizer, you might skip to [Voices and patches](#voices_and_patches) section.

AMY can be controlled using its wire protocol or by fillng its data structures directly. It's up to what's easier for you and your application. 

In Python, rendering to a buffer of samples, using the high level API:

```python
>>> import amy
>>> m = amy.message(voices='0', load_patch=130, note=50, vel=1)
>>> print(m) # Show the wire protocol message
v0n50l1K130r0Z
>>> amy.send_raw(m)
>>> # This plays immediately on Tulip, but if you're running Amy in regular Python, you can get the waveform from render:
>>> audio = amy.render(5.0)
```

You can also start a thread playing live audio:

```python
>>> import amy
>>> amy.live() # can optinally pass in playback and capture audio device IDs, amy.live(2, 1) 
>>> amy.send(voices='0', load_patch=130, note=50, vel=1)
>>> amy.stop()
```


In C, using the high level structures directly;
  
```c
#include "amy.h"
void bleep() {
    struct event e = amy_default_event();
    int32_t start = amy_sysclock();   // Right now..
    e.time = start;
    e.osc = 0;
    e.wave = SINE;
    e.freq_coefs[COEF_CONST] = 220;
    e.velocity = 1;                   // start a 220 Hz sine.
    amy_add_event(e);
    e.time = start + 150;             // in 150 ms..
    e.freq_coefs[COEF_CONST] = 440;   // change to 440 Hz.
    amy_add_event(e);
    e.time = sysclock + 300;          // in  300 ms..
    e.velocity = 0;                   // note off.
    amy_add_event(e);
}

void main() {
    amy_start(/* cores= */ 1, /* reverb= */ 0, /* chorus= */ 0,  /* echo */ 1); // initializes amy 
    amy_live_start(1); // render live audio
    bleep();
}
```

Or in C, sending the wire protocol directly:

```c
#include "amy.h"

void main() {
    amy_start(/* cores= */ 1, /* reverb= */ 0, /* chorus= */ 0, /* echo */ 1);
    amy_live_start(1);
    amy_play_message("v0n50l1K130r0Z");
}
```

If you want to receive buffers of samples, or have more control over the rendering pipeline to support multi-core, instead of using `amy_live_start()`:

```c
#include "amy.h"
...
amy_start(/* cores= */ 2, /* reverb= */ 1, /* chorus= */ 1, /* echo */ 1);
...
... {
    // For each sample block:
    amy_prepare_buffer(); // prepare to render this block
    amy_render(0, OSCS/2, 0); // render oscillators 0 - OSCS/2 on core 0
    // on the other core... 
    amy_render(OSCS/2, OSCS, 1); // render oscillators OSCS/2-OSCS on core 1
    // when they are both done..
    int16_t * samples = amy_fill_buffer();
    // do what you want with samples
... }

```

On storage constrained devices, you may want to limit the amount of PCM samples we ship with AMY. To do this, include a smaller set after including `amy.h`, like:

```c
#include "amy.h"
#include "pcm_tiny.h" 
// or, #include "pcm_small.h"
```

# Wire protocol

AMY's wire protocol is a series of numbers delimited by ascii characters that define all possible parameters of an oscillator. This is a design decision intended to make using AMY from any sort of environment as easy as possible, with no data structure or parsing overhead on the client. It's also readable and compact, far more expressive than MIDI and can be sent over network links, UARTs, or as arguments to functions or commands. We've used AMY over multicast UDP, over Javascript, in Max/MSP, in Python, C, Micropython and many more! 

AMY accepts commands in ASCII, like so:

```
v0w4f440.0l1.0Z
```
This example controls osc 0 (`v0`), sets its waveform to triangle (`w4`), sets its frequency to 4400 Hz (`f440.0`), and velocity (i.e. amplitude) to 1 (`l1.0`).  The final `Z` is a terminator indicating the message is complete.

Here's the full list:

| Code   | Python   | Type-range  | Notes                                 |
| ------ | -------- | ----------  | ------------------------------------- |
| `a`    | `amp`    | float[,float...]  | Control the amplitude of a note; a set of ControlCoefficients. Default is 0,0,1,1  (i.e. the amplitude comes from the note velocity multiplied by Envelope Generator 0.) |
| `A`    | `bp0`    | string      | Envelope Generator 0's comma-separated breakpoint pairs of time(ms) and level, e.g. `100,0.5,50,0.25,200,0`. The last pair triggers on note off (release) |
| `b`    | `feedback` | float 0-1 | Use for the ALGO synthesis type in FM or for karplus-strong, or to indicate PCM looping (0 off, >0, on) |
| `B`    | `bp1`    | string      | Breakpoints for Envelope Generator 1. See bp0 |
| `c`    | `chained_osc` |  uint 0 to OSCS-1 | Chained oscillator.  Note/velocity events to this oscillator will propagate to chained oscillators.  VCF is run only for first osc in chain, but applies to all oscs in chain. |
| `d`    | `duty`   |  float[,float...] | Duty cycle for pulse wave, ControlCoefficients, defaults to 0.5 |
| `D`    | `debug`  |  uint, 2-4  | 2 shows queue sample, 3 shows oscillator data, 4 shows modified oscillator. Will interrupt audio! |
| `f`    | `freq`   |  float[,float...]      | Frequency of oscillator, set of ControlCoefficients.  Default is 0,1,0,0,0,0,1 (from `note` pitch plus `pitch_bend`) |
| `F`    | `filter_freq` | float[,float...]  | Center/break frequency for variable filter, set of ControlCoefficients |
| `G`    | `filter_type` | 0-4 | Filter type: 0 = none (default.) 1 = lowpass, 2 = bandpass, 3 = highpass, 4 = double-order lowpass. |
| `H`    | `sequence` | int,int,int | Tick offset, period, tag for sequencing | 
| `h`    | `reverb` | float[,float,float,float] | Reverb parameters -- level, liveness, damping, xover: Level is for output mix; liveness controls decay time, 1 = longest, default 0.85; damping is extra decay of high frequencies, default 0.5; xover is damping crossover frequency, default 3000 Hz. |
| `I`    | `ratio`  | float | For ALGO types, ratio of modulator frequency to  base note frequency / For the PARTIALS base note, ratio controls the speed of the playback |
| `j`    | `tempo`  | float | The tempo (BPM, quarter notes) of the sequencer. Defaults to 108.0. |
| `k`    | `chorus` | float[,float,float,float] | Chorus parameters -- level, delay, freq, depth: Level is for output mix (0 to turn off); delay is max in samples (320); freq is LFO rate in Hz (0.5); depth is proportion of max delay (0.5). |
| `K`    | `load_patch` | uint 0-X | Apply a saved patch (e.g. DX7 or Juno) to a specified voice (or starting at the selected oscillator). |
| `l`    | `vel` | float 0-1+ | Velocity: > 0 to trigger note on, 0 to trigger note off |
| `L`    | `mod_source` | 0 to OSCS-1 | Which oscillator is used as an modulation/LFO source for this oscillator. Source oscillator will be silent. |
| `m`    | `portamento` | uint | Time constant (in ms) for pitch changes when note is changed without intervening note-off.  default 0 (immediate), 100 is good. |
| `M`    | `echo` | float[,int,int,float,float] | Echo parameters --  level, delay_ms, max_delay_ms, feedback, filter_coef (-1 is HPF, 0 is flat, +1 is LPF). |
| `n`    | `note` | float, but typ. uint 0-127 | Midi note, sets frequency.  Fractional Midi notes are allowed |
| `N`    | `latency_ms` | uint | Sets latency in ms. default 0 (see LATENCY) |
| `o`    | `algorithm` | uint 1-32 | DX7 FM algorithm to use for ALGO type |
| `O`    | `algo_source` | string | Which oscillators to use for the FM algorithm. list of six (starting with op 6), use empty for not used, e.g 0,1,2 or 0,1,2,,, |
| `p`    | `patch` | int | Which predefined PCM or Partials patch to use, or number of partials if < 0. (Juno/DX7 patches are different - see `load_patch`). |
| `P`    | `phase` | float 0-1 | Where in the oscillator's cycle to begin the waveform (also works on the PCM buffer). default 0 |
| `Q`    | `pan`   | float[,float...] | Panning index ControlCoefficients (for stereo output), 0.0=left, 1.0=right. default 0.5. |
| `r`    | `voices` | int[,int] | Comma separated list of voices to send message to, or load patch into. |
| `R`    | `resonance` | float | Q factor of variable filter, 0.5-16.0. default 0.7 |
| `s`    | `pitch_bend` | float | Sets the global pitch bend, by default modifying all note frequencies by (fractional) octaves up or down |
| `S`    | `reset`  | uint | Resets given oscillator. set to RESET_ALL_OSCS to reset all oscillators, gain and EQ. RESET_TIMEBASE resets the clock (immediately, ignoring `time`). RESET_AMY restarts AMY. RESET_SEQUENCER clears the sequencer.|
| `t`    | `time` | uint | Request playback time relative to some fixed start point on your host, in ms. Allows precise future scheduling. |
| `T`    | `eg0_type` | uint 0-3 | Type for Envelope Generator 0 - 0: Normal (RC-like) / 1: Linear / 2: DX7-style / 3: True exponential. |
| `u`    | `store_patch` | number,string | Store up to 32 patches in RAM with ID number (1024-1055) and AMY message after a comma. Must be sent alone |
| `v`    | `osc` | uint 0 to OSCS-1 | Which oscillator to control |
| `V`    | `volume` | float 0-10 | Volume knob for entire synth, default 1.0 |
| `w`    | `wave` | uint 0-16 | Waveform: [0=SINE, PULSE, SAW_DOWN, SAW_UP, TRIANGLE, NOISE, KS, PCM, ALGO, PARTIAL, PARTIALS, BYO_PARTIALS, INTERP_PARTIALS, AUDIO_IN0, AUDIO_IN1, CUSTOM, OFF]. default: 0/SINE |
| `x`    | `eq` | float,float,float | Equalization in dB low (~800Hz) / med (~2500Hz) / high (~7500Gz) -15 to 15. 0 is off. default 0. |
| `X`    | `eg1_type` | uint 0-3 | Type for Envelope Generator 1 - 0: Normal (RC-like) / 1: Linear / 2: DX7-style / 3: True exponential. |
| `z`    | `load_sample` | uint x 6 | Signal to start loading sample. patch, length(samples), samplerate, midinote, loopstart, loopend. All subsequent messages are base64 encoded WAVE-style frames of audio until `length` is reached. Set `patch` and `length=0` to unload a sample from RAM. |



# Synthesizer details

We'll use Python for showing examples of AMY.  Maybe you're running under [Tulip](https://github.com/shorepine/tulipcc), in which case AMY is already loaded, but if you're running under standard Python, make sure you've installed `libamy` and are running a live AMY first by running `make test` and then:
```bash
python
>>> import amy
>>> amy.live()
```

## AMY's sequencer and timestamps

AMY is meant to either receive messages in real time or scheduled events in the future. It can be used as a sequencer where you can schedule notes to play in the future or on a divider of the clock.

The scheduled events are very helpful in cases where you can't rely on an accurate clock from the client, or don't have one. The clock used internally by AMY is based on the audio samples being generated out the speakers, which should run at an accurate 44,100 times a second.  This lets you do things like schedule fast moving parameter changes over short windows of time. 

For example, to play two notes, one a second after the first, you could do:

```python
amy.send(osc=0, note=50, vel=1)
time.sleep(1)
amy.send(osc=0, note=52, vel=1)
```

But you'd be at the mercy of Python's internal timing, or your OS. A more precise way is to send the messages at the same time, but to indicate the intended time of the playback:

```python
start = amy.millis()  # arbitrary start timestamp
amy.send(osc=0, note=50, vel=1, time=start)
amy.send(osc=0, note=52, vel=1, time=start + 1000)
```

Both `amy.send()`s will return immediately, but you'll hear the second note play precisely a second after the first. AMY uses this internal clock to schedule step changes in breakpoints as well. 


### The sequencer

On supported platforms (right now any unix device with pthreads, and the ESP32 or related chip), AMY starts a sequencer that works on `ticks` from startup. You can reset the `ticks` to 0 with an `amy.send(reset=amy.RESET_TIMEBASE)`. Note this will happen immediately, ignoring any `time` or `sequence`.

Ticks run at 48 PPQ at the set tempo. The tempo defaults to 108 BPM. This means there are 108 quarter notes a minute, and `48 * 108 = 5184` ticks a minute, 86 ticks a second. The tempo can be changed with `amy.send(tempo=120)`.

You can schedule an event to happen at a precise tick with `amy.send(... ,sequence="tick,period,tag")`. `tick` can be an absolute or offset tick number. If `period` is ommited or 0, `tick` is assumed to be absolute and once AMY reaches `tick`, the rest of your event will play and the saved event will be removed from memory. If an absolute `tick` is in the past, AMY will ignore it. 

You can schedule repeating events (like a step sequencer or drum machine) with `period`, which is the length of the sequence in ticks. For example a `period` of 48 with `ticks` omitted or 0 will trigger once every quarter note. A `period` of 24 will happen twice every quarter note. A `period` of 96 will happen every two quarter notes. `period` can be any whole number to allow for complex rhythms. 

For pattern sequencers like drum machines, you will also want to use `tick` alongisde `period`. If both are given and nonzero, `tick` is assumed to be an offset on the `period`. For example, for a 16-step drum machine pattern running on eighth notes (PPQ/2), you would use a `period` of `16 * 24 = 384`. The first slot of the drum machine would have a `tick` of 0, the 2nd would have a `tick` offset of 24, and so on. 

`tag` should be given, and will be `0` if not. You should set `tag` to a random or incrementing number in your code that you can refer to later. `tag` allows you to replace or delete the event once scheduled. 

If you are including AMY in a program, you can set the hook `void (*amy_external_sequencer_hook)(uint32_t)` to any function. This will be called at every tick with the current tick number as an argument. 

Sequencer examples:

```python
amy.send(osc=2, vel=1, wave=amy.PCM, patch=2, sequence="1000,,3") # play a PCM drum at absolute tick 1000 

amy.send(osc=0, vel=1, wave=amy.PCM, patch=0, sequence=",24,1") # play a PCM drum every eighth note.
amy.send(osc=1, vel=1, wave=amy.PCM, patch=1, sequence=",48,2") # play a PCM drum every quarter note.
amy.send(sequence=",,1") # remove the eighth note sequence
amy.send(osc=1, vel=1, wave=amy.PCM, patch=1, note=70, sequence=",48,2") # change the quarter note event

amy.send(reset=amy.RESET_SEQUENCER) # clears the sequence

amy.send(osc=0, vel=1, wave=amy.PCM, patch=0, sequence="0,384,1") # first slot of a 16 1/8th note drum machine
amy.send(osc=1, vel=1, wave=amy.PCM, patch=1, sequence="216,384,2") # ninth slot of a 16 1/8th note drum machine


```

## Examples

`amy.drums()` should play a test pattern.

Try to set the volume of the synth with `amy.send(volume=2)` -- that can be up to 10 or so.  The default is 1. 

`amy.reset()` resets all oscillators to default. You can also do `amy.reset(osc=5)` to do just one oscillator.

Let's set a simple sine wave first

```python
amy.send(osc=0, wave=amy.SINE, freq=220, vel=1)
```

We are simply telling oscillator 0 to be a sine wave at 220Hz and amplitude (specified as a note-on velocity) of 1.  You can also try `amy.PULSE`, or `amy.SAW_DOWN`, etc.

To turn off the note, send a note off (velocity zero):

```python
amy.send(osc=0, vel=0)  # Note off.
```

You can also make oscillators louder with `vel` larger than 1.  By default, the total amplitude comes from multiplying together the oscillator amplitude (i.e., the natural level of the oscillator, which is 1 by default) and the velocity (the particular level of this note event) -- however, this can be changed by changing the default values of the `amp` **ControlCoefficients** (see below).

You can also use `note` (MIDI note value) instead of `freq` to control the oscillator frequency for each note event:

```python
amy.reset()
amy.send(osc=0, wave=amy.SINE, note=57, vel=1)
```

This won't work as intended without the `amy.reset()`, because once you've set the oscillator to a non-default frequency with `freq=220`, it will act as an offset to the frequency specified by `note`.  (See **ControlCoefficients** below to see how to control this behavior).

Now let's make a lot of sine waves! 

```python
import time
amy.reset()
for i in range(16):
    amy.send(osc=i, wave=amy.SINE, freq=110+(i*80), vel=((16-i)/32.0))
    time.sleep(0.5) # Sleep for 0.5 seconds
```

Neat! You can see how simple / powerful it is to have control over lots of oscillators. You have up to 64 (or more, depending on your platform). Let's make it more interesting. A classic analog tone is the filtered saw wave. Let's make one.

```python
amy.send(osc=0, wave=amy.SAW_DOWN, filter_freq=400, resonance=5, filter_type=amy.FILTER_LPF)
amy.send(osc=0, vel=1, note=40)
```

You want to be able to stop the note too by sending a note off:
```python
amy.send(osc=0, vel=0)
```

Sounds nice. But we want that filter freq to go down over time, to make that classic filter sweep tone. Let's use an Envelope Generator! An Envelope Generator (EG) creates a smooth time envelope based on a breakpoint set, which is a simple list of (time-delta, target-value) pairs - you can have up to 8 of these per EG, and 2 different EGs to control different things. They're just like ADSRs, but more powerful. You can use an EG to control amplitude, oscillator frequency, filter cutoff frequency, PWM duty cycle, or stereo pan. The EG gets triggered when the note begins. So let's make an EG that turns the filter frequency down from its start at 3200 Hz to 400 Hz over 1000 milliseconds. And when the note goes off, it tapers the frequency to 50 Hz over 200 milliseconds.

```python
amy.send(osc=0, wave=amy.SAW_DOWN, resonance=5, filter_type=amy.FILTER_LPF)
amy.send(osc=0, filter_freq='50,0,0,0,1', bp1='0,6.0,1000,3.0,200,0')
amy.send(osc=0, vel=1, note=40)
```

There are two things to note here:  Firstly, the envelope is defined by the set of breakpoints in `bp1` (defining the second EG; the first is controlled by `bp0`).  The `bp` strings alternate time intervals in milliseconds with target values.  So `0,6.0,1000,3.0,200,0` means to move to 6.0 after 0 ms (i.e., the initial value is 6), then to decay to 3.0 over the next 1000 ms (1 second).  The final pair is always taken as the "release", and does not begin until the note-off event is received.  In this case, the EG decays to 0 in the 200 ms after the note-off.


Secondly, EG1 is coupled to the filter frequency with `filter_freq='50,0,0,0,1'`.  `filter_freq` is an example of a set of **ControlCoefficients** where the control value is calculated on-the-fly by combining a set of inputs scaled by the coefficients.  This is explained fully below, but for now the first coefficient (here 50) is taken as a constant, and the 5th coefficient (here 1) is applied to the output of EG1.  To get good "musical" behavior, the filter frequency is controlled using a "unit per octave" rule.  So if the envelope is zero, the filter is at its base frequency of 50 Hz.  But the envelope starts at 6.0, which, after scaling by the control coefficient of 1, drives the filter frequency 6 octaves higher, or 2^6 = 64x the frequency -- 3200 Hz.  As the envelope decays to 3.0 over the first 1000 ms, the filter moves to 2^3 = 8x the default frequency, giving 400 Hz.  It's only during the final release of 200 ms that it falls back to 0, giving a final filter frequency of (2^0 = 1x) 50 Hz.

### ControlCoefficients

The full set of parameters accepting **ControlCoefficients** is `amp`, `freq`, `filter_freq`, `duty`, and `pan`.  ControlCoefficients are a list of up to 7 floats that are multiplied by a range of control signals, then summed up to give the final result (in this case, the filter frequency).  The control signals are:
 * `const`: A constant value of 1 - so the first number in the control coefficient list is the default value if all the others are zero.
 * `note`: The frequency corresponding to the `note` parameter to the note-on event (converted to unit-per-octave relative to middle C).
 * `vel`: The velocity, from the note-on event.
 * `eg0`: The output of Envelope Generator 0.
 * `eg1`: The output of Envelope Generator 1.
 * `mod`: The output of the modulating oscillator, specified by the `mod_source` parameter.
 * `bend`: The current pitch bend value (from `amy.send(pitch_bend=0.5)` etc.).

The set `50,0,0,0,1` means that we have a base frequency of 50 Hz, we ignore the note frequency and velocity and EG0, but we also add the output of EG1. Any coefficients that you do not specify, for instance by providing fewer than 7 values, are not modified.  You can also use empty strings to skip positional values, so `filter_freq=',,,,1'` couples EG1 to the filter frequency without changing any of the other coefficients.  (Note that when we passed `freq=220` in the first example, that was interpreted setting the `const` coefficient to 220, but leaving all the remaining coefficients untouched.)

Because entering lists of commas is error prone, you can also specify control coefficients as Python dicts consisting of value with keys from the list above, i.e. `filter_freq={'const': 50, 'eg1': 1}` is equivalent to `filter_freq='50,,,,1'`.

You can use the same EG to control several things at once.  For example, we could include `freq=',,,,0.333'`, which says to modify the note frequency from the same EG1 as is controlling the filter frequency, but scaled down by 1/3rd so the initial decay is over 1 octave, not 3.  Give it a go!

The note frequency is scaled relative to a zero-point of middle C (MIDI note 60, 261.63 Hz), so to make the oscillator faithfully track the `note` parameter to the note-on event, you would use something like `freq='261.63,1'`.  Setting it to `freq='523.26,1'` would make the oscillator always be one octave higher than the `note` MIDI number.  Setting `freq='261.3,0.5'` would make the oscillator track the `note` parameter at half an octave per unit, so while `note=60` would still give middle C, `note=72` (C5) would make the oscillator run at F#4, and `note=84` (C6) would be required to get C5 from the oscillator.

The default set of ControlCoefficients for `freq` is `'261.63,1,0,0,0,0,1'`, i.e. a base of middle C, tracking the MIDI note, plus pitch bend (at unit-per-octave).  Because 261.63 is such an important value, as a special case, setting the first `freq` value to zero is magically rewritten as 261.63, so `freq='0,1,0,0,0,0,1'` also yields the default behavior.  `amp` also has a set of defaults: `amp='0,0,1,1,0,0,0'`, i.e. tracking note-on velocity plus modulation by EG0 (which just tracks the note-on status if it has not been set up).  `amp` is a little special because the individual components are *multiplied* together, instead of added together, for any control inputs with nonzero coefficients.  Finally, an offset of 1.0 is added to the coefficient-scaled LFO modulator and pitch bend inputs before multiplying them into the amplitude, to allow small variations around unity e.g. for tremolo.  These defaults are set up in [`src/amy.c:reset_osc()`](https://github.com/shorepine/amy/blob/b1ed189b01e6b908bc19f18a4e0a85761d739807/src/amy.c#L551).

We also have LFOs, which are implemented as one oscillator modulating another (instead of sending its waveform to the output). You set up the low-frequency oscillator, then have it control a parameter of another audible oscillator. Let's make the classic 8-bit duty cycle pulse wave modulation, a favorite:

```python
amy.reset()  # Clear the state.
amy.send(osc=1, wave=amy.SINE, freq=0.5, amp=1)   # We set the amp but not the vel, so it doesn't sound.
amy.send(osc=0, wave=amy.PULSE, duty={'const': 0.5, 'mod': 0.4}, mod_source=1)
amy.send(osc=0, note=60, vel=0.5)
```

You see we first set up the modulation oscillator (a sine wave at 0.5Hz, with amplitude of 1).  We do *not* send it a velocity, because that would make it start sending a 0.5 Hz sinewave to the audio output; we want its output only to be used internally.  Then we set up the oscillator to be modulated, a pulse wave with a modulation source of oscillator 1 and the duty **ControlCoefficients** set to have a constant value of 0.5 plus 0.4 times the modulating input (i.e., the depth of the pulse width modulation, where 0.4 modulates between 0.1 and 0.9, almost the maximum depth).  The initial duty cycle will start at 0.5 and be offset by the state of oscillator 1 every tick, to make that classic thick saw line from the C64 et al. The modulation will re-trigger every note on. Just like with envelope generators, the modulation oscillator has a 'slot' in the ControlCoefficients - the 6th coefficient, `mod` - so it can modulate PWM duty cycle, amplitude, frequency, filter frequency, or pan! And if you want to modulate more than one thing, like frequency and duty, just specify multiple ControlCoefficients:

```python
amy.send(osc=1, wave=amy.TRIANGLE, freq=5, amp=1)
amy.send(osc=0, wave=amy.PULSE, duty={'const': 0.5, 'mod': 0.25}, freq={'mod': 0.5}, mod_source=1)
amy.send(osc=0, note=60, vel=0.5)
```

`amy.py` has some helpful presets, if you want to use them, or add to them. To make that filter bass, just do `amy.preset(1, osc=0)` and then `amy.send(osc=0, vel=1, note=40)` to hear it. Here's another one:

```python
amy.preset(0, osc=2) # will set a simple sine wave tone on oscillator 2
amy.send(osc=2, note=50, vel=1.5) # will play the note at velocity 1.5
amy.send(osc=2, vel=0) # will send a "note off" -- you'll hear the note release
amy.send(osc=2, freq=220.5, vel=1.5) # same but specifying the frequency
amy.reset()
```

## Core oscillators

We support bandlimited saw, pulse/square and triangle waves, alongside sine and noise. Use the wave parameter: 0=SINE, PULSE, SAW_DOWN, SAW_UP, TRIANGLE, NOISE. Each oscillator can have a frequency (or set by midi note), amplitude and phase (set in 0-1.). You can also set `duty` for the pulse type. We also have a karplus-strong type (KS=6). 

Oscillators will not become audible until a `velocity` over 0 is set for the oscillator. This is a "note on" and will trigger any modulators or envelope generators set for that oscillator. Setting `velocity` to 0 sets a note off, which will stop modulators and also finish the envelopes at their release pair. `velocity` also internally sets `amplitude`, but you can manually set `amplitude` after `velocity` starts a note on.

## LFOs & modulators

Any oscillator can modulate any other oscillator. For example, a LFO can be specified by setting oscillator 0 to 0.25Hz sine, with oscillator 1 being a 440Hz sine. Using the 6th parameter of **ControlCoefficient** lists, you can have oscillator 0 modulate frequency, amplitude, filter frequency, or pan of oscillator 1. You can also add targets together, for example amplitude+frequency. Set the `mod_target` and `mod_source` on the audible oscillator (in this case, oscillator 1.) The source mod oscillator will not be audible once it is referred to as a `mod_source` by another oscillator. The amplitude of the modulating oscillator indicates how strong the modulation is (aka "LFO depth.")

## Filters

We support lowpass, bandpass and hipass filters in AMY. You can set `resonance` and `filter_freq` per oscillator. 

## EQ & Volume

You can set a synth-wide volume (in practice, 0-10), or set the EQ of the entire synths's output. 

## Envelope Generators

AMY allows you to set 2 Envelope Generators (EGs) per oscillator. You can see these as ADSR / envelopes (and they can perform the same task), but they are slightly more capable. Breakpoints are defined as pairs of time deltas (specified in milliseconds) and target value. You can specify up to 8 pairs, but the last pair you specify will always be seen as the "release" pair, which doesn't trigger until note off. All preceding pairs have time deltas relative to the previous segment, so `100,1,100,0,0,0` goes up to 1 over 100 ms, then back down to zero over the next 100ms. The last "release" pair counts from ms from the note-off.

An EG can control amplitude, frequency, filter frequency, duty or pan of an oscillator via the 4th (EG0) and 5th (EG1) entries in the corresponding ControlCoefficients.

For example, to define a common ADSR curve where a sound sweeps up in volume from note on over 50ms, then has a 100ms decay stage to 50% of the volume, then is held until note off at which point it takes 250ms to trail off to 0, you'd set time to be 50ms and target to be 1.0, then 100ms with target .5, then a 250ms release with ratio 0. By default, amplitude is set up to be controlled by EG0. At every synthesizer tick, the given amplitude (default of 1.0) will be multiplied by the EG0 value. In AMY wire parlance, this would look like `v0f220w0A50,1.0,100,0.5,250,0` to specify a sine wave at 220Hz with this envelope. 

When using `amy.py`, use the string form of the breakpoint: `amy.send(osc=0, bp0='50,1.0,100,0.5,250,0')`.

Every note on (specified by setting `vel` / `l` to anything > 0) will trigger this envelope, and setting velocity to 0 will trigger the note off / release section.

You can set a completely separate envelope using the second envelope generator, for example, to change pitch and amplitude at different rates.

As with ControlCoefficients, missing values in the comma-separated parameter strings mean to leave the existing value unchanged.  However, unlike ControlCoefficients, it's important to explicitly indicate every value you want to leave unchanged, since the number of parameters provided determines the number of breakpoints in the set.  So in the following sequence:
```
amy.send(osc=0, bp0='0,1,1000,0.1,200,0')
amy.send(osc=0, bp0=',,,0.9,,')
```
.. we end up with the same effect as `bp0='0,1,1000,0.9,200,0`.  However, if we do:
```
amy.send(osc=0, bp0='0,1,1000,0.1,200,0')
amy.send(osc=0, bp0=',,,0.9')  # No trailing commas.
```
.. we effectively end up with `bp0='0,1,1000,0.9`, i.e. the 4 elements in the second `bp0` string change the first breakpoint set to have only 2 breakpoints, meaning a constant amplitude during note-on, then a final slow release to 0.9 -- not at all like the first form, and likely not what we wanted.

## Audio input and effects

By setting `wave` to `AUDIO_IN0` or `AUDIO_IN1`, you can have either channel of a stereo input act as an AMY oscillator. You can use this oscillator like you would any other in AMY, apply global effects to it, add filters, change amplitude, etc. 

```
amy.send(osc=0, wave=amy.AUDIO_IN0, vel=1)
amy.echo(1, 250, 250, 0.5, 0.5)
```

If you are building your own audio system around AMY you will want to fill in the buffer `amy_in_block` before rendering. Our included `miniaudio`-based system does this for you. See [`amychip`](https://github.com/shorepine/amychip) for a demo of this in hardware. 

## FM & ALGO type

Try default DX7 patches, from 128 to 256:

```python
amy.send(voices='0', load_patch=128)
amy.send(voices='0', note=50,vel=1)
```

The `load_patch` lets you set which preset is used (0 to 127 are the Juno 106 analog synth presets, and 128 to 255 are the DX7 FM presets).  But let's make the classic FM bell tone ourselves, without a patch. We'll just be using two operators (two sine waves), one modulating the other.

![DX7 Algorithms](https://raw.githubusercontent.com/shorepine/alles/main/pics/dx7_algorithms.jpg)

When building your own algorithm sets, assign a separate oscillator as wave=`ALGO`, but the source oscillators as `SINE`. The algorithm #s are borrowed from the DX7. You don't have to use all 6 operators. Note that the `algo_source` parameter counts backwards from operator 6. When building operators, they can have their frequencies specified directly with `freq` or as a ratio of the root `ALGO` oscillator via `ratio`.


```python
amy.reset()
amy.send(osc=2, wave=amy.SINE, ratio=1, amp={'const': 1, 'vel': 0, 'eg0': 0})
amy.send(osc=1, wave=amy.SINE, ratio=0.2, amp={'const': 1, 'vel': 0, 'eg0': 1}, bp0='0,1,1000,0,0,0')
amy.send(osc=0, wave=amy.ALGO, algorithm=1, algo_source=',,,,2,1')
```

Let's unpack that last line: we're setting up a ALGO "oscillator" that controls up to 6 other oscillators. We only need two, so we set the `algo_source` to mostly not used and have oscillator 2 modulate oscillator 1. You can have the operators work with each other in all sorts of crazy ways. For this simple example, we just use the DX7 algorithm #1. And we'll use only operators 2 and 1. Therefore our `algo_source` lists the oscillators involved, counting backwards from 6. We're saying only have operator 2 (osc 2 in this case) and operator 1 (osc 1).  From the picture, we see DX7 algorithm 1 has operator 2 feeding operator 1, so we have osc 2 providing the frequency-modulation input to osc 1.

What's going on with `ratio`? And `amp`? Ratio, for FM synthesis operators, means the ratio of the frequency for that operator relative to the base note. So oscillator 1 will be played at 20% of the base note frequency, and oscillator 2 will take the frequency of the base note. In FM synthesis, the `amp` of a modulator input is  called "beta", which describes the strength of the modulation.  Here, osc 2 is providing the modulation with a constant beta of 1, which will result in a range of sinusoids with frequencies around the carrier at multiples of the modulator.  We set osc 2's amp ControlCoefficients for velocity and envelope generator 0 to 0 because they default to 1, but we don't want them for this example (FM sines don't receive the parent note's velocity, so we need to disable its influence).  Osc 1 has `bp0` decaying its amplitude to 0 over 1000 ms, but because beta is fixed there's no other change to the sound over that time.

Ok, we've set up the oscillators. Now, let's hear it!

```python
amy.send(osc=0, note=60, vel=1)
```

You should hear a bell-like tone. Nice. (This example is also implemented using the C API in [`src/examples.c:example_fm()`](https://github.com/shorepine/amy/blob/b1ed189b01e6b908bc19f18a4e0a85761d739807/src/examples.c#L281).)

FM gets much more exciting when we vary beta, which just means varying the amplitide envelope of the modulator.  The spectral effects of the frequency modulation depend on beta in a rich, nonlinear way, leading to the glistening FM sounds.  Let's try fading in the modulator over 5 seconds:


```python
amy.reset()
amy.send(osc=2, wave=amy.SINE, ratio=0.2, amp={'const': 1, 'vel': 0, 'eg0': 2}, bp0='0,0,5000,1,0,0')  # Op 2, modulator
amy.send(osc=1, wave=amy.SINE, ratio=1, amp={'const': 1, 'vel': 0, 'eg0': 0})  # Op 1, carrier
amy.send(osc=0, wave=amy.ALGO, algorithm=1, algo_source=',,,,2,1')
```

Just a refresher on envelope generators; here we are saying to set the beta parameter (amplitude of the modulating tone) to 2x envelope generator 0's output, which starts at 0 at time 0 (actually, this is the default), then grows to 1.0 at time 5000ms - so beta grows to 2.0. At the release of the note, beta immediately drops back to 0. We can play it with:

```python
amy.send(osc=0, note=60, vel=1)
```

and stop it with

```python
amy.send(osc=0, vel=0)
```


## Partials

Additive synthesis is simply adding together oscillators to make more complex tones. You can modulate the breakpoints of these oscillators over time, for example, changing their pitch or time without artifacts, as the synthesis is simply playing sine waves back at certain amplitudes and frequencies (and phases). It's well suited to certain types of instruments. 

![Partials](https://raw.githubusercontent.com/shorepine/alles/main/pics/partials.png)

We have analyzed the partials of a group of instruments and stored them as presets baked into the synth. Each of these patches are comprised of multiple sine wave oscillators, changing over time. The `PARTIALS` type has the presets:

```python
amy.send(osc=0, vel=1, note=50, wave=amy.PARTIALS, patch=5) # a nice organ tone
amy.send(osc=0, vel=1, note=55, wave=amy.PARTIALS, patch=5) # change the frequency
amy.send(osc=0, vel=1, note=50, wave=amy.PARTIALS, patch=6, ratio=0.2) # ratio slows down the partial playback
```

The presets are just the start of what you can do with partials in AMY. You can analyze any piece of audio and decompose it into sine waves and play it back on the synthesizer in real time. It requires a little setup on the client end, here on macOS:

```bash
brew install python3 swig ffmpeg
python3 -m pip install pydub numpy --user
tar xvf loris-1.8.tar
cd loris-1.8
CPPFLAGS=`python3-config --includes` PYTHON=`which python3` ./configure --with-python --prefix=`python3-config --prefix`
make
make install
cd ..
```

And then in python (run `python3`):

```python
import partials, amy
(m, s) = partials.sequence('sounds/sleepwalk_original_45s.mp3')  # Any audio file
153 partials and 977 breakpoints, max oscs used at once was 8

amy.live() # Start AMY playing audio
partials.play(s)
```

https://user-images.githubusercontent.com/76612/131150119-6fa69e3c-3244-476b-a209-1bd5760bc979.mp4


You can see, given any audio file, you can hear a sine wave decomposition version of in AMY. This particular sound emitted 109 partials, with a total of 1029 breakpoints among them to play back to the mesh. Of those 109 partials, only 8 are active at once. `partials.sequence()` performs voice stealing to ensure we use as few oscillators as necessary to play back a set. 

There's a lot of parameters you can (and should!) play with in Loris. `partials.sequence`  and `partials.play`takes the following with their defaults:

```python
def sequence(filename, # any audio filename
             max_len_s = 10, # analyze first N seconds
             amp_floor=-30, # only accept partials at this amplitude in dB, lower #s == more partials
             hop_time=0.04, # time between analysis windows, impacts distance between breakpoints
             max_oscs=amy.OSCS, # max AMY oscs to take up
             freq_res = 10, # freq resolution of analyzer, higher # -- less partials & breakpoints 
             freq_drift=20, # max difference in Hz within a single partial
             analysis_window = 100 # analysis window size 
             ) # returns (metadata, sequence)

def play(sequence, # from partials.sequence
         osc_offset=0, # start at this oscillator #
         sustain_ms = -1, # if the instrument should sustain, here's where (in ms)
         sustain_len_ms = 0, # how long to sustain for
         time_ratio = 1, # playback speed -- 0.5 , half speed
         pitch_ratio = 1, # frequency scale, 0.5 , half freq
         amp_ratio = 1, # amplitude scale
         )
```

## Build-your-own Partials

You can also explicitly control partials in "build-your-own partials" mode, accessed via `wave=amy.BYO_PARTIALS`.  This sets up a string of oscs as individual sinusoids, just like `PARTIALS` mode, but it's up to you to control the details of each partial via its parameters, envelopes, etc.  You just have to say how many partials you want with `num_partials`.  You can then individually set up the amplitude `bp0` envelopes of the next `num_partials` oscs for arbitrary control, subject to the limit of 7 breakpoints plus release for each envelope.  For instance, to get an 8-harmonic pluck tone with a 50 ms attack, and harmonic weights and decay times inversely proportional to to the harmonic number:

```python
num_partials = 8
amy.send(osc=0, wave=amy.BYO_PARTIALS, num_partials=num_partials)
for i in range(1, num_partials + 1):
    # Set up each partial as the corresponding harmonic of 261.63
    # with an amplitude of 1/N, 50ms attack, and a decay of 1 sec / N.
    amy.send(osc=i, wave=amy.PARTIAL, freq=261.63 * i,
             bp0='50,%.2f,%d,0,0,0' % ((1.0 / i), 1000 // i))
amy.send(osc=0, note=60, vel=1)
```

You can add a filter (or an envelope etc.) to the sum of all the `PARTIAL` oscs by configuring it on the parent `PARTIALS` osc:

```
amy.send(osc=0, filter=amy.FILTER_HPF, resonance=4, filter_freq={'const': 200, 'eg1': 4}, bp1='0,0,1000,1,0,0')
amy.send(osc=0, note=60, vel=1)
# etc.
```
Note that the default `bp0` amplitude envelope of the `PARTIALS` osc is a gate, so if you want to have a nonzero release on your partials, you'll need to add a slower release to the `PARTIALS` osc to avoid it cutting them off.


## Interpolated partials

Please see our [piano voice documentation](https://shorepine.github.io/amy/piano.html) for more on the `INTERP_PARTIALS` type. 


## PCM

AMY comes with a set of 67 drum-like and instrument PCM samples to use as well, as they are normally hard to render with additive, subtractive or FM synthesis. You can use the type `PCM` and patch numbers 0-66 to explore them. Their native pitch is used if you don't give a frequency or note parameter. You can update the baked-in PCM sample bank using `amy_headers.py`. 


```python
amy.send(osc=0, wave=amy.PCM, vel=1, patch=10) # cowbell
amy.send(osc=0, wave=amy.PCM, vel=1, patch=10, note=70) # higher cowbell! 
```

You can turn on sample looping, helpful for instruments, using `feedback`:

```python
amy.send(wave=amy.PCM,vel=1,patch=21,feedback=0) # clean guitar string, no looping
amy.send(wave=amy.PCM,vel=1,patch=21,feedback=1) # loops forever until note off
amy.send(vel=0) # note off
amy.send(wave=amy.PCM,vel=1,patch=35,feedback=1) # nice violin
```

## Sampler (aka Memory PCM)

You can also load your own samples into AMY at runtime. We support sending PCM data over the wire protocol. Use `load_sample` in `amy.py` as an example:

```python
amy.load_sample("G1.wav", patch=3)
amy.send(osc=0, wave=amy.PCM, patch=3, vel=1) # plays the sample
```

You can use any patch number. If it overlaps with an existing PCM baked in number, it will play the memory sample instead of the baked in sample until you `unload_sample` the patch.

If the WAV file has sampler metadata like loop points or base MIDI note, we use that in AMY. You can set it directly as well using `loopstart`, `loopend`, `midinote` or `length` in the `load_sample` call. To unload a sample:

```python
amy.unload_sample(3) # unloads the RAM for patch 3
```

Under the hood, if AMY receives a `load_sample` message (with patch number and nonzero length), it will then pause all other message parsing until it has received `length` amount of base64 encoded bytes over the wire protocol. Each individual message must be base64 encoded. Since AMY's maximum message length is 255 bytes, there is logic in `load_sample` in `amy.py`  to split the sample data into 188 byte chunks, which generates 252 bytes of base64 text. Please see `amy.load_sample` if you wish to load samples on other platforms.

## <a name="voices_and_patches"></a>Voices and patches (DX7, Piano, Juno-6, custom) support

Up until now, we have been directly controlling the AMY oscillators, which are the fundamental building blocks for sound production.  However, as we've seen, most interesting tones involve multiple oscillators.  AMY provides a second layer of organization, **voices**, to make it easier to configure and use groups of oscillators in coordination.  And you configure a voice by using a **patch**, which is simply a stored list of AMY commands that set up one or more oscillators.

A voice in AMY is a collection of oscillators. You can assign any patch to any voice number, or set up mulitple voices to have the same patch (for example, a polyphonic synth), and AMY will allocate the oscillators it needs under the hood.  (Note that when you use voices, you'll need to include the `voices` arg when addressing oscillators, and AMY will automatically route your command to the relevant oscillator in each voice set -- there's no other way to tell which oscillators are being used by which voices.)

To play a patch, for instance the built-in patches emulating Juno and DX7 synthesizers and a piano, you allocate them to one or more voices, then send note events, or parameter moidifications, to those voices. We ship patches 0-127 for Juno, 128-255 for DX7, and 256 for our [built in piano](https://shorepine.github.io/amy/piano.html). For example, a multitimbral Juno/DX7 synth can be set up like this:

```python
amy.send(voices='0,1,2,3', load_patch=1)     # Juno patch #1 on voice 0-3
amy.send(voices='4,5,6,7', load_patch=129)   # DX7 patch #2 on voices 4-7
amy.send(voices=0, note=60, vel=1)           # Play note 60 on voice 0
amy.send(voices=0, osc=0, filter_freq=8000)  # Open up the filter on the Juno voice (using its bottom oscillator)
```

The code in `amy_headers.py` generates these patches and bakes them into AMY so they're ready for playback on any device. You can add your own patches by storing alternative wire-protocol setup strings in `patches.h`.

You can also create your own patches at runtime and use them for voices with `store_patch='PATCH_NUMBER,AMY_PATCH_STRING'` where `PATCH_NUMBER` is a number in the range 1024-1055. This message must be on its own in the `amy.send()` command, not combined with any other parameters, because AMY will treat the rest of the message as a patch rather than interpreting the remaining arguments as ususal.

So you can do:
```
>>> import amy; amy.live()  # Not needed on Tulip.
>>> amy.send(store_patch='1024,v0S0Zv0S1Zv1w0f0.25P0.5a0.5Zv0w0f261.63,1,0,0,0,1A0,1,500,0,0,0L1Z')
>>> amy.send(voices=0, load_patch=1024)
>>> amy.send(voices=0, vel=2, note=50)
```
AMY infers the number of oscs needed for the patch at `store_patch` time. If you store a new patch over an old one, that old memory is freed and re-allocated. (We rely on `malloc` for all of this.)

You can "record" patches in a sequence of commands like this:
```
>>> amy.log_patch()
>>> # Execute any commands to set up the oscillators.
>>> amy.preset(5)
>>> bass_drum = amy.retrieve_patch()
>>> bass_drum
'v0S0Zv0S1Zv1w0f0.25P0.5a0.5Zv0w0f261.63,1,0,0,0,1A0,1,500,0,0,0L1Z'
>>> amy.send(store_patch='1024,' + bass_drum)
```

**Note on patches and AMY timing**: If you're using AMY's time scheduler (see below) note that unlike all other AMY commands, allocating new voices from patches (using `load_patch`) will happen once AMY receives the message, not using any advance clock (`time`) you may have set. This default is the right decision for almost all use cases of AMY, but if you do need to be able to "schedule" voice allocations within the short term scheduling window, you can load patches by sending the patch string directly to AMY using the timer, and manage your own oscillator mapping in your code.


