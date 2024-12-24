"""Piano notes generated on amy/tulip."""

# Uses the partials amplitude breakpoints and residual written by piano-partials.ipynb.

import json
import time
import tulip
import amy
import midi

#params_file = 'track02-C4-bps.json'
#residu_file = 'M1_Piano_C3_hprModel_residual_0.25s.wav'
#residu_file = 'track02-C4-resid-0.25s.wav'

params_file = 'Piano.ff.D5.json'
residu_file = 'resid-0.25s.wav'

# Read in the params file.
with open(params_file, 'r') as f:
  bp_params = json.load(f)

pcm_patch = tulip.load_sample(residu_file)

def setup_piano(bp_params, pcm_patch=None, voices=None, base_osc=0, base_freq=261.63, stretch_coef=0.038):
  kwargs = {}
  if voices is not None:
    kwargs = {'voices': voices}
  num_partials = len(bp_params)
  amy.send(osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials, amp={'eg0': 0}, **kwargs)

  harm_nums = range(1, num_partials + 1)
  # Quadratic partial stretching
  #harm_freqs = [n * (261.8 + stretch_coef * n * n) for n in harm_nums]
  
  for i in harm_nums:
    # Set up each partial as the corresponding harmonic of the base_freq, with an amplitude of 1/N, 50ms attack, and a decay of 1 sec / N
    f0 = bp_params[i - 1][0]
    bp_vals = bp_params[i - 1][1]
    bp_string = ','.join("%d,%.3f" % (n, max(0, 100 * val - 0.001)) for n, val in bp_vals)
    bp_string += ',200,0'
    #print(bp_string)
    amy.send(osc=base_osc + i, wave=amy.PARTIAL, freq=f0, bp0=bp_string, eg0_type=amy.ENVELOPE_TRUE_EXPONENTIAL, **kwargs)

  if pcm_patch is not None:
    pcm_osc = num_partials + 1
    #pcm_patch = tulip.load_sample(residu_file)
    amy.send(osc=pcm_osc, wave=amy.CUSTOM, patch=pcm_patch, amp=1, freq='0,0,0,0,0,0', **kwargs)
    amy.send(osc=base_osc, chained_osc=pcm_osc, **kwargs)


def piano_note_on(note, vel=1):
  amy.send(osc=pcm_osc, note=patch_note + (note - 60), vel=2*vel)
  amy.send(osc=0, note=note, vel=vel)


#piano_note_on(60, 1)
#time.sleep(1.0)
#piano_note_on(60, 0)

amy.reset()
time.sleep(0.1)  # to let reset happen.
#amy.send(store_patch='1024,v0w10Zv%dw%dZ')
#amy.send(voices='0,1,2,3', load_patch=1024)
num_partials = len(bp_params)
patch_string = 'v0w10Zv%dw%dZ' % (num_partials + 1, amy.PARTIAL)
synth_obj = midi.Synth(num_voices=4, patch_string=patch_string)
#voices = '0,1,2,3'
voices = ",".join([str(a) for a in synth_obj.amy_voice_nums])

print("voices=", voices)

setup_piano(bp_params, pcm_patch, voices=voices)

midi.config.add_synth_object(channel=1, synth_object=synth_obj)


