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


def init_piano_voices():
    amy.send(store_patch='1024,' + tulip_piano.patch_string)
    amy.send(time=0, voices='0,1,2', load_patch=1024)
    for voice in ['0', '1', '2']:
      tulip_piano.init_piano_voice(tulip_piano.num_partials, voices=voice, time=0)


def tulip_piano_example(filename='piano_example_additive.wav', note_offset=-12):
    # Allocate voices
    amy.restart()
    amy.send(time=0, volume=5)

    init_piano_voices()

    base_note = 74 + note_offset
      
    tulip_piano.piano_note_on(voices='0', vel=0.4, note=base_note, time=50)
    tulip_piano.piano_note_on(voices='0', vel=0, time=440)
    tulip_piano.piano_note_on(voices='0', vel=0.7, note=base_note, time=450)
    tulip_piano.piano_note_on(voices='0', vel=0, time=840)
    tulip_piano.piano_note_on(voices='0', vel=1.0, note=base_note, time=850)
    tulip_piano.piano_note_on(voices='0', vel=0, time=1490)
    tulip_piano.piano_note_on(voices='1', vel=0.6, note=base_note - 24, time=1500)
    tulip_piano.piano_note_on(voices='2', vel=1.0, note=base_note + 24, time=2100)
    tulip_piano.piano_note_on(voices='1', vel=0, time=3000)
    tulip_piano.piano_note_on(voices='2', vel=0, time=3000)

    samples = amy.render(3.3)

    #amy.write(samples, filename)
    wavwrite(filename, samples)
    print('Wrote', len(samples), 'samples to', filename)


def piano_example(patch_num=7, base_note=72, filename='piano_examples.wav', volume=5,
                  send_command=amy.send, init_command=lambda: None):
    amy.restart()
    amy.send(time=0, volume=volume)

    #amy.send(time=0, voices="0,1,2,3", load_patch=patch_num)
    init_command()

    send_command(time=50, voices="0", note=base_note, vel=0.05)
    send_command(time=435, voices="0", note=base_note, vel=0)
    send_command(time=450, voices="0", note=base_note, vel=0.63)
    send_command(time=835, voices="0", note=base_note, vel=0)
    send_command(time=850, voices="0", note=base_note, vel=1.0)
    send_command(time=1485, voices="0", note=base_note, vel=0)
    send_command(time=1500, voices="1", note=base_note - 24, vel=0.6)
    send_command(time=2100, voices="2", note=base_note + 24, vel=1.0)
    send_command(time=3000, voices="1", note=base_note - 24, vel=0)
    send_command(time=3000, voices="2", note=base_note + 24, vel=0)

    samples = amy.render(3.3)

    #amy.write(samples, filename)
    wavwrite(filename, samples)
    print('Wrote', len(samples), 'samples to', filename)


def init_piano_voices():
    amy.send(store_patch='1024,' + tulip_piano.patch_string)
    amy.send(time=0, voices='0,1,2', load_patch=1024)
    tulip_piano.init_piano_voice(tulip_piano.num_partials, voices='0,1,2', time=0)
    # additive_interpolated overwrites these settings before each note,
    # but pre-configure each note to C4.mf for additive_fixed.
    tulip_piano.setup_piano_voice_for_note_vel(note=60, vel=80, voices='0,1,2')


def main(argv):

  piano_example(base_note=74, volume=10, filename='piano_example_juno_patch_7.wav',
                init_command=lambda: amy.send(time=0, voices='0,1,2', load_patch=7))
  piano_example(base_note=50, volume=25, filename='piano_example_dx7_patch_137.wav',
                init_command=lambda: amy.send(time=0, voices='0,1,2', load_patch=137))
  piano_example(base_note=62, volume=5,
                filename='piano_example_additive_fixed.wav',
                init_command=init_piano_voices)
  piano_example(base_note=62,
                filename='piano_example_additive_interpolated.wav',
                init_command=init_piano_voices,
                send_command=tulip_piano.piano_note_on)
  
  print("done.")


if __name__ == "__main__":
  main(sys.argv)

  
