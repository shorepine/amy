import sys
import os

import numpy as np
import scipy.io.wavfile as wav

import amy
import c_amy as _amy
import time


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
    self.config_default = False

  def test(self):
    name = self.__class__.__name__
    _amy.stop()
    if self.config_default:
      _amy.start()
    else:
      _amy.start_no_default()
    self.run()
    
    samples = amy.render(1.0)
    amy.write(samples, os.path.join(self.test_dir, name + '.wav'))
    rms_x = dB(rms(samples))
    message = ('%-32s:' % name) + (' signal=%5.1f dB' % rms_x)

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
    amy.send(time=0, osc=0, wave=amy.PCM, preset=1)
    amy.send(time=100, vel=1)


class TestPcmShift(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PCM, preset=10)
    # Cowbell with no note should play at "default" pitch, midi 69 (for that preset)
    amy.send(time=100, vel=1)
    # Specifying a note should shift its pitch.
    amy.send(time=500, note=70, vel=1)


class TestPcmPatchChange(AmyTest):
  """There was a bug where switching PCM preset would persist the base note of the preceding preset."""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PCM, preset=9)  # Clap
    amy.send(time=100, vel=1)
    amy.send(time=450, preset=10)  # Cowbell
    amy.send(time=500, vel=1)


class TestPcmLoop(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PCM, preset=10, feedback=1)
    amy.send(time=100, osc=0, vel=1)
    amy.send(time=500, osc=0, vel=0)


class TestPcmLoopEnvFilt(AmyTest):
  # Check that filter, amp-env, and pitch mod apply to PCM

  def run(self):
    amy.send(time=0, osc=0, wave=amy.PCM, preset=10, feedback=1)
    amy.send(time=0, osc=0, filter_type=amy.FILTER_LPF24, filter_freq='200,0,0,0,3', bp1='0,1,500,0,200,0')
    amy.send(time=0, osc=0, bp0='100,1,1000,0,1000,0')
    amy.send(time=0, osc=1, freq='1')
    amy.send(time=0, osc=0, mod_source=1, freq=',,,,,-0.2')
    amy.send(time=100, osc=0, note=64, vel=5)
    amy.send(time=500, osc=0, vel=0)



class TestBuildYourOwnPartials(AmyTest):

  def run(self):
    # PARTIALS but you have to configure the freq and amp of each partial yourself
    num_partials = 16
    base_freq = 261.63
    base_osc = 0
    amy.send(time=0, osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials)
    for i in range(1, num_partials + 1):
      # Set up each partial as the corresponding harmonic of the base_freq, with an amplitude of 1/N, 50ms attack, and a decay of 1 sec / N
      amy.send(osc=base_osc + i, wave=amy.PARTIAL, freq=base_freq * i, bp0='50,%.2f,%d,0,0,0' % ((1.0 / i), 1000 // i))
    amy.send(time=100, osc=0, note=60, vel=0.5)
    amy.send(time=200, osc=0, note=72, vel=1)

class TestBYOPVoices(AmyTest):

  def run(self):
    # Does build-your-own-partials work with the voices mechanism?
    num_partials = 4
    s = 'v0w%dp%dZ' % (amy.BYO_PARTIALS, num_partials) + ''.join(['v%dw%dZ' % (i + 1, amy.PARTIAL) for i in range(num_partials)])
    #amy.send(patchr=1024, patch_string=s)
    #amy.send(time=0, voices='0,1,2,3', patch=1024)
    amy.send(time=0, voices='0,1,2,3', patch_string=s)
    for i in range(num_partials):
      amy.send(voices='0,1,2,3', osc=i + 1, freq=220 * (i + 1), bp0='50,1,%d,0,50,0' % (600 // (i + 1)))
    amy.send(time=100, voices=0, note=60, vel=1)
    amy.send(time=200, voices=1, note=63, vel=1)
    amy.send(time=300, voices=2, note=67, vel=1)
    amy.send(time=400, voices=3, note=70, vel=1)

class TestBYOPNoteOff(AmyTest):

  def run(self):
    # Partials were not seeing note-offs.
    num_partials = 8
    s = 'v0w%dp%dZ' % (amy.BYO_PARTIALS, num_partials) + ''.join(['v%dw%dZ' % (i + 1, amy.PARTIAL) for i in range(num_partials)])
    amy.send(patch=1024, patch_string=s)
    amy.send(time=0, voices='0,1', patch=1024)
    for i in range(num_partials):
      amy.send(voices='0,1', osc=i + 1, freq=220 * (i + 1), bp0='50,1,%d,%f,200,0' % (1000 // (i + 1), 1 / (i + 1)))
    amy.send(voices='0,1', bp0='0,1,1000,0')  # Parent osc env is slow release to be able to see partials.
    amy.send(time=100, voices=1, note=60, vel=1)
    amy.send(time=700, voices=1, vel=0)

class TestInterpPartials(AmyTest):

  def run(self):
    # PARTIALS but each partial is interpolated from a table of pre-analyzed harmonic-sets.
    base_osc = 0
    num_partials = 20
    amy.send(time=0, osc=base_osc, wave=amy.INTERP_PARTIALS, preset=0)
    for i in range(1, num_partials + 1):
      amy.send(osc=base_osc + i, wave=amy.PARTIAL)
    amy.send(time=50, osc=0, note=60, vel=0.1)
    amy.send(time=300, osc=0, note=67, vel=0.6)
    amy.send(time=550, osc=0, note=72, vel=1)
    amy.send(time=800, osc=0, vel=0)
    
class TestInterpPartialsRetrigger(AmyTest):

  def run(self):
    # PARTIALS but each partial is interpolated from a table of pre-analyzed harmonic-sets.
    base_osc = 0
    num_partials = 20
    amy.send(time=0, osc=base_osc, wave=amy.INTERP_PARTIALS, preset=0)
    for i in range(1, num_partials + 1):
      amy.send(osc=base_osc + i, wave=amy.PARTIAL)
    amy.send(time=50, osc=0, note=52, vel=0.7)
    amy.send(time=200, osc=0, note=52, vel=0.8)
    amy.send(time=350, osc=0, note=52, vel=0.9)
    amy.send(time=500, osc=0, vel=0)
    amy.send(time=510, osc=100, wave=amy.SINE, bp0='3,1,500,0,50,0')
    amy.send(time=550, osc=100, note=76, vel=1)
    amy.send(time=700, osc=100, note=76, vel=1)
    amy.send(time=850, osc=100, vel=0)


class TestSineEnv(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SINE, freq=1000)
    amy.send(time=0, osc=0, amp='0,0,0.85,1,0,0', bp0='50,1,200,0.1,50,0')
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)


class TestSineEnv2(AmyTest):

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SINE, freq=1000)
    amy.send(time=0, osc=0, amp='0,0,1,1,0,0', bp0='0,0,200,5,200,0,0,0')
    amy.send(time=100, vel=1)
    amy.send(time=500, vel=0)
    # The DX7 algo is weird - attack is different from decay, envelope is clipped to 1.
    amy.send(time=500, osc=0, eg0_type=amy.ENVELOPE_DX7)
    amy.send(time=550, vel=1)
    amy.send(time=950, vel=0)


class TestAlgo(AmyTest):

  def run(self):
    amy.send(time=0, voices="0",  patch=21+128)
    amy.send(time=100, voices="0", note=58, vel=1)
    amy.send(time=500, voices="0", vel=0)


class TestAlgo2(AmyTest):

  def run(self):
    amy.send(time=0, voices="0", patch=128+24)
    amy.send(time=100, voices="0", note=58, vel=1)
    amy.send(time=500, voices="0", vel=0)


class TestFMRepeat(AmyTest):
  # Douglas reports that the DX7 Marimba sometimes clicks at onset.

  def run(self):
    amy.send(time=0, voices="0", patch=128+21)
    for i in range(5):
      t = 100 + round(i * 51200 / 441)
      amy.send(time=t, voices="0", note=32, vel=1)
      amy.send(time=t + 20, voices="0", vel=0)


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
    # Also test the synth mechanism.
    amy.send(time=0, synth=1, num_voices=4, patch=20)
    amy.send(time=50, synth=1, note=48, vel=1)
    amy.send(time=150, synth=1, note=60, vel=1)
    amy.send(time=250, synth=1, note=63, vel=1)
    amy.send(time=350, synth=1, note=67, vel=1)
    amy.send(time=600, synth=1, note=48, vel=0)
    amy.send(time=700, synth=1, note=60, vel=0)
    amy.send(time=800, synth=1, note=63, vel=0)
    amy.send(time=900, synth=1, note=67, vel=0)

class TestJunoClip(AmyTest):
  """Juno patch that clips."""

  def run(self):
    amy.send(time=0, voices="0,1,2,3", patch=9)
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
    osc_args = {'time':0, 'wave':amy.SAW_UP, 'filter_type':amy.FILTER_LPF24, 'resonance':1.75, 
        'bp0':'200,1,9800,0,100,0', 'bp1':'200,1,9800,0,100,0', 'filter_freq':'242,0.323'}
    amy.send(osc=0, **osc_args)
    amy.send(osc=1, **osc_args)
    amy.send(osc=2, **osc_args)
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
    # Sine waveform always starts at phase 0 after retrigger.
    amy.send(time=0, osc=0, wave=amy.SINE, phase=0, bp0="0,1,500,0,0,0", freq="261.63,1,0,0,0,2", mod_source=1)
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
    amy.send(time=0, voices="0,1", patch=2)
    amy.send(time=50, voices="0", note=60, vel=1)
    amy.send(time=200, voices="0", vel=0)
    amy.send(time=300, voices="1", note=60, vel=1)
    amy.send(time=450, voices="1", vel=0)


class TestJunoCheapTrumpetPatch(AmyTest):
  """Try out the 'cheap' LPF hack."""

  def run(self):
    amy.send(time=0, voices="0,1", patch=2)
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
    amy.send(time=0, voices="0,1,2", patch=0)

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


class TestVoiceManagement(AmyTest):
  """The 'synth' structure manages a set of voices."""

  def run(self):
    # Patch is bare sinewave oscillator but with a 100ms release.
    #amy.send(patch=1024, patch_string=amy.message(osc=0, wave=amy.SINE, bp0='0,1,1000,1,100,0'))
    #amy.send(time=10, synth=0, num_voices=3, patch=1024)
    patch_string = amy.message(osc=0, wave=amy.SINE, bp0='0,1,1000,0,100,0')
    amy.send(time=10, synth=0, num_voices=2, patch_string=patch_string)
    amy.send(time=100, synth=0, note=60, vel=1)
    amy.send(time=200, synth=0, note=72, vel=1)
    # Check if using the same string for a second synth reuses the same memory_patch (based on debug fprintfs).
    amy.send(time=200, synth=1, num_voices=1, patch_string=patch_string)
    amy.send(time=300, synth=1, note=84, vel=1)
    # We ran out of voices, this should steal the first one
    amy.send(time=400, synth=0, note=96, vel=1)
    # Stop one
    amy.send(time=500, synth=1, note=84, vel=0)
    # Stop all the rest - vel=0 without note= means all notes off.
    amy.send(time=600, synth=0, vel=0)


class TestVoiceStealing(AmyTest):
  """There's a bug with the default 6 voices."""

  def __init__(self):
    super().__init__()
    self.config_default = True
  
  def run(self):
    # Default juno synth.
    amy.send(time=40, synth=1, note=60, vel=1)
    amy.send(time=120, synth=1, note=64, vel=1)
    amy.send(time=200, synth=1, note=67, vel=1)
    amy.send(time=280, synth=1, note=70, vel=1)
    amy.send(time=360, synth=1, note=72, vel=1)
    amy.send(time=440, synth=1, note=76, vel=1)
    amy.send(time=520, synth=1, note=79, vel=1)
    amy.send(time=600, synth=1, note=82, vel=1)
    amy.send(time=800, synth=1, note=60, vel=0)
    amy.send(time=820, synth=1, note=64, vel=0)
    amy.send(time=840, synth=1, note=67, vel=0)
    amy.send(time=860, synth=1, note=70, vel=0)
    amy.send(time=880, synth=1, note=72, vel=0)
    amy.send(time=900, synth=1, note=76, vel=0)
    amy.send(time=920, synth=1, note=79, vel=0)
    amy.send(time=940, synth=1, note=82, vel=0)


class TestMidiDrums(AmyTest):
  """Test MIDI drums on channel 10 via injection."""

  def __init__(self):
    super().__init__()
    self.config_default = True
  
  def run(self):
    # inject_midi args are (time, midi_event_chan, midi_note, midi_vel)
    amy.inject_midi(100, 0x99, 35, 100)  # bass
    amy.inject_midi(400, 0x99, 35, 100)  # bass
    amy.inject_midi(400, 0x99, 37, 100)  # snare
    amy.inject_midi(700, 0x99, 37, 100)  # snare
    amy.inject_midi(900, 0x89, 37, 100)  # snare note off


class TestDefaultChan1Synth(AmyTest):
  """Test default setup of Juno synth on synth 1 (MIDI channel 1)."""

  def __init__(self):
    super().__init__()
    self.config_default = True

  def run(self):
    amy.send(time=100, synth=1, note=60, vel=1)
    amy.send(time=300, synth=1, note=63, vel=1)
    amy.send(time=500, synth=1, note=67, vel=1)
    amy.send(time=700, synth=1, note=74, vel=1)
    amy.send(time=800, synth=1, note=60, vel=0)
    amy.send(time=850, synth=1, note=63, vel=0)
    amy.send(time=900, synth=1, note=67, vel=0)
    amy.send(time=950, synth=1, note=74, vel=0)


class TestSynthProgChange(AmyTest):
  """Test switchting default synth to DX7, do oscs allocate OK?"""

  def __init__(self):
    super().__init__()
    self.config_default = True

  def run(self):
    # DX7 first patch, uses 9 oscs/voice, num_voices is inherited from previous init.
    amy.send(time=0, synth=1, patch=128)
    amy.send(time=100, synth=1, note=60, vel=1)
    amy.send(time=300, synth=1, note=63, vel=1)
    amy.send(time=500, synth=1, note=67, vel=1)
    amy.send(time=700, synth=1, note=74, vel=1)
    amy.send(time=800, synth=1, note=60, vel=0)
    amy.send(time=850, synth=1, note=63, vel=0)
    amy.send(time=900, synth=1, note=67, vel=0)
    amy.send(time=950, synth=1, note=74, vel=0)


class TestSynthDrums(AmyTest):
  """Test MIDI drums using synth-level note translation."""

  def __init__(self):
    super().__init__()
    self.config_default = True
  
  def run(self):
    amy.send(time=100, synth=10, note=35, vel=100/127)  # bass
    amy.send(time=400, synth=10, note=35, vel=100/127)  # bass
    amy.send(time=400, synth=10, note=37, vel=100/127)  # snare
    amy.send(time=700, synth=10, note=37, vel=100/127)  # snare
    amy.send(time=900, synth=10, note=37, vel=0)  # snare note off - ignored with current setup.


class TestSynthFlags(AmyTest):
  """Test setting up MIDI drums using synth_flags (alias).  Slightly different waveform than TestSynthDrums because chorus is off."""

  def run(self):
    # The default config is NOT set, set up MIDI drums on instrument 1 here.
    amy.send(patch=1024, patch_string='w7f0');
    # synth_flags=3 means do MIDI drums note translation and ignore note-offs.
    amy.send(synth=1, synth_flags=3, num_voices=4, patch=1024)
    amy.send(time=100, synth=1, note=35, vel=100/127)  # bass
    amy.send(time=400, synth=1, note=35, vel=100/127)  # bass
    amy.send(time=400, synth=1, note=37, vel=100/127)  # snare
    amy.send(time=700, synth=1, note=37, vel=100/127)  # snare
    amy.send(time=900, synth=1, note=37, vel=0)  # snare note off - ignored with current setup.


class TestDoubleNoteOff(AmyTest):
  """Test for bug where release restarts if a second note-off is received (#319)."""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SINE, bp0='0,1,100,1,1000,0')
    amy.send(time=100, osc=0, note=60, vel=1)
    amy.send(time=200, osc=0, vel=0)
    amy.send(time=600, osc=0, vel=0)


class TestSustainPedal(AmyTest):
  """Test sustain pedal."""

  def run(self):
    amy.send(time=0, reset=amy.RESET_SYNTHS)
    amy.send(time=0, synth=1, num_voices=4, patch=256)
    amy.send(time=50, synth=1, note=76, vel=1)
    amy.send(time=100, synth=1, note=76, vel=0)
    amy.send(time=150, synth=1, pedal=127)
    amy.send(time=250, synth=1, note=63, vel=1)
    amy.send(time=300, synth=1, note=63, vel=0)
    amy.send(time=450, synth=1, note=67, vel=1)
    amy.send(time=500, synth=1, note=67, vel=0)
    amy.send(time=650, synth=1, note=72, vel=1)   # This note is held across the pedal release
    amy.send(time=750, synth=1, pedal=0)
    amy.send(time=900, synth=1, note=72, vel=0)


class TestPatchFromEvents(AmyTest):
  """Test defining a patch from events with patch_number."""
  def __init__(self):
    super().__init__()
    self.config_default = True   # So that the patch space is already partly populated.

  def run(self):
    amy.send(time=0, patch=1039, reset=amy.RESET_PATCH)
    amy.send(time=0, patch=1039, osc=0, wave=amy.SAW_DOWN, bp0='0,1,1000,0.1,200,0', chained_osc=1)
    amy.send(time=0, patch=1039, osc=1, wave=amy.SINE, freq=131, bp0='0,1,500,0,200,0')
    amy.send(time=0, synth=0, num_voices=4, patch=1039)
    amy.send(time=100, synth=0, note=60, vel=1)
    amy.send(time=300, synth=0, note=64, vel=1)
    amy.send(time=500, synth=0, note=67, vel=1)
    amy.send(time=800, synth=0, vel=0)


class TestBreakpointsRealloc(AmyTest):
  """A default osc has only 8 breakpoints, but it should realloc to 24 if you try to set a long bpset."""

  def run(self):
    amy.send(time=0, osc=0, wave=amy.SINE, bp0='100,1,100,0,100,1,100,0,100,1,100,0,100,1,100,0')
    amy.send(time=100, osc=0, note=60, vel=1)
    amy.send(time=900, osc=0, vel=0)



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
    #TestPcmLoop().test()
    #TestBYOPNoteOff().test()
    #TestInterpPartials().test()
    #TestVoiceStealing().test()
    #TestSustainPedal().test()
    TestPatchFromEvents().test()

  amy.send(debug=0)
  print("tests done.")


if __name__ == "__main__":
  main(sys.argv)

  
