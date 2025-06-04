# patch.py
import amy

class Patch:
    patch_num = 1024
    def new_patch_num():
        # just keep a global patch num around for these patches
        Patch.patch_num = Patch.patch_num + 1
        return Patch.patch_num-1

    def synth_with_patch_func(func,synth=0):
        p = func()
        amy.send(synth=0, patch_number=p)
        return synth
        
    def simple_sine(**kwargs):
        p = Patch.new_patch_num()
        amy.send(osc=0, wave=amy.SINE, bp0="10,1,240,0.7,500,0", patch_number=p,**kwargs)
        return p

    def filter_bass(**kwargs):
        p = Patch.new_patch_num()
        amy.send(osc=0, filter_freq="100,0,0,5", resonance=5, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, bp0="0,1,1000,0,100,0", patch_number=p, **kwargs)
        return p

    def amp_lfo(**kwargs):
        p = Patch.new_patch_num()
        amy.send(osc=1, wave=amy.SINE, amp=0.50, freq=1.5, patch_number = p , **kwargs)
        amy.send(osc=0, wave=amy.PULSE, bp0="150,1,1850,0.25,250,0", amp="0,0,1,1,0,1", mod_source=1, patch_number= p, **kwargs)
        return p

    def pitch_lfo(**kwargs):
        p = Patch.new_patch_num()
        amy.send(osc=1, wave=amy.SINE, amp=0.50, freq=0.25, patch_number = p, **kwargs)
        amy.send(osc=0, wave=amy.PULSE, bp0="150,1,250,0,0,0", freq="261.63,1,0,0,0,1", mod_source=1, patch_number=p, **kwargs)
        return p

    def bass_drum(**kwargs):
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        p = Patch.new_patch_num()
        amy.send(osc=1, wave=amy.SINE, amp=0.50, freq=0.25, phase=0.5, patch_number=p, **kwargs)
        amy.send(osc=0, wave=amy.SINE, bp0="0,1,500,0,0,0", freq="261.63,1,0,0,0,1",  mod_source=1, patch_number=p, **kwargs)
        return p

    def noise_snare(**kwargs):
        p = Patch.new_patch_num()
        amy.send(osc=0, wave=amy.NOISE, bp0="0,1,250,0,0,0",patch_number=p,  **kwargs)
        return p

    def closed_hat(**kwargs):
        p = Patch.new_patch_num()
        amy.send(osc=0, wave=amy.NOISE, bp0="25,1,50,0,0,0", patch_number=p, **kwargs)
        return p
