# AMY Control Coefficients (CtrlCoefs)


**TODO**

Maybe a picture of synth panel with these knobs to explain the metaphor

Link to tutorial




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

We have some helpful patches in `example_patches.py`, if you want to use them, or add to them. To make that filter bass, just do `amy.send(synth=0, num_voices=4, patch_number=patches.filter_bass())` and then `amy.send(synth=0,vel=1,note=50)` to hear it.
