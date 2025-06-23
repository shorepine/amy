# AMY synthesizer API

This page collects the current API for [AMY](https://github.com/shorepine/amy).

[**Please see our interactive AMY tutorial for more tips on using AMY**](https://shorepine.github.io/amy/tutorial.html)

## C / Arduino / Javascript API

Parsing, creating and adding events to AMY:

```c
// returns default empty amy_event
amy_event amy_default_event();

// clears an existing AMY event
amy_clear_event(amy_event *e);

// given an event play / schedule the event directly
amy_add_event(amy_event *e);

// given a wire message string play / schedule the event directly
amy_add_message(char *message);
```

Get and set external audio buffers:
```c

// get AUDIO_IN0 and AUDIO_IN1
amy_get_input_buffer(output_sample_type * samples);

// set AUDIO_EXT0 and AUDIO_EXT1
amy_set_external_input_buffer(output_sample_type * samples);
```

If not running live, render a new block of AMY audio into a int16_t buffer:

```c
output_sample_type * amy_simple_fill_buffer();
```

Get the AMY time:
```c
// on all platforms, sysclock is based on total samples played, using audio out (i2s or etc) as system clock
uint32_t amy_sysclock();
```


Start and stop AMY:
```c
amy_stop();

// Emscripten web start
amy_start_web();
amy_start_web_no_synths();

// Start AMY with a config
amy_start(amy_config_t c);

// Start playing audio in real time
amy_live_start();

// Stop playing audio
amy_live_stop();
```

Default MIDI handlers:
```c
void amy_enable_juno_filter_midi_handler(); // assigns the Juno-6 MIDI CC handler
```

## `amy_config_t`

Use like:

```c
amy_config_t amy_config = amy_default_config()
amy_config.max_sequencer_tags = 128;
amy_start(amy_config);
```

| Field   | Values | Default   |  Notes                                 |
| ------  | ------ | --------  |  --------                              |
| `features.chorus` | `0=off, 1=on` | On | If chorus is enabled (uses RAM) |
| `features.reverb` | `0=off, 1=on` | On | If reverb is enabled (uses RAM) |
| `features.echo` | `0=off, 1=on` |On | If echo is enabled (uses RAM) |
| `features.audio_in` | `0=off, 1=on` | On | If audio_in gets processed via the audio interface |
| `features.default_synths` | `0=off, 1=on` | On| If AMY boots with Juno-6 on `synth` 1 and GM drums on `synth` 10 |
| `features.partials` | `0=off, 1=on` | On | If partials are enabled |
| `features.custom` | `0=off, 1=on` | On | If custom C oscillators are enabled |
| `features.startup_bleep` | `0=off, 1=on` | On | If AMY plays a startup sound on boot |
| `midi` | `AMY_MIDI_IS_NONE`, `AMY_MIDI_IS_UART`, `AMY_MIDI_IS_USB_GADGET`, `AMY_MIDI_IS_WEBMIDI` | `AMY_MIDI_IS_NONE` | Which MIDI interface(s) are active |
| `audio` | `AMY_AUDIO_IS_NONE`, `AMY_AUDIO_IS_I2S`, `AMY_AUDIO_IS_USB_GADGET`, `AMY_AUDIO_IS_MINIAUDIO`| I2S or miniaudio | Which audio interface(s) are active |
| `max_oscs` | Int | 180 | How many oscillators to support |
| `max_sequencer_tags` | Int | 256 | How many sequencer items to handle |
| `max_voices` | Int | 64 | How many voices |
| `max_synths` | Int | 64 | How many synths |
| `max_memory_patches` | Int | 32 | How many in memory patches to supprot |
| `i2s_lrc`, `i2s_dout`, `i2s_din`, `i2s_bclk`, `i2s_mclk` | Int | -1 | Pin numbers for the I2S interface |
| `midi_out`, `midi_in` | Int | -1 | Pin number for the MIDI UART pins |
| `midi_uart` | 0,1,[2] | -1 | UART device index for MCU. Default 1 (`UART1`) on Pi Pico and ESP. Teensy is always `8` |
| `capture_device_id`, `playback_device_id` | Int | -1 | Which miniaudio device to use, -1 is auto |


## Hooks

Optionally called by AMY during rendering:

```c
// Optional render hook that's called per oscillator during rendering, used (now) for CV output from oscillators. return 1 if this oscillator should be silent
uint8_t (*amy_external_render_hook)(uint16_t osc, SAMPLE*, uint16_t len ) = NULL;

// Optional external coef setter (meant for CV control of AMY via CtrlCoefs)
float (*amy_external_coef_hook)(uint16_t channel) = NULL;

// Optional hook that's called after all processing is done for a block, meant for python callback control of AMY
void (*amy_external_block_done_hook)(void) = NULL;

// Optional hook for a consumer of AMY to access MIDI data coming IN to AMY -- see midi_mappings.c
void (*amy_external_midi_input_hook)(uint8_t * bytes, uint16_t len, uint8_t is_sysex) = NULL;

// Called every sequencer tick
void (*amy_external_sequencer_hook)(uint32_t) = NULL;
```


## `amy_event` and `amy.send` API:

**NOTE:** as of now, a few `amy_event` methods are not availble to C, only using the wire code (or `amy.send()` in Python.)

Please see [AMY synthesizer details](synth.md) for more explanation on the synthesizer parameters.

[**Please see our interactive AMY tutorial for more tips on using AMY**](https://shorepine.github.io/amy/tutorial.html)


### `synth`s and `voice`s:

| Wire code   | C/JS `amy_event` | Python `amy.send`   | Type-range  | Notes                                 |
| ------ | -------- | ---------- | ----------  | ------------------------------------- |
| `i`    | `synth` | `synth`  | 0-31  | Define a set of voices for voice management. |
| `if`   | `synth_flags` | `synth_flags` | uint | Flags for synth creation: 1 = Use MIDI drum note->preset translation; 2 = Drop note-off events. |
| `it`   | `to_synth` |  `to_synth` | 0-31 | New synth number, when changing the number (MIDI channel for n=1..16) of an entire synth. |
| `iv`   | `num_voices` | `num_voices` | int | The number of voices to allocate when defining a synth, alternative to directly specifying voice numbers with `voices=`.  Only valid with `synth=X, patch[_number]=Y`. |
| `im`   | `grab_midi_notes` | `grab_midi_notes` | 0/1 | Use `amy.send(synth=CHANNEL, grab_midi_notes=0)` to prevent the default direct forwarding of MIDI note-on/offs to synth CHANNEL. |
| `ip`   | `pedal` | `pedal` | int | Non-zero means pedal is down (i.e., sustain).  Must be used with `synth`. |
| `K`    | `patch_number` | `patch` | uint 0-X | Apply a saved or user patch to a specified synth or voice. |
| `r`    | `voices[]` | `voices` | int[,int] | Comma separated list of voices to send message to, or load patch into. |
| `u`    | **TODO**| `patch_string` | string | Provide AMY message to define up to 32 patches in RAM with ID numbers (1024-1055) provided via `patch_number`, or directly configure a `synth`. |


### Oscillator control

| Wire code   | C/JS `amy_event` | Python `amy.send`   | Type-range  | Notes                                 |
| ------ | -------- | ---------- | ----------  | ------------------------------------- |
| `v`    | `osc` | `osc` | uint 0 to OSCS-1 | Which oscillator to control |
| `w`    | `wave` | `wave` | uint 0-16 | Waveform: [0=SINE, PULSE, SAW_DOWN, SAW_UP, TRIANGLE, NOISE, KS, PCM, ALGO, PARTIAL, BYO_PARTIALS, INTERP_PARTIALS, AUDIO_IN0, AUDIO_IN1, CUSTOM, OFF]. default: 0/SINE |
| `S`    | `reset_osc`| `reset`  | uint | Resets given oscillator. set to RESET_ALL_OSCS to reset all oscillators, gain and EQ. RESET_TIMEBASE resets the clock (immediately, ignoring `time`). RESET_AMY restarts AMY. RESET_SEQUENCER clears the sequencer.|
| `A`    | `bp0` | `bp0`    | string      | Envelope Generator 0's comma-separated breakpoint pairs of time(ms) and level, e.g. `100,0.5,50,0.25,200,0`. The last pair triggers on note off (release) |
| `B`    | `bp1` | `bp1`    | string      | Breakpoints for Envelope Generator 1. See bp0 |
| `b`    | `feedback` | `feedback` | float 0-1 | Use for the ALGO synthesis type in FM or for karplus-strong, or to indicate PCM looping (0 off, >0, on) |
| `c`    | `chained_osc` | `chained_osc` |  uint 0 to OSCS-1 | Chained oscillator.  Note/velocity events to this oscillator will propagate to chained oscillators.  VCF is run only for first osc in chain, but applies to all oscs in chain. |
| `G`    | `filter_type` | `filter_type` | 0-4 | Filter type: 0 = none (default.) 1 = lowpass, 2 = bandpass, 3 = highpass, 4 = double-order lowpass. |
| `I`    | `ratio` | `ratio`  | float | For ALGO types, ratio of modulator frequency to  base note frequency  |
| `L`    | `mod_source` | `mod_source` | 0 to OSCS-1 | Which oscillator is used as an modulation/LFO source for this oscillator. Source oscillator will be silent. |
| `m`    | `portamento`| `portamento` | uint | Time constant (in ms) for pitch changes when note is changed without intervening note-off.  default 0 (immediate), 100 is good. |
| `n`    | `midi_note` | `note` | float, but typ. uint 0-127 | Midi note, sets frequency.  Fractional Midi notes are allowed. |
| `o`    | `algorithm` | `algorithm` | uint 1-32 | DX7 FM algorithm to use for ALGO type |
| `O`    | `algo_source[]`| `algo_source` | string | Which oscillators to use for the FM algorithm. list of six (starting with op 6), use empty for not used, e.g 0,1,2 or 0,1,2,,, |
| `p`    | `preset` | `preset` | int | Which predefined PCM preset patch to use, or number of partials if < 0. (Juno/DX7 patches are different - see `patch_number`). |
| `P`    | `phase` | `phase` | float 0-1 | Where in the oscillator's cycle to begin the waveform (also works on the PCM buffer). default 0 |
| `R`    | `resonance` | `resonance` | float | Q factor of variable filter, 0.5-16.0. default 0.7 |
| `T`    | `eg_type[0]` | `eg0_type` | uint 0-3 | Type for Envelope Generator 0 - 0: Normal (RC-like) / 1: Linear / 2: DX7-style / 3: True exponential. |
| `X`    | `eg_type[1]` | `eg1_type` | uint 0-3 | Type for Envelope Generator 1 - 0: Normal (RC-like) / 1: Linear / 2: DX7-style / 3: True exponential. |
| `l`    | `velocity` | `vel` | float | Note on velocity. Use to start an envelope or set amplitude |

### CtrlCoefs

These per-oscillator parameters use [CtrlCoefs](synth.md) notation

| Wire code   | C/JS `amy_event` | Python `amy.send`   | Type-range  | Notes                                 |
| ------ | -------- | ---------- | ----------  | ------------------------------------- |
| `Q`    | `pan_coefs[]` | `pan`   | float[,float...] | Panning index ControlCoefficients (for stereo output), 0.0=left, 1.0=right. default 0.5. |
| `a`    | `amp_coefs[]` | `amp`    | float[,float...]  | Control the amplitude of a note; a set of ControlCoefficients. Default is 0,0,1,1  (i.e. the amplitude comes from the note velocity multiplied by by Envelope Generator 0.) |
| `d`    | `duty_coefs[]` | `duty`   |  float[,float...] | Duty cycle for pulse wave, ControlCoefficients, defaults to 0.5 |
| `f`    | `freq_coefs[]` | `freq`   |  float[,float...]      | Frequency of oscillator, set of ControlCoefficients.  Default is 0,1,0,0,0,0,1 (from `note` pitch plus `pitch_bend`) |
| `F`    | `filter_freq_coefs[]` | `filter_freq` | float[,float...]  | Center/break frequency for variable filter, set of ControlCoefficients |


### Other

| Wire code   | C/JS `amy_event` | Python `amy.send`   | Type-range  | Notes                                 |
| ------ | -------- | ---------- | ----------  | ------------------------------------- |
| `H`    | `sequence[3]` | `sequence` | int,int,int | Tick offset, period, tag for sequencing | 
| `h`    | **TODO** | `reverb` | float[,float,float,float] | Reverb parameters -- level, liveness, damping, xover: Level is for output mix; liveness controls decay time, 1 = longest, default 0.85; damping is extra decay of high frequencies, default 0.5; xover is damping crossover frequency, default 3000 Hz. |
| `j`    | `tempo` | `tempo`  | float | The tempo (BPM, quarter notes) of the sequencer. Defaults to 108.0. |
| `k`    | **TODO** | `chorus` | float[,float,float,float] | Chorus parameters -- level, delay, freq, depth: Level is for output mix (0 to turn off); delay is max in samples (320); freq is LFO rate in Hz (0.5); depth is proportion of max delay (0.5). |
| `M`    | **TODO** | `echo` | float[,int,int,float,float] | Echo parameters --  level, delay_ms, max_delay_ms, feedback, filter_coef (-1 is HPF, 0 is flat, +1 is LPF). |
| `N`    | `latency_ms`| `latency_ms` | uint | Sets latency in ms. default 0 (see LATENCY) |
| `s`    | `pitch_bend` | `pitch_bend` | float | Sets the global pitch bend, by default modifying all note frequencies by (fractional) octaves up or down |
| `t`    | `time` | `time` | uint | Request playback time relative to some fixed start point on your host, in ms. Allows precise future scheduling. |
| `V`    | `volume`| `volume` | float 0-10 | Volume knob for entire synth, default 1.0 |
| `x`    | `eq_l, eq_m, eq_h` |`eq` | float,float,float | Equalization in dB low (~800Hz) / med (~2500Hz) / high (~7500Gz) -15 to 15. 0 is off. default 0. |
| `z`    | **TODO**| `load_sample` | uint x 6 | Signal to start loading sample. preset number, length(samples), samplerate, midinote, loopstart, loopend. All subsequent messages are base64 encoded WAVE-style frames of audio until `length` is reached. Set `preset` and `length=0` to unload a sample from RAM. |
| `D`    | **TODO** | `debug`  |  uint, 2-4  | 2 shows queue sample, 3 shows oscillator data, 4 shows modified oscillator. Will interrupt audio! |


