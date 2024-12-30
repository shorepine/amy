# The AMY Additive Piano Voice

The piano is a versatile and mature instrument.  One reason for its popularity is its wide timbral range; in fact, its main innovation was the ability to play notes both loud and soft, hence "[Pianoforte](https://en.wikipedia.org/wiki/Piano#History)" (its full original name).

To make AMY into a truly general-purpose music synthesizer, we wanted to add a good piano voice.  But it's not simple to synthesize a good piano voice.  This page explains some of what makes piano synthesis challenging, and how we addressed it.  Our approach was to use [additive synthesis](https://en.wikipedia.org/wiki/Additive_synthesis), which nicely fills out the demonstration of the primary synthesis techniques implemented in AMY, after subtractive (the Juno-6 patches) and FM (the DX7 patches).

## The sound of a piano

Here's an example of 5 notes played on a real piano:

https://github.com/user-attachments/assets/a8851901-a46e-42a5-a612-79bd5b6112be

![piano_example_original](https://github.com/user-attachments/assets/852a57f2-745d-4c20-86a0-752112318973)

This clip starts with a D4 note played at three loudnesses - _pp_, _mf_, and _ff_.  These are followed by a D2 (two octaves lower), and a D6 (two octaves higher).
(I made this example by adding together isolated recordings of single piano notes from the [University of Iowa Electronic Music Studios Instrument Samples](https://theremin.music.uiowa.edu/mispiano.html), which are the basis of the AMY piano.  I combined them with the code in [make_piano_examples.ipynb](../experiments/make_piano_examples.ipynb).)

Some things to notice:
* Piano sounds consist of very strong, stable sets of harmonics (fixed-frequency Fourier components), visible as horizontal lines in the spectrogram.  The harmonics appear mostly uniformly-spaced (as expected for a Fourier series of a periodic signal) (although there are a few extra components e.g. around 4.5 kHz at 0.8 seconds for the _ff_ D4).
* Each harmonic starts at its peak amplitude, then decays away.  The higher harmonics decay more quickly.  Higher notes, whose harmonics are naturally all at higher frequencies, die away more quickly than lower notes.
* The first three notes have the same pitch, so the harmonics have the same frequencies.  However, as the note gets louder, not only do the lower harmonics grow in intensity (brighter color), we also see additional higher harmonics appearing.  This is the vital "brightening" of piano sounds as the strike strength increases.
* The D2 is too low for the harmonics to be resolved in this spectrogam, but we can see a complex pattern of time modulation in its energy.  We can also see a complex pattern of per-harmonic modulations in the D6.

## Synthesizing piano sounds

Electronic musical instruments have always taken inspiration from their acoustic forbears, and most electronic keyboards will attempt to simulate a piano.  The Roland Juno-60 included a preset called Piano, which we can deploy with `load_patch=7`:
```
amy.send(voices='0,1', load_patch=7)
amy.send(voices='0', note=74, vel=0.05)
amy.send(voices='0', note=74, vel=0.63)
amy.send(voices='0', note=74, vel=1.0)
amy.send(voices='0', note=50, vel=0.6)
amy.send(voices='1', note=98, vel=1.0)
amy.send(voices='0,1', vel=0)
```

https://github.com/user-attachments/assets/06d3d8cb-5338-4da5-9bba-9ec14044423c

![piano_example_juno60](https://github.com/user-attachments/assets/75d30548-97db-45f7-9037-7536fb865e12)

This synthetic piano gets the stable harmonic structure and steady decay of each note, but there's no change in timbre with the different note velocities; every harmonic gets louder by the same factor.  (In fact, the Juno-60 was not velocity sensitive, but its usual practice to scale the whole note in proportion to velocity). There's no complexity to the harmonic decays, they are uniformly monotonic.  And the overall note decay time doesn't vary with the pitch.

The DX7 similarly provides a number of presets claiming to be pianos:
```
amy.send(voices='0,1', load_patch=137)
amy.send(voices='0', note=50, vel=0.05)
amy.send(voices='0', note=50, vel=0.63)
amy.send(voices='0', note=50, vel=1.0)
amy.send(voices='0', note=26, vel=0.6)
amy.send(voices='1', note=74, vel=1.0)
amy.send(voices='0,1', vel=0)
```

https://github.com/user-attachments/assets/25d5b663-a708-463b-ab35-3efeb0ab6d63

![piano_example_dx7](https://github.com/user-attachments/assets/67c8ccd8-229d-44a6-bbf1-c7b4dd0418e8)

This sounds more like a DX7 than any acoustic instrument.  It does manage to bring some modulation on top of the decays of the harmonics (visible as gaps in the horizontal lines) but is not very convincing.

## Additive synthesis

The horizontal lines in the spectrogram are simply sinusoids at fixed frequencies with slowly-varying amplitudes; the essesnce of Fourier analysis is that any periodic signal can be built up by summing sinusoids at integer multiples of the fundamental frequency (the lowest sinusoid).  We can use this directly to synthesize musical sounds, so-called "additive synthesis", and AMY was originally designed for this very purpose.  We use one oscillator for each harmonic, and set up its amplitude envelope to be a copy of the corresponding harmonic in a real piano signal.  (I wrote code to analyze the UIowa piano sounds into harmonic envelopes in [piano_heterodyne.ipynb](../experiments/piano_heterodyne.ipynb).)

We can set up a `PARTIALS` patch with independent per-harmonic envelopes and play those:
```
import tulip_piano  # Make sure you have piano-params.json too.
amy.send(store_patch='1024,' + tulip_piano.patch_string)
amy.send(voices='0,1', load_patch=1024)
tulip_piano.init_piano_voice(tulip_piano.num_partials, voices='0,1')
tulip_piano.setup_piano_voice_for_note_vel(note=60, vel=80, voices='0,1')
amy.send(voices='0', note=62, vel=0.05)
amy.send(voices='0', note=62, vel=0.63)
amy.send(voices='0', note=62, vel=1.0)
amy.send(voices='0', note=38, vel=0.6)
amy.send(voices='1', note=86, vel=1.0)
amy.send(voices='0,1', vel=0)
```
The setup here is a bit complicated - we're setting 20 breakpoints independently for 20 harmonics with data read from the `piano-params.json` file written by `piano_heterodyne.ipynb` - which is why I'm using the functions from [`tulip_piano.py`](../experiments/tulip_piano.py).

https://github.com/user-attachments/assets/2176741f-13e5-46c4-b34f-b2f15e3e1ec6

![piano_example_fixed](https://github.com/user-attachments/assets/bb272194-29b1-43a1-9629-d4a521ca4276)

The _mf_ D4 note now sounds quite realistic, because it's a reasonably accurate reproduction of the original recording.  However, we're still simply scaling its overall magnitude in to get different veloicities.  And when we change the pitch, we just squeeze or stretch the harmonics (and hence the notes' spectral envelopes), which is not at all realistic sounding.

Instead, we need to analyze many different real piano samples, then interpolate them to get the actual envelopes we synthesize.  The UIowa samples provide recordings of all 88 notes on the piano they sampled, at three strike strengths.  We *could* include harmonic data for all of them, but adjacent notes are quite similar, so instead we encode 3 notes per octave (C, E, and Ab) and interpolate the 3 semitones between each adjacent pair.  (We currently store only 7 octaves, C1 to Ab7, so 21 pitches).

For velocity, we have no choice but to interpolate, since the three recorded strikes do not provide enough expressivity for performance.  We analyze all three (for each pitch stored, so 63 notes total).

Playing a note, then, involves interpolating between *four* of the stored harmonic envelope sets (recall that each set consists of 20 breakpoints for up to 20 harmonics): To synthesize the D4 at, say, velocity 90, we use C4 at _mf_ and _ff_ (which I interpreted as velocities 80 and 120) as well as E4 _mf_ and _ff_.  By doing this interpolation separately for every `(note, velocity)` event, we get a much richer range of tones:
```
# We use the tulip_piano setup of the previous example, but now we have a special function
# to handle adjusting all the harmonic envelopes for each distinct note-on event:
tulip_piano.piano_note_on(voices='0', note=62, vel=0.05)
tulip_piano.piano_note_on(voices='0', note=62, vel=0.63)
tulip_piano.piano_note_on(voices='0', note=62, vel=1.0)
tulip_piano.piano_note_on(voices='0', note=38, vel=0.6)
tulip_piano.piano_note_on(voices='1', note=86, vel=1.0)
tulip_piano.piano_note_on(voices='0,1', vel=0)
```

https://github.com/user-attachments/assets/c1feccc0-7559-4fbd-b38b-d60b6679644c

![piano_example_interpolated](https://github.com/user-attachments/assets/d6e23607-2c52-40e4-ab57-573f71eff9c2)

This recovers both the spectral complexity of the original piano notes, *and* the variation of the spectrum both across the keyboard range and across strike intensities.  The spectrogram of the original recordings is repeated below for comparison.

![piano_example_original](https://github.com/user-attachments/assets/cedce885-f768-4a0d-be93-2aa6eefbd3d8)

While there are plenty of details that have not been exactly preserved (most notably the noisy "clunk" visible around each onset of the recordings, but also the cutoff at 20 harmonics, which loses a lot of high-frequency for the low note), this synthesis just feels much, much more realistic and "acoustic" than any of the previous syntheses.


