#!/usr/bin/env python3
# examples.py
# sound examples and patch examples

import amy
from time import sleep


class Patch:
    """
        Provides a collection of illustrations of different voice configurations with AMY.

        You use them like this:

        amy.send(synth=0, num_voices=4, patch=patches.filter_bass())
    """
    next_patch_number = 1024

    @classmethod
    def _new_patch_number(cls):
        # just keep a global patch num around for these patches
        new_num = cls.next_patch_number
        cls.next_patch_number = cls.next_patch_number + 1
        return new_num

    @classmethod
    def reset(cls):
        cls.next_patch_number = 1024

    def __init__(self, **kwargs):
        self.patch_number = Patch._new_patch_number()
        self.setup(**kwargs)

    def __int__(self):
        # When amy.message() tries to convert this into an int, just return the patch number.
        return self.patch_number

    def setup(self, **kwargs):
        raise ValueError('Subclasses of Patch must define a setup(self, **kwargs) method.')


class simple_sine(Patch):
    def setup(self, **kwargs):
        amy.send(patchr=self.patch_number,
                 wave=amy.SINE, bp0="10,1,240,0.7,500,0",
                 **kwargs)

class filter_bass(Patch):
    def setup(self, **kwargs):
        amy.send(patch=self.patch_number,
                 osc=0, filter_freq="100,0,0,5", resonance=5, wave=amy.SAW_DOWN,
                 filter_type=amy.FILTER_LPF, bp0="0,1,1000,0,100,0",
                 **kwargs)

class amp_lfo(Patch):
    def setup(self, **kwargs):
        # Not clear what to do with kwargs.  They probably don't want to apply to both oscs.
        amy.send(patch=self.patch_number,
                 osc=1, wave=amy.SINE, amp=0.50, freq=1.5,
                 **kwargs)
        amy.send(patch=self.patch_number,
                 osc=0, wave=amy.PULSE, bp0="150,1,1850,0.25,250,0", amp="0,0,1,1,0,1", mod_source=1,
                 **kwargs)

class pitch_lfo(Patch):
    def setup(self, **kwargs):
        amy.send(patch=self.patch_number,
                 osc=1, wave=amy.SINE, amp=0.50, freq=0.25,
                 **kwargs)
        amy.send(patch=self.patch_number,
                 osc=0, wave=amy.PULSE, bp0="150,1,250,0,0,0", freq="261.63,1,0,0,0,1", mod_source=1,
                 **kwargs)

class bass_drum(Patch):
    def setup(self, **kwargs):
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        amy.send(patch=self.patch_number,
                 osc=1, wave=amy.SINE, amp=0.50, freq=0.25, phase=0.5,
                 **kwargs)
        amy.send(patch=self.patch_number,
                 osc=0, wave=amy.SINE, bp0="0,1,500,0,0,0", freq="261.63,1,0,0,0,1",  mod_source=1,
                 **kwargs)

class noise_snare(Patch):
    def setup(self, **kwargs):
        amy.send(patch=self.patch_number,
                 osc=0, wave=amy.NOISE, bp0="0,1,250,0,0,0",
                 **kwargs)

class closed_hat(Patch):
    def setup(self, **kwargs):
        amy.send(patch=self.patch_number,
                 osc=0, wave=amy.NOISE, bp0="5,1,30,0,0,0", filter_type=amy.FILTER_HPF, filter_freq=6000,
                 **kwargs)




"""
    Various python examples of using AMY, including the port of all the C examples.c 
"""


# Plays the example patches defined in this file
def play_example_patches(wait=1, **kwargs):
    from . import examples
    try:
        patchClasses = examples.Patch.__subclasses__()
    except AttributeError:
        # micropython does not have __subclasses__
        patchClasses = [
            examples.simple_sine,
            examples.filter_bass,
            examples.amp_lfo,
            examples.pitch_lfo,
            examples.bass_drum,
            examples.noise_snare,
            examples.closed_hat,
        ]
    for patchClass in patchClasses:
        print("Patch", patchClass.__name__)
        send(synth=0, num_voices=1, patch=patchClass())
        time.sleep(wait/4.0)            
        send(synth=0, note=50, vel=1, **kwargs)
        time.sleep(wait)
        send(synth=0, vel=0)
        time.sleep(wait/4.0)

# Plays the baked in Juno, DX7 and piano patches
def play_baked_patches(wait=1, patch_total = 256, **kwargs):
    import random
    patch_count = 0
    while True:
        patch = random.randint(0,256) #patch_count % patch_total
        print("Sending patch %d" % patch)
        send(synth=0, num_voices=1, patch=patch)
        time.sleep(wait/4.0)            
        send(synth=0, note=50, vel=1, **kwargs)
        time.sleep(wait)
        send(synth=0, vel=0)
        time.sleep(wait/4.0)

def example_multimbral_synth():
    amy.send(patch=4, num_voices=6, synth=1)  # juno-6 preset 4 on MIDI channel 1
    amy.send(patch=129, num_voices=2, synth=2) # dx7 preset 1 on MIDI channel 2
    amy.send(synth=3, num_voices=4, patch=filter_bass()) # filter bass user patch on MIDI channel 3
    amy.send(synth=4, num_voices=4, patch=pitch_lfo()) # pitch LFO user patch on MIDI channel 4

def example_reset(start=0):
    amy.send(osc=0, reset=amy.RESET_ALL_OSCS, time=start)

def example_voice_alloc():
    # alloc 2 juno voices, then try to alloc a dx7 voice on voice 0
    amy.send(patch=1, voices=[0, 1])
    sleep(0.25)

    amy.send(patch=131, voices=[0])
    sleep(0.25)

    # play the same note on both
    amy.send(vel=1, note=60, voices=[0])
    sleep(2)

    amy.send(vel=1, note=60, voices=[1])
    sleep(2)

    # now try to alloc voice 0 with a juno, should use oscs 0-4 again
    amy.send(patch=2, voices=[0])
    sleep(0.25)

def example_voice_chord(patch=0):
    amy.send(patch=patch, voices=[0, 1, 2])
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
    amy.send(patch=patch, num_voices=3, synth=0)
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

    amy.send(synth=1, num_voices=4, patch=patch)
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
        amy.send(patch=i, voices=[0])
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
        amy.send(voices=[i], patch=128+i)
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

    amy.send(patch=number, reset=amy.RESET_PATCH)

    amy.send(patch=number, osc=0, wave=amy.SAW_DOWN,
            chained_osc=1, bp0="0,1,1000,0.1,200,0")

    amy.send(patch=number, osc=1, wave=amy.SINE,
            freq=[131.0], bp0="0,1,500,0,200,0")

    amy.send(synth=0, num_voices=4, patch=number)
    sleep(.1)
    amy.send(synth=0, note=60, vel=1.0)
    sleep(.3)
    amy.send(synth=0, note=64, vel=1.0)
    sleep(.2)
    amy.send(synth=0, note=67, vel=1.0)
    sleep(.3)
    amy.send(synth=0, vel=0.0) 
