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



class AmyTest:

  ref_dir = './tests/ref'
  test_dir = './tests/tst'

  def __init__(self):
    amy.restart()
    amy.send(time=0)  # Defeat "computed_delta" offset.

  def test(self):

    name = self.__class__.__name__

    self.run()
    
    samples = amy.render(1.0)
    amy.write(samples, os.path.join(self.test_dir, name + '.wav'))

    ref_file = os.path.join(self.ref_dir, name + '.wav')
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
    amy.send(time=0, osc=0, wave=amy.SAW_UP)
    amy.send(time=100, note=46, vel=1)
    amy.send(time=500, vel=0)


class TestTriangleOsc(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.TRIANGLE, freq=1000)
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestNoiseOsc(AmyTest):

  def run(self):
    # If this is the first time noise is called, the waveform should be deterministic.
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


class TestAlgo2(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.ALGO, patch=24)
    amy.send(time=100, note=70, vel=1)
    amy.send(time=500, vel=0)


class TestFilter(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy.send(time=100, note=48, vel=1.0)
    amy.send(time=900, vel=0)


class TestFilterLFO(AmyTest):

  def run(self):
    amy.send(time=0, osc=1, wave=amy.SINE, freq=6, amp=1.0)
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, mod_source=1, filter_freq='400,0,0,0,3,0.5', bp1='0,1,500,0,100,0')
    amy.send(time=100, note=48, vel=1.0)
    amy.send(time=500, vel=0)


class TestLFO(AmyTest):

  def run(self):
    # LFO mod used to be 1+x i.e. 0.9..1.1
    #amy.send(time=0, osc=1, wave=amy.SINE, freq=4, amp=0.1)
    # With unit-per-octave scaling, that's approx log2(0.9) = -0.152, log2(1.1) = 0.138
    amy.send(time=0, osc=1, wave=amy.SINE, freq=4, amp=0.138)
    amy.send(time=0, osc=0, wave=amy.SINE, mod_source=1, mod_target=amy.TARGET_FREQ)
    amy.send(time=100, note=70, vel=1)
    amy.send(time=500, vel=0)
    

class TestDuty(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PULSE, duty=0.1)
    amy.send(time=100, note=70, vel=1)
    amy.send(time=200, vel=0)
    amy.send(time=300, osc=0, wave=amy.PULSE, duty=0.9)
    amy.send(time=300, note=70, vel=1)
    amy.send(time=400, vel=0)
    

class TestPWM(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PULSE, mod_source=1, mod_target=amy.TARGET_DUTY)
    amy.send(time=0, osc=1, wave=amy.SINE, freq=4, amp=0.25)
    amy.send(time=100, note=70, vel=1)
    amy.send(time=500, vel=0)
    

class TestGlobalEQ(AmyTest):

  def run(self):
    amy.send(time=0, eq_l=-10, eq_m=10, eq_h=3)
    amy.send(time=0, osc=0, wave=amy.SAW_UP)
    amy.send(time=100, note=46, vel=1)
    amy.send(time=500, vel=0)


class TestChorus(AmyTest):

  def run(self):
    # Turn on chorus.
    amy.send(chorus_level=1)
    # Note from TestFilter.
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy.send(time=100, note=48, vel=1.0)
    amy.send(time=900, vel=0)


def main(argv):
  if len(argv) > 1:
    # Override location of reference files.
    AmyTest.ref_dir = argv[1]

  for testClass in AmyTest.__subclasses__():
    test_object = testClass()
    test_object.test()


if __name__ == "__main__":
  main(sys.argv)

  
