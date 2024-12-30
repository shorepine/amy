# The AMY Additive Piano Voice

The piano is a versatile and mature instrument.  One reason for its popularity is its wide timbral range; in fact, its main innovation was the ability to play notes both loud and soft, hence "[Pianoforte](https://en.wikipedia.org/wiki/Piano#History)" (its full original name).

To make AMY into a truly general-purpose music synthesizer, we wanted to add a good piano voice.  But it's not simple to synthesize a good piano voice.  This page explains some of what makes piano synthesis challenging, and how we addressed it.  Our approach was to use [additive synthesis](), which nicely fills out the demonstration of the primary synthesis techniques implemented in AMY, after subtractive (the Juno-6 patches) and FM (the DX7 patches).

## The sound of a piano

Here's an example of 5 notes played on a real piano:

![piano_example_original](https://github.com/user-attachments/assets/852a57f2-745d-4c20-86a0-752112318973)


https://github.com/user-attachments/assets/a8851901-a46e-42a5-a612-79bd5b6112be


This clip starts with a D4 note played at three loudnesses - _pp_, _mf_, and _ff_.  These are followed by a D2 (two octaves lower), and a D6 (two octaves higher).
(I made this example by adding together isolated recordings of single piano notes from the [University of Iowa Electronic Music Studios Instrument Samples](https://theremin.music.uiowa.edu/mispiano.html), which are the basis of the AMY piano.  I combined them with the code in [make_piano_examples.ipynb](../experiments/make_piano_examples.ipynb).)
