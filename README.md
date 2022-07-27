# amy
AMY - the Additive Music synthesizer librarY

AMY is a fast, small and accurate audio synthesizer library that deals with combinations of many oscillators very well. It can easily be embedded into almost any program, architecture or microcontroller with an FPU and around 100KB of RAM. We've built AMY on Mac, Linux, the microcontrollers ESP32 and ESP32S3, and more to come. 

AMY powers the multi-speaker mesh synthesizer [Alles](https://github.com/bwhitman/alles), as well as the forthcoming Tulip Creative Computer. Let us know if you use AMY for your own projects and we'll add it here!

AMY was built by [DAn Ellis](https://research.google/people/DanEllis/) and [Brian Whitman](https://notes.variogr.am), and would love your contributions.

It supports

 * An arbitrary number (compile-time option) of band-limited oscillators, each with adjustable frequency and amplitude:
   * pulse (+ adjustable duty cycle)
   * sine
   * saw
   * triangle
   * noise
   * PCM, reading from a baked-in buffer of percussive and misc samples
   * karplus-strong string with adjustable feedback 
   * An operator / algorithm-based frequency modulation (FM) synth
 * Biquad low-pass, bandpass or hi-pass filters with cutoff and resonance, can be assigned to any oscillator
 * An additive partial synthesizer with an analysis front end to play back long strings of breakpoint-based sine waves, including amplitude modulated noise
 * Oscillators can be specified by frequency in floating point or midi note 
 * Each oscillator has 3 breakpoint generators, which can modify any combination of amplitude, frequency, duty, filter cutoff, feedback or resonance
 * Each oscillator can also act as an modulator to modify any combination of parameters of another oscillator, for example, a bass drum can be indicated via a half phase sine wave at 0.25Hz modulating the frequency of another sine wave. 
 * Control of overall gain and 3-band parametric EQ
 * Built in patches for PCM, FM and partials

 The FM synthesizer in AMY is especially well-loved and as close to a real DX7 as you can get in floating point. We provide a Python library, `fm.py` that can convert any DX7 patch into AMY setup commands, and also a pure-Python implementation of the AMY FM synthesizer in `dx7_simulator.py`.

 The partial tone synthesizer also provides `partials.py`, where you can mode the partials of any arbitrary audio into AMY setup commands for live partial playback of hundreds of oscillators.


## Controlling AMY

AMY can be controlled using its wire protocol or by fillng its data structures directly. It's up to what's easier for you and your application. 

In Python, rendering to a buffer of samples, using the high level API:

```python
>>> import amy
>>> m = amy.message(osc=0,wave=amy.ALGO,patch=30,note=50,vel=1)
>>> print(m) # Show the wire protocol message
't76555951v0w8n50p30l1Z'
>>> amy.send(m)
>>> audio = amy.render(5.0)
```

In C, using the high level structures directly:

```c
#include "amy.h"
void bleep() {
    struct event e = default_event();
    int64_t sysclock = get_sysclock();
    e.time = sysclock;
    e.wave = SINE;
    e.freq = 220;
    e.velocity = 1;
    add_event(e);
    e.time = sysclock + 150;
    e.freq = 440;
    add_event(e);
    e.time = sysclock + 300;
    e.velocity = 0;
    e.amp = 0;
    e.freq = 0;
    add_event(e);
}

void main() {
    live_start();
    bleep();
}
```

Or in C, sending the wire protocol directly:

```c
#include "amy.h"

void main() {
    live_start();
    parse_message("t76555951v0w8n50p30l1Z");
}
```

AMY's wire protocol is a series of numbers delimited by ascii characters that define all possible parameters of an oscillator. This is a design decision intended to make using AMY from any sort of environment as easy as possible, with no data structure or parsing overhead on the client. It's also readable and compact, far more expressive than MIDI and can be sent over network links, UARTs, or as arguments to functions or commands. We've used AMY over multicast UDP, over javascript, in MAX/MSP, in Python, C, Micropython and many more! 

AMY accepts commands in ASCII, each command separated with a `Z` (you can group multiple messages in one, to avoid network overhead if that's your transport). Like so:

```
v0w4f440.0l0.9Z
```

Here's the full list:

```
a = amplitude, float 0-1+. use after a note on is triggered with velocity to adjust amplitude without re-triggering the note
A = breakpoint0, string, in commas, like 100,0.5,150,0.25,200,0 -- envelope generator with alternating time(ms) and ratio. last pair triggers on note off
B = breakpoint1, set the second breakpoint generator. see breakpoint0
b = feedback, float 0-1. use for the ALGO synthesis type in FM, or partial synthesis (for bandwidth) or for karplus-strong, or to indicate PCM looping (0 off, >0, on)
C = breakpoint2, set the third breakpoint generator. see breakpoint0
c = client, uint, 0-255 indicating a single client, 256-510 indicating (client_id % (x-255) == 0) for groups, default all clients
d = duty cycle, float 0.001-0.999. duty cycle for pulse wave, default 0.5
D = debug, uint, 2-4. 2 shows queue sample, 3 shows oscillator data, 4 shows modified oscillator. will interrupt audio!
f = frequency, float 0-44100 (and above). default 0. Sampling rate of synth is 44,100Hz but higher numbers can be used for PCM waveforms
F = center frequency of biquad filter. default 0. 
g = modulation target mask. Which parameter modulation/LFO controls. 1=amp, 2=duty, 4=freq, 8=filter freq, 16=resonance, 32=feedback. Can handle any combo, add them together
G = filter type. 0 = none (default.) 1 = low pass, 2 = band pass, 3 = hi pass. 
I = ratio. for ALGO types, where the base note frequency controls the modulators, or for the ALGO base note and PARTIALS base note, where the ratio controls the speed of the playback
L = modulation source oscillator. 0-63. Which oscillator is used as an modulation/LFO source for this oscillator. Source oscillator will be silent. 
l = velocity (amplitude), float 0-1+, >0 to trigger note on, 0 to trigger note off.  
N = latency (ms.) 0-5000 (in practice). default 1000 for mesh use. change to 0 if using locally
n = midinote, uint, 0-127 (this will also set f). default 0
o = algorithm, choose which algorithm for the ALGO type, uint, 1-32. mirrors DX7 algorithms 
O = algorithn source oscillators, choose which oscillators make up the algorithm oscillator, like "0,1,2,3,4,5" for algorithm 0
p = patch, uint, choose a preloaded PCM sample, partial patch or FM patch number for FM waveforms. See fm.h, pcm.h, partials.h. default 0
P = phase, float 0-1. where in the oscillator's cycle to start sampling from (also works on the PCM buffer). default 0
R = q factor / "resonance" of biquad filter. float. in practice, 0 to 10.0. default 0.7.
s = sync, int64, same as time but used alone to do an enumeration / sync, see alles.py
S = reset oscillator, uint 0-63 or for all oscillators, anything >63, which also resets speaker gain and EQ.
T = breakpoint0 target mask. Which parameter the breakpoints controls. 1=amp, 2=duty, 4=freq, 8=filter freq, 16=resonance, 32=feedback. Can handle any combo, add them together. Add 64 to indicate linear ramp, otherwise exponential
t = time, int64: ms since some fixed start point on your host. you should always give this if you can.
v = oscillator, uint, 0 to 63. default: 0
V = volume, float 0 to about 10 in practice. volume knob for the entire synth / speaker. default 1.0
w = waveform, uint: [0=SINE, PULSE, SAW_DOWN, SAW_UP, TRIANGLE, NOISE, KS, PCM, ALGO, PARTIAL, PARTIALS, OFF]. default: 0/SINE
W = breakpoint1 target mask. 
x = "low" EQ amount for the entire synth (Fc=800Hz). float, in dB, -15 to 15. 0 is off. default: 0
X = breakpoint2 target mask. 
y = "mid" EQ amount for the entire synth (Fc=2500Hz). float, in dB, -15 to 15. 0 is off. default: 0
z = "high" EQ amount for the entire synth (Fc=7500Hz). float, in dB, -15 to 15. 0 is off. default: 0

```


