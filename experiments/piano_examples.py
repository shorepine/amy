import sys
import os

import numpy as np
import scipy.io.wavfile as wav

import amy
import c_amy as _amy

from experiments import tulip_piano

import scipy.io.wavfile as wav


def wavwrite(filename, data, samplerate=44100, force_mono=True):
  """Write a waveform to a WAV file."""
  if force_mono and len(data.shape) > 1:
    data = np.mean(data, axis=-1)
  wav.write(filename, samplerate, (32768.0 * data).astype(np.int16))


# There's no more time= to schedule notes ahead of time (AMY only schedules
# on ticks now), so this offline render instead renders forward -- in whole
# AMY_BLOCK_SIZE blocks, exactly like amy.render() does -- to each note's
# simulated ms position before sending it, then renders out to the end.
# _blocks_for_ms must be a ceiling, not amy.render()'s truncation: it needs
# the same block amy_sysclock() first reads >= ms on, or every note lands up
# to one block later than intended (see amy/test.py's identical helper).
_frames = []
_blocks_done = 0

def _blocks_for_ms(ms):
  numerator = int(round(ms)) * amy.AMY_SAMPLE_RATE
  denominator = amy.AMY_BLOCK_SIZE * 1000
  return -(-numerator // denominator)  # ceiling division

def _render_to_ms(ms):
  global _blocks_done
  target = _blocks_for_ms(ms)
  while _blocks_done < target:
    _frames.append(np.array(_amy.render_to_list()) / 32768.0)
    _blocks_done += 1


def piano_example(base_note=72, filename='piano_examples.wav', volume=5,
                  note_command=amy.send, init_command=lambda: None):
    global _frames, _blocks_done
    amy.restart()
    _frames = []
    _blocks_done = 0
    amy.send(volume=volume)

    init_command()

    def send_command(ms, **kwargs):
        _render_to_ms(ms)
        note_command(**kwargs)

    send_command(50, synth=1, note=base_note, vel=0.05)
    send_command(435, synth=1, note=base_note, vel=0)
    send_command(450, synth=1, note=base_note, vel=0.63)
    send_command(835, synth=1, note=base_note, vel=0)
    send_command(850, synth=1, note=base_note, vel=1.0)
    send_command(1485, synth=1, note=base_note, vel=0)
    send_command(1500, synth=1, note=base_note - 24, vel=0.6)
    send_command(2100, synth=1, note=base_note + 24, vel=1.0)
    send_command(3000, synth=1, note=base_note - 24, vel=0)
    send_command(3000, synth=1, note=base_note + 24, vel=0)

    _render_to_ms(3300)
    samples = np.hstack(_frames).reshape((-1, amy.AMY_NCHANS))

    wavwrite(filename, samples)
    print('Wrote', len(samples), 'samples to', filename)


def init_piano_voices():
    amy.send(store_patch='1024,' + tulip_piano.patch_string)
    amy.send(synth=1, num_voices=3, load_patch=1024)
    tulip_piano.init_piano_voice(tulip_piano.num_partials, synth=1)
    # additive_interpolated overwrites these settings before each note,
    # but pre-configure each note to C4.mf for additive_fixed.
    tulip_piano.setup_piano_voice_for_note_vel(note=60, vel=80, synth=1)


def main(argv):

  piano_example(base_note=74, volume=10, filename='piano_example_juno_patch_7.wav',
                init_command=lambda: amy.send(synth=1, num_voices=3, load_patch=7))
  piano_example(base_note=50, volume=25, filename='piano_example_dx7_patch_137.wav',
                init_command=lambda: amy.send(synth=1, num_voices=3, load_patch=137))
  piano_example(base_note=62, volume=5,
                filename='piano_example_additive_fixed.wav',
                init_command=init_piano_voices)
  piano_example(base_note=62,
                filename='piano_example_additive_interpolated.wav',
                init_command=init_piano_voices,
                note_command=tulip_piano.piano_note_on)
  
  print("done.")


if __name__ == "__main__":
  main(sys.argv)

  
