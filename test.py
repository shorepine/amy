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
  data = (wave_data.astype(np.float32)) / 32768.0
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
    rms_x = dB(rms(samples))
    message = ('%-32s:' % name) + (' signal=%.1f dB' % rms_x)

    ref_file = os.path.join(self.ref_dir, name + '.wav')
    try:
      expected_samples, _ = wavread(ref_file)

      rms_n = dB(rms(samples - expected_samples))
      message += (' err=%.1f dB' % rms_n)

    except FileNotFoundError:
      message += ' / Unable to read ' + ref_file

    print(message)

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
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN)
    amy.send(time=100, note=48, vel=1)
    amy.send(time=900, vel=0)


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


class TestPcmShift(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PCM, patch=10)
    # Cowbell with no note should play at "default" pitch, midi 69 (for that patch)
    amy.send(time=100, vel=1)
    # Specifying a note should shift its pitch.
    amy.send(time=500, note=70, vel=1)


class TestPcmLoop(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PCM, patch=10, feedback=1)
    amy.send(time=100, osc=0, vel=1)
    amy.send(time=500, osc=0, vel=0)


class TestPcmLoopEnvFilt(AmyTest):
  # Check that filter, amp-env, and pitch mod apply to PCM

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PCM, patch=23, feedback=1)
    amy.send(time=0, osc=0, filter_type=amy.FILTER_LPF24, filter_freq='200,0,0,0,3', bp1='0,1,500,0,200,0')
    amy.send(time=0, osc=0, bp0='100,1,1000,0,1000,0')
    amy.send(time=0, osc=1, freq='1')
    amy.send(time=0, osc=0, mod_source=1, freq=',,,,,-0.05')
    amy.send(time=100, osc=0, note=64, vel=5)
    amy.send(time=500, osc=0, vel=0)


class TestPartial(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PARTIALS, patch=1)
    amy.send(time=100, note=60, vel=1)


class TestPartialNoteOff(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PARTIALS, patch=5)
    amy.send(time=100, note=60, vel=5)
    amy.send(time=500, note=60, vel=0)


class TestPartialEnvFiltMod(AmyTest):

  def run(self):
    # PARTIALS uses a whole bunch of oscs following the parent osc, so put the modulator BEFORE.
    amy.send(time=0, osc=0, freq='0.5')
    amy.send(time=0, osc=1, wave=amy.PARTIALS, patch=5)
    amy.send(time=0, osc=1, mod_source=0, freq=',,,,,-0.1')  # Downward sigh from 1 Hz sine beginning.
    amy.send(time=0, osc=1, filter_type=amy.FILTER_LPF24, filter_freq='2000,0,0,0,3', bp1='0,1,500,0,200,0')
    amy.send(time=0, osc=1, bp0='100,1,1000,0,1000,0')
    amy.send(time=100, osc=1, note=60, vel=5)
    amy.send(time=500, osc=1, note=60, vel=0)


class TestSineEnv(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SINE, freq=1000)
    amy.send(time=0, osc=0, amp='0,0,0.85,1,0,0', bp0='50,1,200,0.1,50,0')
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestAlgo(AmyTest):

  def run(self):
    amy.send(time=0, voices="0",  load_patch=21+128)
    amy.send(time=100, voices="0", note=58, vel=1)
    amy.send(time=500, voices="0", vel=0)


class TestAlgo2(AmyTest):

  def run(self):
    amy.send(time=0, voices="0", load_patch=128+24)
    amy.send(time=100, voices="0", note=58, vel=1)
    amy.send(time=500, voices="0", vel=0)


class TestFilter(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy.send(time=100, note=48, vel=1.0)
    amy.send(time=900, vel=0)


class TestFilter24(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF24, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
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
    amy.send(time=0, osc=0, wave=amy.SINE, mod_source=1, freq='0,1,0,0,0,1')
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
    amy.send(time=0, osc=0, wave=amy.PULSE, mod_source=1, duty='0.5,0,0,0,0,0.25')
    amy.send(time=0, osc=1, wave=amy.SINE, freq=4, amp=1)
    amy.send(time=100, note=70, vel=1)
    amy.send(time=500, vel=0)
    

class TestGlobalEQ(AmyTest):

  def run(self):
    amy.send(time=0, eq="-10,10,3")
    amy.send(time=0, osc=0, wave=amy.SAW_UP)
    amy.send(time=100, note=46, vel=1)
    amy.send(time=500, vel=0)


class TestChorus(AmyTest):

  def run(self):
    # Turn on chorus.
    amy.send(chorus=1)
    # Note from TestFilter.
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy.send(time=100, note=48, vel=1.0)
    amy.send(time=900, vel=0)


class TestBrass(AmyTest):
  """One of the Juno-6 patches, spelled out."""

  def run(self):
    #amy.send(time=0, osc=0, wave=amy.SAW_UP, amp='0,0,0.85,1,0,0', freq='130.81,1,0,0,0,0', filter_type=amy.FILTER_LPF,
    #         resonance=0.167, bp0='60,1,740,0.9,200,0', filter_freq='6000,0.5,0,0,1,0',
    #         bp1='60,1,740,0.9,200,0')
    #amy.send(time=0, osc=0, wave=amy.SAW_UP, amp='0,0,0.85,1,0,0', freq='130.81,1,0,0,0,0', filter_type=amy.FILTER_LPF24,
    #         resonance=0.167, bp0='60,1,340,0.3,200,0', filter_freq='2000,0.5,0,0,4,0',
    #         bp1='60,1,340,0.3,200,0')
    amy.send(time=0, osc=1, wave=amy.SAW_UP, freq='130.81,1,0,0,0,0',
             amp='0,0,0.85,1,0,0', bp0='30,1,672,0.354,100,0',
             filter_type=amy.FILTER_LPF24, resonance=0.167,
             filter_freq='93.73,0.677,0,0,9.133,0', bp1='30,1,672,0.354,100,0',
             mod_source=2,
             )
    amy.send(time=0, osc=2,
             wave=amy.SINE, freq=0.974, bp0='156,1.0,100,1.0,100,0')  # amp='1,0,0,0,0,0') #
    amy.send(time=100, osc=1, note=76, vel=1.0)
    amy.send(time=300, osc=1, vel=0)
    amy.send(time=600, osc=1, note=76, vel=1.0)
    amy.send(time=800, osc=1, vel=0)
    # 'filter_freq': '93.73,0.677,0,0,4.567,0', 'bp1': '30,1,672,0.354,232,0'


class TestBrass2(AmyTest):
  """Trying to catch the note-off thump."""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_UP, amp='0,0,0.85,1', freq='130.815,1', 
             resonance=0.713, filter_type=amy.FILTER_LPF24, filter_freq='93.726,0.677,0,0,9.134',
             bp0='30,1,672,0.354,232,0', bp1='30,1,672,0.354,232,0')
    amy.send(time=100, osc=0, note=60, vel=1.0)
    amy.send(time=600, osc=0, vel=0)

class TestGuitar(AmyTest):
  """Trying to catch the note-off zzzzzip."""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_UP, amp='0,0,0.756,1', freq='130.815,1',
             filter_freq='16.23,0.236,0,0,11.181', resonance=0.753, filter_type=amy.FILTER_LPF24,
             bp0='6,1,51,0.425,153,0',
             bp1='6,1,51,0.425,153,0')
    amy.send(time=100, osc=0, note=60, vel=4.0)
    amy.send(time=150, osc=0, vel=0)
    amy.send(time=500, osc=0, note=60, vel=4.0)
    amy.send(time=550, osc=0, vel=0)

class TestBleep(AmyTest):
  """Test the tulip start-up beep."""

  def run(self):
    amy.send(time=0, wave=amy.SINE, freq=220)
    amy.send(time=0, osc=0, pan=0.9, vel=1)
    amy.send(time=150, osc=0, pan=0.1, freq=440)
    amy.send(time=300, osc=0, pan=0.5, vel=0)

class TestOverload(AmyTest):
  """Run the output very hot to check for clipping."""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy.send(time=0, eq="12")
    amy.send(time=0, chorus=1)
    amy.send(time=100, note=48, vel=8.0)
    amy.send(time=900, vel=0)

class TestJunoPatch(AmyTest):
  """Known Juno patch."""

  def run(self):
    amy.send(time=0, voices="0,1,2,3", load_patch=20)
    amy.send(time=50, voices="0", note=48, vel=1)
    amy.send(time=50, voices="1", note=60, vel=1)
    amy.send(time=50, voices="2", note=63, vel=1)
    amy.send(time=50, voices="3", note=67, vel=1)
    amy.send(time=600, voices="0", vel=0)
    amy.send(time=600, voices="1", vel=0)
    amy.send(time=600, voices="2", vel=0)
    amy.send(time=600, voices="3", vel=0)

class TestJunoClip(AmyTest):
  """Juno patch that clips."""

  def run(self):
    amy.send(time=0, voices="0,1,2,3", load_patch=9)
    amy.send(time=50, voices="0", note=60, vel=1)
    amy.send(time=50, voices="1", note=57, vel=1)
    amy.send(time=50, voices="2", note=55, vel=1)
    amy.send(time=50, voices="3", note=52, vel=1)
    amy.send(time=800, voices="0", vel=0)
    amy.send(time=800, voices="1", vel=0)
    amy.send(time=800, voices="2", vel=0)
    amy.send(time=800, voices="3", vel=0)

class TestLowVcf(AmyTest):
  """Weird fxpt warble when hitting fundamental."""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN,
             filter_type=amy.FILTER_LPF24, resonance=1.0,
             amp='0,0,0.85,1',
             filter_freq='161.28,0,0,0,5',
             bp0='0,1,0,0',
             bp1='0,1,600,0,1,0')
    amy.send(time=100, osc=0, note=48, vel=3)
    amy.send(time=800, osc=0, vel=0)

class TestLowerVcf(AmyTest):
  """Top16 LPF24 has issues with cf below fundamental?"""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN,
             filter_type=amy.FILTER_LPF24, resonance=4.0,
             amp='0,0,0.85,1',
             filter_freq='50,0,0,0,6',
             bp0='0,1,0,0',
             bp1='0,1,300,0,1,0')
    amy.send(time=100, osc=0, note=48, vel=3)
    amy.send(time=800, osc=0, vel=0)

class TestFlutesEq(AmyTest):
  """VCF leaving almost pure sine + HPF2 -> noise, clicks?"""

  def run(self):
    amy.send(time=0, eq="-15,8,8")
    amy.send(time=0, osc=0, wave=amy.SAW_UP, filter_type=amy.FILTER_LPF24, resonance=1.75,
             bp0='200,1,9800,0,100,0',
             bp1='200,1,9800,0,100,0',
             filter_freq='242,0.323'),
    amy.send(time=0, osc=1, clone_osc=0)
    amy.send(time=0, osc=2, clone_osc=0)
    amy.send(time=100, osc=0, note=48, vel=0.5)
    amy.send(time=200, osc=1, note=52, vel=0.5)
    amy.send(time=300, osc=2, note=55, vel=0.5)
    amy.send(time=900, osc=0, vel=0)
    amy.send(time=900, osc=1, vel=0)
    amy.send(time=900, osc=2, vel=0)

class TestOscBD(AmyTest):
  """Bass Drum as modulated sine-tone. amy.py:preset(5). """

  def run(self):
    # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
    amy.send(time=0, osc=1, wave=amy.SINE, amp=1, freq=0.25, phase=0.5)
    amy.send(time=0, osc=0, wave=amy.SINE, bp0="0,1,500,0,0,0", freq="261.63,1,0,0,0,2", mod_source=1)
    amy.send(time=100, osc=0, note=84, vel=1)
    amy.send(time=350, osc=0, note=84, vel=1)
    amy.send(time=600, osc=0, note=84, vel=1)

class TestChainedOsc(AmyTest):
  """Two oscillators chained together."""

  def run(self):
    # TestFilter but on Saw + subosc with same envelope.
    #amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    #amy.send(time=0, osc=1, wave=amy.PULSE, filter_type=amy.FILTER_LPF, resonance=8.0, amp="0,0,0.2,1", freq="130.81,1", filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    #amy.send(time=100, osc=0, note=48, vel=1.0)
    #amy.send(time=100, osc=1, note=48, vel=1.0)
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0', chained_osc=1)
    amy.send(time=0, osc=1, wave=amy.PULSE, amp="0,0,0.2,1", freq='130.81,1,0,0,0,0,1')
    amy.send(time=100, osc=0, note=48, vel=1.0)
    #amy.send(time=100, osc=1, note=48, vel=1.0)
    amy.send(time=900, osc=0, vel=0)
    #amy.send(time=900, osc=1, vel=0)

class TestJunoTrumpetPatch(AmyTest):
  """I'm hearing a click in the Juno Trumpet patch.  Catch it."""

  def run(self):
    amy.send(time=0, voices="0,1", load_patch=2)
    amy.send(time=50, voices="0", note=60, vel=1)
    amy.send(time=200, voices="0", vel=0)
    amy.send(time=300, voices="1", note=60, vel=1)
    amy.send(time=450, voices="1", vel=0)


class TestJunoCheapTrumpetPatch(AmyTest):
  """Try out the 'cheap' LPF hack."""

  def run(self):
    amy.send(time=0, voices="0,1", load_patch=2)
    amy.send(time=0, voices="0,1", filter_type=amy.FILTER_LPF)
    amy.send(time=50, voices="0", note=60, vel=1)
    amy.send(time=200, voices="0", vel=0)
    amy.send(time=300, voices="1", note=60, vel=1)
    amy.send(time=450, voices="1", vel=0)


class TestFilterReleaseGlitch(AmyTest):
  """See https://github.com/shorepine/amy/issues/126."""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF24, filter_freq='100,0,0,6')
    amy.send(time=100, note=64, vel=1)
    amy.send(time=500, vel=0)


class TestPortamento(AmyTest):

  def run(self):
    amy.send(time=0, voices="0,1,2", load_patch=0)

    # Starting-point pitches...
    amy.send(time=50, voices="0", note=60, vel=1)
    amy.send(time=50, voices="1", note=64, vel=1)
    amy.send(time=50, voices="2", note=67, vel=1)

    # .. immediately start bending towards final pitches.
    amy.send(time=60, voices="0,1,2", osc=0, portamento=100)
    amy.send(time=60, voices="0,1,2", osc=1, portamento=100)
    amy.send(time=60, voices="0,1,2", osc=2, portamento=100)
    amy.send(time=60, voices="0", note=65)
    amy.send(time=60, voices="1", note=69)
    amy.send(time=60, voices="2", note=72)

    amy.send(time=800, voices="0,1,2", vel=0)


class TestEcho(AmyTest):

  def run(self):
    amy.echo(level=0.5, delay_ms=200, feedback=0.7)
    amy.send(time=0, osc=0, bp0="0,1,200,0,0,0")

    amy.send(time=100, osc=0, note=48, vel=1)


class TestEchoLPF(AmyTest):

  def run(self):
    amy.echo(level=0.5, delay_ms=200, feedback=0.7, filter_coef=0.9)
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, bp0="0,1,200,0,0,0")

    amy.send(time=100, osc=0, note=48, vel=1)


class TestEchoHPF(AmyTest):

  def run(self):
    amy.echo(level=0.5, delay_ms=200, feedback=0.7, filter_coef=-0.9)
    amy.send(time=0, osc=0, wave=amy.SAW_DOWN, bp0="0,1,200,0,0,0")

    amy.send(time=100, osc=0, note=48, vel=1)


def main(argv):
  if len(argv) > 1:
    # Override location of reference files.
    AmyTest.ref_dir = argv[1]

  do_all_tests = True

  if do_all_tests:
    for testClass in AmyTest.__subclasses__():
      test_object = testClass()
      test_object.test()
  else:
    #TestPcmShift().test()
    #TestChorus().test()
    #TestBleep().test()
    #TestBrass().test()
    #TestBrass2().test()
    #TestSineEnv().test()
    #TestSawDownOsc().test()
    #TestGuitar().test()
    #TestFilter().test()
    #TestAlgo().test()
    #TestBleep().test()
    #TestChainedOsc().test()
    #TestJunoPatch().test()
    #TestJunoTrumpetPatch().test()
    TestPcmLoop().test()

  amy.send(debug=0)
  print("tests done.")


if __name__ == "__main__":
  main(sys.argv)

  
