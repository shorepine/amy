import sys
import os

import numpy as np
import scipy.io.wavfile as wav

import amy


def wavread(filename):
  """Read in audio data from a wav file.  Return d, sr."""
  # Read in wav file.
  file_handle = open(filename, 'rb')
  samplerate, wave_data = wav.read(file_handle)
  # Normalize short ints to floats in range [-1..1).
  data = np.asfarray(wave_data) / 32768.0
  return data, samplerate


def rms(samples):
  return np.sqrt(np.mean(samples ** 2))


def dB(level):
  return 20 * np.log10(level + 1e-5)


ref_dir = './tests/ref'
tst_dir = './tests/tst'


class AmyTest:

  def __init__(self):
    amy.restart()
    amy.send(time=0)  # Defeat "computed_delta" offset.

  def test(self):

    name = self.__class__.__name__

    self.run()
    
    samples = amy.render(1.0)
    amy.write(samples, os.path.join(tst_dir, name + '.wav'))

    ref_file = os.path.join(ref_dir, name + '.wav')
    try:
      expected_samples, _ = wavread(ref_file)

      rms_x = dB(rms(samples))
      rms_n = dB(rms(samples - expected_samples))
      print('%-16s:' % name, 'signal=%.1f dB' % rms_x, 'err=%.1f dB' % rms_n)

    except FileNotFoundError:
      print('Unable to read', ref_file)
  

class TestSineOsc(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SINE, freq=1000)
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestPulseOsc(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PULSE, freq=1000)
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestSawDownOsc(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, freq=1000)
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestSawUpOsc(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_UP, freq=1000)
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestTriangleOsc(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.TRIANGLE, freq=1000)
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestNoiseOsc(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.NOISE, freq=1000)
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestPcm(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PCM, patch=1)
    amy.send(time=100, vel=1)


class TestPartial(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PARTIALS, patch=1)
    amy.send(time=100, note=60, vel=1)


class TestSineEnv(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SINE, freq=1000)
    amy.send(time=0, osc=0, bp0_target=amy.TARGET_AMP, bp0='50,1,250,0.1,50,0')
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestAlgo(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.ALGO, patch=21)
    amy.send(time=100, note=70, vel=1)
    amy.send(time=500, vel=0)


class TestFilter(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.NOISE, filter_type=amy.FILTER_LPF, resonance=4.0, bp0_target=amy.TARGET_FILTER_FREQ, filter_freq=3000, bp0='0,1,500,0.02')
    amy.send(time=100, note=70, vel=1)
    amy.send(time=500, vel=0)


def main(argv):
  TestSineOsc().test()
  TestPulseOsc().test()
  TestSawDownOsc().test()
  TestSawUpOsc().test()
  TestTriangleOsc().test()
  TestNoiseOsc().test()
  TestPcm().test()
  TestPartial().test()
  TestAlgo().test()
  TestFilter().test()
  
  TestSineEnv().test()
  

if __name__ == "__main__":
  main(sys.argv)

  
