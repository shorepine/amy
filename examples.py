#!/usr/bin/env python3
# examples.py
# sound examples

import amy
from time import sleep


def example_multimbral_synth():
    import example_patches as patches
    amy.send(patch_number=4, num_voices=6, synth=1)  # juno-6 preset 4 on MIDI channel 1
    amy.send(patch_number=129, num_voices=2, synth=2) # dx7 preset 1 on MIDI channel 2
    amy.send(synth=3, num_voices=4, patch_number=patches.filter_bass()) # filter bass user patch on MIDI channel 3
    amy.send(synth=4, num_voices=4, patch_number=patches.pitch_lfo()) # pitch LFO user patch on MIDI channel 4

def example_reset(start=0):
    amy.send(osc=0, reset=amy.RESET_ALL_OSCS, time=start)

def example_voice_alloc():
    # alloc 2 juno voices, then try to alloc a dx7 voice on voice 0
    amy.send(patch_number=1, voices=[0, 1])
    sleep(0.25)

    amy.send(patch_number=131, voices=[0])
    sleep(0.25)

    # play the same note on both
    amy.send(vel=1, note=60, voices=[0])
    sleep(2)

    amy.send(vel=1, note=60, voices=[1])
    sleep(2)

    # now try to alloc voice 0 with a juno, should use oscs 0-4 again
    amy.send(patch_number=2, voices=[0])
    sleep(0.25)

def example_voice_chord(patch=0):
    amy.send(patch_number=patch, voices=[0, 1, 2])
    sleep(.250)

    amy.send(vel=0.5, voices=[0], note=50)
    sleep(1)

    amy.send(vel=0.5, voices=[1], note=54)
    sleep(1)

    amy.send(vel=0.5, voices=[2], note=56)
    sleep(2)

    amy.send(vel=0, voices=[0, 1, 2])

def example_synth_chord(patch=0):
    # Like example_voice_chord, but use 'synth' to avoid having to keep track of voices
    amy.send(patch_number=patch, num_voices=3, synth=0)
    sleep(1)

    amy.send(vel=0.5, synth=0, note=50)
    sleep(1)

    amy.send(vel=0.5, synth=0, note=54)
    sleep(1)

    amy.send(vel=0.5, synth=0, note=56)
    sleep(1)

    # Voices are referenced only by their note, so have to turn them off individually
    amy.send(vel=0, synth=0, note=50)
    amy.send(vel=0, synth=0, note=54)
    amy.send(vel=0, synth=0, note=56)

def example_sustain_pedal(patch=0):
    # Reset all oscillators first
    amy.send(reset=amy.RESET_ALL_OSCS)

    amy.send(synth=1, num_voices=4, patch_number=patch)
    sleep(0.05)

    amy.send(synth=1, note=76, vel=1.0)

    sleep(0.05)
    amy.send(synth=1, note=76, vel=0)

    sleep(0.05)
    amy.send(synth=1, pedal=127)

    sleep(0.05)
    amy.send(synth=1, note=63, vel=1.0)

    sleep(0.05)
    amy.send(synth=1, note=63, vel=0)

    sleep(0.05)
    amy.send(synth=1, note=67, vel=1.0)

    sleep(0.05)
    amy.send(synth=1, note=67, vel=0)

    sleep(0.05)
    amy.send(synth=1, note=72, vel=1.0)

    sleep(0.05)
    amy.send(synth=1, pedal=0)

    sleep(0.05)
    amy.send(synth=1, note=72, vel=0)

def example_patches():
    for i in range(256):
        amy.send(patch_number=i, voices=[0])
        print(f"sending patch {i}")
        sleep(0.25)

        amy.send(voices=[0], osc=0, note=50, vel=0.5)
        sleep(1)

        amy.send(voices=[0], vel=0)
        sleep(0.25)

        amy.reset()

def example_reverb():
    amy.reverb(2, amy.REVERB_DEFAULT_LIVENESS, amy.REVERB_DEFAULT_DAMPING, amy.REVERB_DEFAULT_XOVER_HZ)

def example_chorus():
    amy.chorus(0.8, amy.CHORUS_DEFAULT_MAX_DELAY, amy.CHORUS_DEFAULT_LFO_FREQ, amy.CHORUS_DEFAULT_MOD_DEPTH)

def example_ks():
    amy.send(vel=1, wave=amy.KS, feedback=0.996, preset=15, osc=0, note=60)

def example_sine():
    amy.send(freq=[440], wave=amy.SINE, vel=1)

def example_multimbral_fm():
    notes = [60, 70, 64, 68, 72, 82]
    for i, note in enumerate(notes):
        # Two amy sends, one to load the patch, one to play it
        amy.send(voices=[i], patch_number=128+i)
        amy.send(voices=[i], note=note, vel=0.5, pan=[i*2])
        sleep(1)


def example_sequencer_drums():
    # Reset all oscillators
    amy.send(reset=amy.RESET_ALL_OSCS)

    amy.send(tempo=120.0)

    # Setup oscs for bd, snare, hat, cow, hicow
    oscs = [0, 1, 2, 3, 4]
    presets = [1, 5, 0, 10, 10]

    for osc, preset in zip(oscs, presets):
        amy.send(osc=osc, wave=amy.PCM, preset=preset)

    # Update high cowbell
    amy.send(osc=4, note=70)

    # Add patterns
    # Hi hat every 1/8th note
    amy.send(sequence=[0, 24, 0], osc=2, vel=2.0)

    # Bass drum every quarter note
    amy.send(sequence=[0, 96, 1], osc=0, vel=1.0)

    # Snare every quarter note, counterphase to BD
    amy.send(sequence=[24, 96, 2], osc=1, vel=1.0)

    # Cow once every other cycle
    amy.send(sequence=[0, 192, 3], osc=3, vel=1.0)

def example_fm():
    amy.reset()
    # Modulating oscillator (op 2)
    amy.send(osc=9, wave=amy.SINE, ratio=1.0,
            amp=[1.0, None, 0, 0])  

    # Output oscillator (op 1)
    amy.send(osc=8, wave=amy.SINE, ratio=0.2,
            amp=[1.0, None, 0, 1.0], 
            bp0="0,1,1000,0,0,0")

    # ALGO control oscillator
    amy.send(osc=7, wave=amy.ALGO, algorithm=1,
            algo_source=[0, 0, 0, 0, 9, 8])  # Only indices 4 and 5 matter here

    # Add a note on event
    sleep(.1)
    amy.send(osc=7, note=60, vel=2.0)

def example_patch_from_events():
    number = 1039

    amy.send(patch_number=number, reset=amy.RESET_PATCH)

    amy.send(patch_number=number, osc=0, wave=amy.SAW_DOWN,
            chained_osc=1, bp0="0,1,1000,0.1,200,0")

    amy.send(patch_number=number, osc=1, wave=amy.SINE,
            freq=[131.0], bp0="0,1,500,0,200,0")

    amy.send(synth=0, num_voices=4, patch_number=number)
    sleep(.1)
    amy.send(synth=0, note=60, vel=1.0)
    sleep(.3)
    amy.send(synth=0, note=64, vel=1.0)
    sleep(.2)
    amy.send(synth=0, note=67, vel=1.0)
    sleep(.3)
    amy.send(synth=0, vel=0.0) 
