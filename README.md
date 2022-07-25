# amy
AMY - the Additive Music synthesizer librarY

AMY is a simple, fast, small and accurate audio synthesizer library that deals with combinations of many oscillators very well. It can easily be embedded into almost any program, architecture or microcontroller with an FPU and around 100KB of RAM. 

It supports

 * An arbitrary number (compile-time option) of band-limited oscillators, each with adjustable frequency and amplitude:
   * pulse (+ adjustable duty cycle)
   * sine
   * saw
   * triangle
   * noise
   * PCM, reading from a baked-in buffer of percussive and misc samples
   * karplus-strong string with adjustable feedback (can have up to 2 per synth)
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




