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

The FM synth provides a Python library, [`fm.py`](https://github.com/shorepine/amy/blob/main/fm.py) that can convert any DX7 patch into an AMY patch.

The Juno-6 emulation provides [`juno.py`](https://github.com/shorepine/amy/blob/main/juno.py) and can read in Juno-6 SYSEX patches and convert them into AMY patches.

[The partials-driven piano voice and the code to generate the partials are described here](https://shorepine.github.io/amy/piano.html).

## Using AMY in Arduino

AMY will run on many modern microcontrollers under Arduino. On most platforms, we handle sending audio out to an I2S interface and handling MIDI input. Some platforms support more features than others. 

**Please see our more detailed [Getting Started on Arduino](docs/arduino.md) page for more details.**

## Using AMY in Python on any platform

You can `import amy` in Python and have it render either out to your speakers or to a buffer of samples you can process on your own. To install the `libamy` library, run `cd src; pip install .`. You can also run `make test` to install the library and run a series of tests.

[**Please see our interactive AMY tutorial for more tips on using AMY from Python**](https://shorepine.github.io/amy/tutorial.html)

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

[**Please see our interactive AMY tutorial for more tips on using AMY from Python**](https://shorepine.github.io/amy/tutorial.html)

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
e.voices = '0'
```

In Python, we provide `amy.py` that generates wire messages from a Pythonic `amy.send(**kwargs)`. In Python, you'd do

```python
amy.send(osc=0, patch_number = 130, vel = 1, note = 50, voices = [0])
```

Wire messages are used in AMY as a compact serialization of AMY events and become useful when communicating between AMY and other programs that may not be linked together. For example, [Alles](https://github.com/shorepine/alles) uses wire messages over Wi-Fi UDP to control a mesh of AMY synthesizers. [Tulip Web](https://tulip.computer/run) sends wire messages from the Micropython web process to the AudioWorklet running AMY on the web. We also store the Juno-6 and DX7 patches within AMY itself using wire messages, which helps keep the code size down. 

You can also send wire messages over SYSEX to AMY, if you want to control AMY over MIDI beyond the default MIDI mode. [See our MIDI documentation for more details.](docs/midi.md)

It's good to understand what wire messages are but you don't need to construct them directly if you're linking AMY in your software. Use `amy_event` or `amy.send()` in Python to control AMY for almost all use cases.


## Oscillators, voices, patches and synths

**TODO** : a nice omnigraffle or whatever diagram of many small oscillators making up a voice, many voices making up a synth

AMY's lowest level of control is the `osc`illator - a single waveform that you can define a number of parameters for, apply filters, frequency, pan, etc. By default AMY ships with support for 180 oscillators running at once. 

We then provide `voices`, to make it easier to configure and use groups of oscillators in coordination. For example, a single Juno-6 note is a single voice made from 5 oscillators. 

You configure a voice by using a `patch`, which is simply a stored list of AMY commands that set up one or more oscillators.  You can assign any patch to any voice number, or set up mulitple voices to have the same patch (for example, a polyphonic synth), and AMY will allocate the oscillators it needs under the hood. 

You then manage a set of voices using a `synth`, which takes care of allocating available voices to successive notes. For example a Juno-6 synth can play 6 notes of a patch at once. The `synth` in AMY allocates 6 `voices`, each with 5 `osc`, and handles note stealing and parameter changes. 

(Note that when you use voices, you'll need to include the `voices` or `synth` args when addressing oscillators, and AMY will automatically route your command to the relevant oscillator in each voice set -- there's no other way to tell which oscillators are being used by which voices.)

To play a patch, for instance the built-in patches emulating Juno and DX7 synthesizers and a piano, you allocate them to one or more voices (either directly or as part of a synth), then send note events, or parameter moidifications, to those voices. We ship patches 0-127 for Juno, 128-255 for DX7, and 256 for our [built in piano](https://shorepine.github.io/amy/piano.html). For example, a multitimbral Juno/DX7 synth can be set up like this:

```python
amy.send(voices='0,1,2,3', patch_number=1)     # Juno patch #1 on voice 0-3
amy.send(voices='4,5,6,7', patch_number=129)   # DX7 patch #2 on voices 4-7
amy.send(voices=0, note=60, vel=1)           # Play note 60 on voice 0
amy.send(voices=0, osc=0, filter_freq=8000)  # Open up the filter on the Juno voice (using its bottom oscillator)
```

The code in `amy_headers.py` generates these patches and bakes them into AMY so they're ready for playback on any device. You can add your own patches at compile time by storing alternative wire-protocol setup strings in `patches.h`, or by making user patches at runtime:


### User patches

**TODO: I think this is older still, dan to tell me what to put here**

You can also create your own patches at runtime and use them for voices with `amy.send(patch_number=PATCH_NUMBER, patch='AMY_PATCH_STRING')` where `PATCH_NUMBER` is a number in the range 1024-1055. This message must be on its own in the `amy.send()` command, not combined with any other parameters, because AMY will treat the rest of the message as a patch rather than interpreting the remaining arguments as ususal.

So you can do:

```python
>>> import amy; amy.live()  # Not needed on Tulip.
>>> amy.send(patch_number=1024, patch='v0S0Zv0S1Zv1w0f0.25P0.5a0.5Zv0w0f261.63,1,0,0,0,1A0,1,500,0,0,0L1Z')
>>> amy.send(voices=0, patch_number=1024)
>>> amy.send(voices=0, vel=2, note=50)
```
You can also directly specify the patch string when loading a patch into a voice or synth, if you won't need to reference it again:
```
>>> amy.send(voices=0, patch='v0S0Zv0S1Zv1w0f0.25P0.5a0.5Zv0w0f261.63,1,0,0,0,1A0,1,500,0,0,0L1Z')
```
AMY infers the number of oscs needed for the patch from the `patch` string. If you store a new patch over an old one, that old memory is freed and re-allocated. (We rely on `malloc` for all of this.)

You can "record" patches in a sequence of commands like this:
```
>>> amy.log_patch()
>>> # Execute any commands to set up the oscillators.
>>> amy.preset(5)
>>> bass_drum = amy.retrieve_patch()
>>> bass_drum
'v0S0Zv0S1Zv1w0f0.25P0.5a0.5Zv0w0f261.63,1,0,0,0,1A0,1,500,0,0,0L1Z'
>>> amy.send(patch_number=1024, patch=bass_drum)
```


### Synths

A common use-case is to want a pool of voices which are allocated to a series of notes as-needed.  This is accomplished with **synths**.  You associate a synth number with a set of voices when defining the patch for those voices; the `synth` arg becomes a smart alias for the appropriate `voices`, e.g.

```python
amy.send(synth=0, num_voices=3, patch_number=1)     # 3-voice Juno path #1 on synth 0
# Play three notes simultaneously
amy.send(synth=0, note=60, vel=1)
amy.send(synth=0, note=64, vel=1)
amy.send(synth=0, note=67, vel=1)
# To play a 4th note, the synth 'steals' the oldest voice, i.e. the one that was playing note 60
amy.send(synth=0, note=70, vel=1)
# We can send note-offs to individual notes
amy.send(synth=0, note=70, vel=0)
# .. or we can send note-offs to all the currently-active synth voices by sending a note-off with no note.
amy.send(synth=0, vel=0)
# Once a synth has been initialized and associated with a set of voices, you can use it alone with patch_number
amy.send(synth=0, patch_number=13)  # Load a different Juno patch, will remain 4-voice.
# You can release all the voices/oscs being used by a synth by setting its num_voices to zero.
amy.send(synth=0, num_voices=0)
# As a special case, you can use synth_flags to set up a MIDI drum synth that will translate note events into PCM presets.
amy.send(synth=10, num_voices=3, patch='w7f0Z', synth_flags=3)
amy.send(synth=10, note=40, vel=1)  # MIDI drums 'electric snare'
```

(Note: Although `note` can take on real values -- e.g. `note=60.5` for 50 cents above C4 -- the voice management tracks voices by integer note numbers (i.e., midi notes) so it rounds note values to the nearest integer when deciding which note-off goes with which note-on.  Note also that note-on events that also set the `preset` parameter (e.g. to select PCM samples) will fold the patch number into the note integer used as the key for note-on, note-off matching.)

# Synthesizer details


## AMY's sequencer and timestamps

AMY can accept a `time` (in milliseconds) parameter to schedule events in the future, and also provides a pattern sequencer for repeating events.

The scheduled events are very helpful in cases where you can't rely on an accurate clock from the client, or don't have one. The clock used internally by AMY is based on the audio samples being generated out the speakers, which should run at an accurate 44,100 times a second.  This lets you do things like schedule fast moving parameter changes over short windows of time. 

```python
start = amy.millis()  # arbitrary start timestamp
amy.send(osc=0, note=50, vel=1, time=start)
amy.send(osc=0, note=52, vel=1, time=start + 1000)
```

Both `amy.send()`s will return immediately, but you'll hear the second note play precisely a second after the first. AMY uses this internal clock to schedule step changes in breakpoints as well. 


### The sequencer

AMY starts a musical sequencer that works on `ticks` from startup. You can reset the `ticks` to 0 with an `amy.send(reset=amy.RESET_TIMEBASE)`. Note this will happen immediately, ignoring any `time` or `sequence`.

Ticks run at 48 PPQ at the set tempo. The tempo defaults to 108 BPM. This means there are 108 quarter notes a minute, and `48 * 108 = 5184` ticks a minute, 86 ticks a second. The tempo can be changed with `amy.send(tempo=120)`.

You can schedule an event to happen at a precise tick with `amy.send(... ,sequence="tick,period,tag")`. `tick` can be an absolute or offset tick number. If `period` is ommited or 0, `tick` is assumed to be absolute and once AMY reaches `tick`, the rest of your event will play and the saved event will be removed from memory. If an absolute `tick` is in the past, AMY will ignore it. 

You can schedule repeating events (like a step sequencer or drum machine) with `period`, which is the length of the sequence in ticks. For example a `period` of 48 with `ticks` omitted or 0 will trigger once every quarter note. A `period` of 24 will happen twice every quarter note. A `period` of 96 will happen every two quarter notes. `period` can be any whole number to allow for complex rhythms. 

For pattern sequencers like drum machines, you will also want to use `tick` alongisde `period`. If both are given and nonzero, `tick` is assumed to be an offset on the `period`. For example, for a 16-step drum machine pattern running on eighth notes (PPQ/2), you would use a `period` of `16 * 24 = 384`. The first slot of the drum machine would have a `tick` of 0, the 2nd would have a `tick` offset of 24, and so on. 

`tag` should be given, and will be `0` if not. You should set `tag` to a random or incrementing number in your code that you can refer to later. `tag` allows you to replace or delete the event once scheduled. 

If you are including AMY in a program, you can set the [hook `void (*amy_external_sequencer_hook)(uint32_t)`](docs/api.md) to any function. This will be called at every tick with the current tick number as an argument. 

Sequencer examples:

```python
amy.send(osc=2, vel=1, wave=amy.PCM, preset=2, sequence="1000,,3") # play a PCM drum at absolute tick 1000 

amy.send(osc=0, vel=1, wave=amy.PCM, preset=0, sequence=",24,1") # play a PCM drum every eighth note.
amy.send(osc=1, vel=1, wave=amy.PCM, preset=1, sequence=",48,2") # play a PCM drum every quarter note.
amy.send(sequence=",,1") # remove the eighth note sequence
amy.send(osc=1, vel=1, wave=amy.PCM, preset=1, note=70, sequence=",48,2") # change the quarter note event

amy.send(reset=amy.RESET_SEQUENCER) # clears the sequence

amy.send(osc=0, vel=1, wave=amy.PCM, preset=0, sequence="0,384,1") # first slot of a 16 1/8th note drum machine
amy.send(osc=1, vel=1, wave=amy.PCM, preset=1, sequence="216,384,2") # ninth slot of a 16 1/8th note drum machine


```

## Examples

**TODO** : move these to tutorial

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
amy.send(voices='0', patch_number=128)  # Set up a voice.
amy.send(voices='0', note=50, vel=1)  # Play a note on the voice.
```

The `patch_number` lets you set which preset is used (0 to 127 are the Juno 106 analog synth presets, and 128 to 255 are the DX7 FM presets).  But let's make the classic FM bell tone ourselves, without a patch. We'll just be using two operators (two sine waves), one modulating the other.

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


## Build-your-own Partials

You can also explicitly control partials in "build-your-own partials" mode, accessed via `wave=amy.BYO_PARTIALS`.  This sets up a string of oscs as individual sinusoids, but it's up to you to control the details of each partial via its parameters, envelopes, etc.  You just have to say how many partials you want with `num_partials`.  You can then individually set up the amplitude `bp0` envelopes of the next `num_partials` oscs for arbitrary control, subject to the limit of 7 breakpoints plus release for each envelope.  For instance, to get an 8-harmonic pluck tone with a 50 ms attack, and harmonic weights and decay times inversely proportional to to the harmonic number:

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

You can add a filter (or an envelope etc.) to the sum of all the `PARTIAL` oscs by configuring it on the parent `BYO_PARTIALS` osc:

```
amy.send(osc=0, filter=amy.FILTER_HPF, resonance=4, filter_freq={'const': 200, 'eg1': 4}, bp1='0,0,1000,1,0,0')
amy.send(osc=0, note=60, vel=1)
# etc.
```
Note that the default `bp0` amplitude envelope of the `BYO_PARTIALS` osc is a gate, so if you want to have a nonzero release on your partials, you'll need to add a slower release to the `BYO_PARTIALS` osc to avoid it cutting them off.


## Interpolated partials

Please see our [piano voice documentation](https://shorepine.github.io/amy/piano.html) for more on the `INTERP_PARTIALS` type. 


## PCM

AMY comes with a set of 67 drum-like and instrument PCM samples to use as well, as they are normally hard to render with additive, subtractive or FM synthesis. You can use the type `PCM` and preset numbers 0-66 to explore them. Their native pitch is used if you don't give a frequency or note parameter. You can update the baked-in PCM sample bank using `amy_headers.py`. 


```python
amy.send(osc=0, wave=amy.PCM, vel=1, preset=10) # cowbell
amy.send(osc=0, wave=amy.PCM, vel=1, preset=10, note=70) # higher cowbell! 
```

You can turn on sample looping, helpful for instruments, using `feedback`:

```python
amy.send(wave=amy.PCM,vel=1,preset=21,feedback=0) # clean guitar string, no looping
amy.send(wave=amy.PCM,vel=1,preset=21,feedback=1) # loops forever until note off
amy.send(vel=0) # note off
amy.send(wave=amy.PCM,vel=1,preset=35,feedback=1) # nice violin
```

## Sampler (aka Memory PCM)

You can also load your own samples into AMY at runtime. We support sending PCM data over the wire protocol. Use `load_sample` in `amy.py` as an example:

```python
amy.load_sample("G1.wav", preset=3)
amy.send(osc=0, wave=amy.PCM, preset=3, vel=1) # plays the sample
```

You can use any preset number. If it overlaps with an existing PCM baked in number, it will play the memory sample instead of the baked in sample until you `unload_sample` the preset.

If the WAV file has sampler metadata like loop points or base MIDI note, we use that in AMY. You can set it directly as well using `loopstart`, `loopend`, `midinote` or `length` in the `load_sample` call. To unload a sample:

```python
amy.unload_sample(3) # unloads the RAM for preset 3
```

Under the hood, if AMY receives a `load_sample` message (with preset number and nonzero length), it will then pause all other message parsing until it has received `length` amount of base64 encoded bytes over the wire protocol. Each individual message must be base64 encoded. Since AMY's maximum message length is 255 bytes, there is logic in `load_sample` in `amy.py`  to split the sample data into 188 byte chunks, which generates 252 bytes of base64 text. Please see `amy.load_sample` if you wish to load samples on other platforms.


