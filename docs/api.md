# AMY synthesizer API

This page collects the current API for [AMY](https://github.com/shorepine/amy).


## AMY wire message, `amy_event` and `amy.send` parameters:


| Wire code   | C/JS `amy_event` | Python `amy.send`   | Type-range  | Notes                                 |
| ------ | -------- | ----------  | ------------------------------------- |
| `a`    | `amp_coefs[]` | `amp`    | float[,float...]  | Control the amplitude of a note; a set of ControlCoefficients. Default is 0,0,1,1  (i.e. the amplitude comes from the note velocity multiplied by Envelope Generator 0.) |
| `A`    | `bp0` | `bp0`    | string      | Envelope Generator 0's comma-separated breakpoint pairs of time(ms) and level, e.g. `100,0.5,50,0.25,200,0`. The last pair triggers on note off (release) |
| `b`    | `feedback` | `feedback` | float 0-1 | Use for the ALGO synthesis type in FM or for karplus-strong, or to indicate PCM looping (0 off, >0, on) |
| `B`    | `bp1` | `bp1`    | string      | Breakpoints for Envelope Generator 1. See bp0 |
| `c`    | `chained_osc` | `chained_osc` |  uint 0 to OSCS-1 | Chained oscillator.  Note/velocity events to this oscillator will propagate to chained oscillators.  VCF is run only for first osc in chain, but applies to all oscs in chain. |
| `d`    | `duty_coefs[]` | `duty`   |  float[,float...] | Duty cycle for pulse wave, ControlCoefficients, defaults to 0.5 |
| `D`    | **TODO** | `debug`  |  uint, 2-4  | 2 shows queue sample, 3 shows oscillator data, 4 shows modified oscillator. Will interrupt audio! |
| `f`    | `freq_coefs[]` | `freq`   |  float[,float...]      | Frequency of oscillator, set of ControlCoefficients.  Default is 0,1,0,0,0,0,1 (from `note` pitch plus `pitch_bend`) |
| `F`    | `filter_freq_coefs[]` | `filter_freq` | float[,float...]  | Center/break frequency for variable filter, set of ControlCoefficients |
| `G`    | `filter_type` | `filter_type` | 0-4 | Filter type: 0 = none (default.) 1 = lowpass, 2 = bandpass, 3 = highpass, 4 = double-order lowpass. |
| `H`    | `sequence[3]` | `sequence` | int,int,int | Tick offset, period, tag for sequencing | 
| `h`    | **TODO** | `reverb` | float[,float,float,float] | Reverb parameters -- level, liveness, damping, xover: Level is for output mix; liveness controls decay time, 1 = longest, default 0.85; damping is extra decay of high frequencies, default 0.5; xover is damping crossover frequency, default 3000 Hz. |
| `i`    | `synth` | `synth`  | 0-31  | Define a set of voices for voice management. |
| `if`   | `synth_flags` | `synth_flags` | uint | Flags for synth creation: 1 = Use MIDI drum note->preset translation; 2 = Drop note-off events. |
| `ip`   | `pedal` | `pedal` | int | Non-zero means pedal is down (i.e., sustain).  Must be used with `synth`. |
| `it`   | `to_synth` |  `to_synth` | 0-31 | New synth number, when changing the number (MIDI channel for n=1..16) of an entire synth. |
| `iv`   | `num_voices` | `num_voices` | int | The number of voices to allocate when defining a synth, alternative to directly specifying voice numbers with `voices=`.  Only valid with `synth=X, patch[_number]=Y`. |
| `im`   | `grab_midi_notes` | `grab_midi_notes` | 0/1 | Use `amy.send(synth=CHANNEL, grab_midi_notes=0)` to prevent the default direct forwarding of MIDI note-on/offs to synth CHANNEL. |
| `I`    | `ratio` | `ratio`  | float | For ALGO types, ratio of modulator frequency to  base note frequency  |
| `j`    | `tempo` | `tempo`  | float | The tempo (BPM, quarter notes) of the sequencer. Defaults to 108.0. |
| `k`    | **TODO** | `chorus` | float[,float,float,float] | Chorus parameters -- level, delay, freq, depth: Level is for output mix (0 to turn off); delay is max in samples (320); freq is LFO rate in Hz (0.5); depth is proportion of max delay (0.5). |
| `K`    | `patch_number` | `patch_number` | uint 0-X | Apply a saved patch (e.g. DX7 or Juno) to a specified synth or voice. |
| `l`    | `velocity` | `vel` | float 0-1+ | Velocity: > 0 to trigger note on, 0 to trigger note off |
| `L`    | `mod_source` | `mod_source` | 0 to OSCS-1 | Which oscillator is used as an modulation/LFO source for this oscillator. Source oscillator will be silent. |
| `m`    | `portamento`| `portamento` | uint | Time constant (in ms) for pitch changes when note is changed without intervening note-off.  default 0 (immediate), 100 is good. |
| `M`    | **TODO** | `echo` | float[,int,int,float,float] | Echo parameters --  level, delay_ms, max_delay_ms, feedback, filter_coef (-1 is HPF, 0 is flat, +1 is LPF). |
| `n`    | `midi_note` | `note` | float, but typ. uint 0-127 | Midi note, sets frequency.  Fractional Midi notes are allowed. |
| `N`    | `latency_ms`| `latency_ms` | uint | Sets latency in ms. default 0 (see LATENCY) |
| `o`    | `algorithm` | `algorithm` | uint 1-32 | DX7 FM algorithm to use for ALGO type |
| `O`    | `algo_source[]`| `algo_source` | string | Which oscillators to use for the FM algorithm. list of six (starting with op 6), use empty for not used, e.g 0,1,2 or 0,1,2,,, |
| `p`    | `preset` | `preset` | int | Which predefined PCM or Partials preset patch to use, or number of partials if < 0. (Juno/DX7 patches are different - see `patch_number`). |
| `P`    | `phase` | `phase` | float 0-1 | Where in the oscillator's cycle to begin the waveform (also works on the PCM buffer). default 0 |
| `Q`    | `pan_coefs[]` | `pan`   | float[,float...] | Panning index ControlCoefficients (for stereo output), 0.0=left, 1.0=right. default 0.5. |
| `r`    | `voices[]` | `voices` | int[,int] | Comma separated list of voices to send message to, or load patch into. |
| `R`    | `resonance` | `resonance` | float | Q factor of variable filter, 0.5-16.0. default 0.7 |
| `s`    | `pitch_bend` | `pitch_bend` | float | Sets the global pitch bend, by default modifying all note frequencies by (fractional) octaves up or down |
| `S`    | `reset_osc`| `reset`  | uint | Resets given oscillator. set to RESET_ALL_OSCS to reset all oscillators, gain and EQ. RESET_TIMEBASE resets the clock (immediately, ignoring `time`). RESET_AMY restarts AMY. RESET_SEQUENCER clears the sequencer.|
| `t`    | `time` | `time` | uint | Request playback time relative to some fixed start point on your host, in ms. Allows precise future scheduling. |
| `T`    | `eg_type[0]` | `eg0_type` | uint 0-3 | Type for Envelope Generator 0 - 0: Normal (RC-like) / 1: Linear / 2: DX7-style / 3: True exponential. |
| `u`    | **TODO**| `patch` | string | Provide AMY message to define up to 32 patches in RAM with ID numbers (1024-1055) provided via `patch_number`, or directly configure a `synth`. |
| `v`    | `osc` | `osc` | uint 0 to OSCS-1 | Which oscillator to control |
| `V`    | `volume`| `volume` | float 0-10 | Volume knob for entire synth, default 1.0 |
| `w`    | `wave` | `wave` | uint 0-16 | Waveform: [0=SINE, PULSE, SAW_DOWN, SAW_UP, TRIANGLE, NOISE, KS, PCM, ALGO, PARTIAL, BYO_PARTIALS, INTERP_PARTIALS, AUDIO_IN0, AUDIO_IN1, CUSTOM, OFF]. default: 0/SINE |
| `x`    | `eq_l, eq_m, eq_h` |`eq` | float,float,float | Equalization in dB low (~800Hz) / med (~2500Hz) / high (~7500Gz) -15 to 15. 0 is off. default 0. |
| `X`    | `eg_type[1]` | `eg1_type` | uint 0-3 | Type for Envelope Generator 1 - 0: Normal (RC-like) / 1: Linear / 2: DX7-style / 3: True exponential. |
| `z`    | **TODO**| `load_sample` | uint x 6 | Signal to start loading sample. preset number, length(samples), samplerate, midinote, loopstart, loopend. All subsequent messages are base64 encoded WAVE-style frames of audio until `length` is reached. Set `preset` and `length=0` to unload a sample from RAM. |


