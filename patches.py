"""patches.py

This file provides a collection of illustrations of different voice configurations with AMY.

You use them like this:

amy.send(synth=0, num_voices=4, patch=patches.filter_bass())
"""

import amy


class Patch:
    
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
        amy.send(patch_number=self.patch_number,
                 wave=amy.SINE, bp0="10,1,240,0.7,500,0",
                 **kwargs)

class filter_bass(Patch):
    def setup(self, **kwargs):
        amy.send(patch_number=self.patch_number,
                 osc=0, filter_freq="100,0,0,5", resonance=5, wave=amy.SAW_DOWN,
                 filter_type=amy.FILTER_LPF, bp0="0,1,1000,0,100,0",
                 **kwargs)

class amp_lfo(Patch):
    def setup(self, **kwargs):
        # Not clear what to do with kwargs.  They probably don't want to apply to both oscs.
        amy.send(patch_number=self.patch_number,
                 osc=1, wave=amy.SINE, amp=0.50, freq=1.5,
                 **kwargs)
        amy.send(patch_number=self.patch_number,
                 osc=0, wave=amy.PULSE, bp0="150,1,1850,0.25,250,0", amp="0,0,1,1,0,1", mod_source=1,
                 **kwargs)

class pitch_lfo(Patch):
    def setup(self, **kwargs):
        amy.send(patch_number=self.patch_number,
                 osc=1, wave=amy.SINE, amp=0.50, freq=0.25,
                 **kwargs)
        amy.send(patch_number=self.patch_number,
                 osc=0, wave=amy.PULSE, bp0="150,1,250,0,0,0", freq="261.63,1,0,0,0,1", mod_source=1,
                 **kwargs)

class bass_drum(Patch):
    def setup(self, **kwargs):
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        amy.send(patch_number=self.patch_number,
                 osc=1, wave=amy.SINE, amp=0.50, freq=0.25, phase=0.5,
                 **kwargs)
        amy.send(patch_number=self.patch_number,
                 osc=0, wave=amy.SINE, bp0="0,1,500,0,0,0", freq="261.63,1,0,0,0,1",  mod_source=1,
                 **kwargs)

class noise_snare(Patch):
    def setup(self, **kwargs):
        amy.send(patch_number=self.patch_number,
                 osc=0, wave=amy.NOISE, bp0="0,1,250,0,0,0",
                 **kwargs)

class closed_hat(Patch):
    def setup(self, **kwargs):
        amy.send(patch_number=self.patch_number,
                 osc=0, wave=amy.NOISE, bp0="25,1,50,0,0,0",
                 **kwargs)
