# amy
AMY - the Additive Music synthesizer librarY

AMY is a fast, small and accurate audio synthesizer library that deals with combinations of many oscillators very well. It can easily be embedded into almost any program, architecture or microcontroller with an FPU and around 100KB of RAM. 

AMY powers the multi-speaker mesh synthesizer Alles, as well as the Tulip Creative Computer. 

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

```python
>>> import amy
>>> m = amy.message(osc=0,wave=amy.ALGO,patch=30,note=50,vel=1)
>>> print(m)
"v0w5p30n50l1Z"
>>> amy.send(m)
>>> audio = amy.render(5.0)
```

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

```c
#include "amy.h"

void main() {
    live_start();
    parse_message("v0w5p30n50l1Z");
}
```

AMY's wire protocol is a series of numbers delimited by ascii characters that define all possible parameters of an oscillator. This is a design decision intended to make using AMY from any sort of environment as easy as possible, with no data structure or parsing overhead on the client. It's also readable and compact, far more expressive than MIDI and can be sent over network links, UARTs, or as arguments to functions or commands. 




