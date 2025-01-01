# The AMY Additive Piano Voice

The piano is a versatile and mature instrument.  One reason for its popularity is its wide timbral range; in fact, its main innovation was the ability to play notes both loud and soft, hence "[Pianoforte](https://en.wikipedia.org/wiki/Piano#History)" (its full original name).

To make AMY into a truly general-purpose music synthesizer, we wanted to add a good piano voice.  But it's not simple to synthesize a good piano voice.  This page explains some of what makes piano synthesis challenging, and how we addressed it.  Our approach was to use [additive synthesis](https://en.wikipedia.org/wiki/Additive_synthesis), which nicely fills out the demonstration of the primary synthesis techniques implemented in AMY, after subtractive (the Juno-6 patches) and FM (the DX7 patches).

## The sound of a piano

Here's an example of 5 notes played on a real piano:

https://github.com/user-attachments/assets/a8851901-a46e-42a5-a612-79bd5b6112be

![uiowa](https://github.com/user-attachments/assets/67f6a35c-bbc9-47bb-9440-41c85c9ba8b4)

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

![juno](https://github.com/user-attachments/assets/11d61f1b-1c04-4740-a97c-b60f9b62369d)

This synthetic piano gets the stable harmonic structure and steady decay of each note, but there's no change in timbre with the different note velocities; every harmonic gets louder by the same factor.  (In fact, the Juno-60 was not velocity sensitive, but its usual practice to scale the whole note in proportion to velocity). There's no complexity to the harmonic decays, they are uniformly monotonic.  And the overall note decay time doesn't vary with the pitch.

The DX7 similarly provides a number of [presets claiming to be pianos](https://www.synthmania.com/dx7.htm), including 135-137 (in our numbering which starts at 128):
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

![dx7](https://github.com/user-attachments/assets/5e1f0ec5-beac-4476-86e2-27f24e3c0bc5)

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

![fixed](https://github.com/user-attachments/assets/37f45670-fb31-4672-a354-6364f42113a9)

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

![interpolated](https://github.com/user-attachments/assets/bcdf9711-3186-45cd-8c06-937a67b10afa)

This recovers both the spectral complexity of the original piano notes, *and* the variation of the spectrum both across the keyboard range and across strike intensities.  The spectrogram of the original recordings is repeated below for comparison.

![uiowa](https://github.com/user-attachments/assets/6337d2b1-cee1-4636-bf96-ce4b7ab26544)

While there are plenty of details that have not been exactly preserved (most notably the noisy "clunk" visible around each onset of the recordings, but also the cutoff at 20 harmonics, which loses a lot of high-frequency for the low note), this synthesis just feels much, much more realistic and "acoustic" than any of the previous syntheses.

# Extracting harmonic envelopes

How exactly to we capture the envelopes for each harmonic in our additive model?  In principle, this is simply a matter of dissecting a spectrogram of the individual note (like the images above) to measure the intensity of each individual horizontal line.  In practice, however, I wanted something with finer resolution in time and frequency to obtain very accurate parameters.  I used [heterodyne analysis](https://en.wikipedia.org/wiki/Heterodyne), which I'll now explain.

The [Fourier series](https://en.wikipedia.org/wiki/Fourier_series) expresses a periodic waveform as the sum of sinusoids at multiples of the fundamental frequency ("harmonics"), with the phases as amplitudes of each harmonic determining the resulting waveform.  It's mathematically convenient to describe these sinusoids with [complex exponentials](https://medium.com/@theorose49/meaning-of-complex-exponential-for-electric-engineering-68de0625603f), essentially sinusoids with two-dimensional values at each moment, where the real axis part is our regular sine, and the imaginary (2nd) axis part is the cosine (sine phase-shifted by 90 degrees):


![1*t6wVEZv6CkhACEyY2pFe2A](https://github.com/user-attachments/assets/556d8102-b741-4942-82f7-832878833097)


Each sinusoid is constructed as the sum of a pair of complex exponentials with positive and negative frequencies; the imaginary parts cancel out leaving the real sinusoid.  Thus, the full Fourier spectrum of a real signal has mirror-image positive and negative parts (although we generally only plot the positive half):

![download-2](https://github.com/user-attachments/assets/e6c553f8-a802-4324-a75f-22ac96fb4c0e)

The neat thing about this complex-exponential Fourier representation is that multiplying a signal by a complex exponential is a pure shift in the Fourier spectrum domain.  So, by multiplying by the complex exponential at the negative of a particular harmonic frequency, we can shift that harmonic down to 0 Hz (d.c.).  Then, by low-pass filtering (i.e., smoothing) that waveform, we can cancel out all the other harmonics leaving only the envelope of the one harmonic we are targeting.  (By smoothing over a window that exactly matches the fundamental period, we can exactly cancel all the other sinusoid components because they will complete a whole number of cycles in that period).  See [make_piano_examples.ipynb](../experiments/make_piano_examples.ipynb) for how these figures were prepared:

![download-1](https://github.com/user-attachments/assets/183f00b6-b507-44c9-9d36-96a1c5382912)

![download](https://github.com/user-attachments/assets/7070eb86-c184-411e-9833-402a867ca7c8)

## Finding harmonic frequencies and piano inharmonicity

The heterodyne extraction allows us to extract sample-precise envelopes for harmonics at specific frequencies, but we need to give it the exact frequencies we want it to extract.  Again, in principle, this is straightforward: The harmonics should occur at integer-multiples of the fundamental frequency, and if the piano is tuned right, we already know the [fundamental frequences for each note](https://en.wikipedia.org/wiki/Piano_key_frequencies).

It turns out, however, that piano notes are not perfectly harmonic: They can be well modeled as the sum of fixed-frequency sinusoids, but those sinusoids are not exact integer multiples of a common fundamental.  This is a consequence of the stiffness of the steel strings (I'm told!) which makes the speed of wave propagation down the strings higher for higher harmonics.  This [piano inharmonicity](https://en.wikipedia.org/wiki/Inharmonicity#Pianos) has been credited with some of the "warmth" of piano sounds, and is something we want to preserve in our synthesis.  In order to precisely extract each harmonic for each note, we need to individually estimate the inharmonicity coefficient for each string (because the strings are all different thicknesses, the inharmonicity varies across the range of the piano).

I estimated the inharmonicity by extracting very precise peak frequencies from a long Fourier transform of the piano note, then fitting the theoretical equation $f_n \propto n \sqrt{1 + B n^2}$ (from [this StackExchange explanation](https://physics.stackexchange.com/questions/268568/why-are-the-harmonics-of-a-piano-tone-not-multiples-of-the-base-frequency)) to those values.

![download-4](https://github.com/user-attachments/assets/c8455d3d-f5bf-4010-a6f9-f1c0b3e158b8)

![download-5](https://github.com/user-attachments/assets/9ee92a7d-9464-4ac3-b866-bbbade5bcf93)

These plots come from the "Inharmonicity estimation" part of [piano_heterodyne.ipynb](../experiments/piano_heterodyne.ipynb).  Estimating the inharmonicity for each note allowed me to extract harmonic envelopes precisely corresponding to each specific harmonic of each piano note.  This was important because when we are interpolating between different harmonic envelopes, we want to be sure we're looking at the same harmonic in both notes.

## Describing envelopes

We can extract sample-accurate envelopes for as many harmonics as we want for each of the real piano note recordings we have.  But to turn them into a practical additive-synthesis instrument on AMY we need to think about efficiency.  Right now, the harmonic envelopes are represented as 44,100 values per second of recording, and we're modeling something like the first 5 seconds of each note (the low notes can easily be 20 seconds long before they decay into oblivion).  But the AMY envelopes can only handle up to 24 breakpoints (it used to be 8 before I changed it to serve this project!) so we need some way to summarize the envelopes in a smaller number of straight-line segments (which is what the envelope breakpoints define).

Additive synthesis of individual harmonics is so powerful because sounds like piano notes are so well described by a small number of harmonics with constant or slowly-changing frequency and amplitude.  Looking at the four harmonic envelopes extracted in the previous section, it's obvious that the long tails are almost entirely smooth and could be described with a small number of parameters.  The initial portions are more complex, however, including going up and down.  Reproducing them will need more breakpoints, and it's not immediately obvious how to choose those breakpoints to give the best approximation.

I spent a while trying to come up with ad-hoc algorithms to accurately match an arbitrary envelope with a few line segments - see the "Adaptive magnitude sampling" section of [piano_heterodyne.ipynb](../experiments/piano_heterodyne.ipynb).  The goal was to choose breakpoint times (and values) that preserved the most detail of the original envelope, even though I couldn't define exactly what I wanted.  However, the outcome was naturally that there would be different breakpoint times for each note, which made interpolation between different notes very problematic - can we interpolate the breakpoint times too?  (I tried it, and it fared badly in practice because of wildly varying allocations of breakpoints to different time regions).

In the end I gave up and used a much simpler strategy of predefining a set of breakpoint sample times that are constant for all notes.  Although this is inevitably sub-optimal for any given note, it gives a much more solid foundation for interpolating between notes.  And it turns out that the accuracy lost in modeling the envelopes doesn't seem to be too important perceptually.  To respect the idea that the envelopes have an initial period of considerable detail, followed by a longer, smoother evolution, I used exponential spacing of the sampling times.  Specifically each envelope is described by samples at 20 instants: 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096 milliseconds after onset.  After the initial value, these are in a pattern of $2^n$ and $1.5 * 2^n$ milliseconds, giving the exponential spacing.  This plot shows the original envelope with the breakpoint approximation superimposed, on two timescales - the first 150 msec on the left, and the full 4.1 sec on the right (with the 150 msec edge of the left plot shown as a dotted line).

![download-3](https://github.com/user-attachments/assets/eb700d34-9100-4625-b19b-8f91f8cd3154)

Although a bunch of detail has been lost, it still provides a suitably complex character to the resyntheses.

These 20 envelope samples, along with the measured center frequency, are then the description for each of the up-to 20 harmonics describing each of the (7 octaves x 3 chroma x 3 strike strengths) notes, or about 1200 envelopes total.  This is the data stored in `piano-params.json` and read by `tulip_piano.py`.

