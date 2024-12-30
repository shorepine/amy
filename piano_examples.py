import sys
import os

import numpy as np
import scipy.io.wavfile as wav

import amy

from experiments import tulip_piano

import scipy.io.wavfile as wav





def wavwrite(filename, data, samplerate=44100, force_mono=True):
  """Write a waveform to a WAV file."""
  if force_mono and len(data.shape) > 1:
    data = np.mean(data, axis=-1)
  wav.write(filename, samplerate, (32768.0 * data).astype(np.int16))


def tulip_piano_example(filename='piano_example_additive.wav', note_offset=-12):
    # Allocate voices
    amy.restart()
    amy.send(time=0, volume=5)
    amy.send(store_patch='1024,' + tulip_piano.patch_string)
    amy.send(time=0, voices='0,1,2', load_patch=1024)
    for voice in ['0', '1', '2']:
      tulip_piano.init_piano_voice(tulip_piano.num_partials, voices=voice, time=0)

    base_note = 74 + note_offset
      
    tulip_piano.setup_piano_voice_for_note_vel(base_osc=0, note=base_note, vel=50, voices='0', time=40)
    amy.send(time=50, voices='0', note=60, vel=1)
    amy.send(time=440, voices='0', note=60, vel=0)
    tulip_piano.setup_piano_voice_for_note_vel(base_osc=0, note=base_note, vel=90, voices='0', time=440)
    amy.send(time=450, voices='0', note=60, vel=1)
    amy.send(time=840, voices='0', note=60, vel=0)
    tulip_piano.setup_piano_voice_for_note_vel(base_osc=0, note=base_note, vel=110, voices='0', time=840)
    amy.send(time=850, voices='0', note=60, vel=1)
    amy.send(time=1200, voices='0', note=60, vel=0)
    tulip_piano.setup_piano_voice_for_note_vel(base_osc=0, note=base_note - 24, vel=80, voices='1', time=1490)
    amy.send(time=1500, voices='1', note=60, vel=1)
    tulip_piano.setup_piano_voice_for_note_vel(base_osc=0, note=base_note + 24, vel=120, voices='2', time=2090)
    amy.send(time=2100, voices='2', note=60, vel=1)
    amy.send(time=3000, voices='1', note=60, vel=0)
    amy.send(time=3000, voices='2', note=60, vel=0)

    samples = amy.render(3.3)

    #amy.write(samples, filename)
    wavwrite(filename, samples)
    print('Wrote', len(samples), 'samples to', filename)


def piano_example(patch_num=7, note_offset=0, filename='piano_examples.wav', volume=5):
    amy.restart()
    amy.send(time=0, volume=volume)

    amy.send(time=0, voices="0,1,2,3", load_patch=patch_num)
    amy.send(time=50, voices="0", note=note_offset + 72, vel=0.05)
    amy.send(time=450, voices="0", note=note_offset + 72, vel=0.4)
    amy.send(time=850, voices="0", note=note_offset + 72, vel=1.0)
    amy.send(time=1200, voices="0", note=note_offset + 72, vel=0)
    amy.send(time=1500, voices="1", note=note_offset + 48, vel=.6)
    amy.send(time=2100, voices="2", note=note_offset + 96, vel=.6)
    amy.send(time=3000, voices="1", note=note_offset + 48, vel=0)
    amy.send(time=3000, voices="2", note=note_offset + 96, vel=0)

    samples = amy.render(3.3)

    #amy.write(samples, filename)
    wavwrite(filename, samples)
    print('Wrote', len(samples), 'samples to', filename)


def main(argv):

  piano_example(patch_num=7, note_offset=2, volume=10, filename='piano_example_juno_patch_7.wav')
  piano_example(patch_num=137, note_offset=-22, volume=25, filename='piano_example_dx7_patch_137.wav')
  piano_example(patch_num=256, note_offset=-24, volume=25, filename='piano_example_dpwe_piano.wav')
  tulip_piano_example(filename='piano_example_additive.wav')
  
  print("done.")


if __name__ == "__main__":
  main(sys.argv)

  
