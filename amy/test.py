import sys
import os
import random
import string
import tempfile

import numpy as np
import soundfile as sf

import amy
import c_amy as _amy
import time

from . import constants

def wavread(filename):
  """Read in audio data from a wav file.  Return d, sr."""
  # Read in wav file.
  wav_data, samplerate = sf.read(filename, dtype='int16')
  # Normalize short ints to floats in range [-1..1).
  data = (wav_data.astype(np.float32)) / 32768.0
  return data, samplerate


def rms(samples):
  return np.sqrt(np.mean(samples ** 2))


def dB(level):
  return 20 * np.log10(level + 1e-5)


# amy_send_at() allows tests to run AMY commands in "real time" by rendering
# up until the time specified (thus advancing AMY's internal clock), then
# sending the rest of the command.
_test_clock_frames = []
_test_clock_blocks = 0

def _reset_test_clock():
  global _test_clock_frames, _test_clock_blocks
  _test_clock_frames = []
  _test_clock_blocks = 0

def _blocks_for_ms(ms):
  """The block count on which amy_sysclock() first reads >= `ms`."""
  numerator = int(round(ms)) * amy.AMY_SAMPLE_RATE
  denominator = amy.AMY_BLOCK_SIZE * 1000
  return -(-numerator // denominator)  # ceiling division

def _render_test_clock_to_ms(ms):
  """Render whole blocks until amy_sysclock() would read >= `ms`."""
  global _test_clock_blocks
  target_blocks = _blocks_for_ms(ms)
  while _test_clock_blocks < target_blocks:
    _test_clock_frames.append(np.array(_amy.render_to_list()) / 32768.0)
    _test_clock_blocks += 1

def _finish_test_clock(seconds):
  """Render out to `seconds`, same shape as amy.render() gives."""
  global _test_clock_blocks
  target_blocks = int((seconds * amy.AMY_SAMPLE_RATE) / amy.AMY_BLOCK_SIZE)
  while _test_clock_blocks < target_blocks:
    _test_clock_frames.append(np.array(_amy.render_to_list()) / 32768.0)
    _test_clock_blocks += 1
  return np.hstack(_test_clock_frames).reshape((-1, amy.AMY_NCHANS))

def amy_send_at(time=0, **kwargs):
  """Send an amy command once amy.render() reaches the specified time (in ms)."""
  _render_test_clock_to_ms(time)
  amy.send(**kwargs)


def amy_inject_midi_at(time, *midi_bytes):
  """Feed MIDI bytes once amy.render() reaches the specified time (in ms).

  The MIDI path has no scheduling parameter of its own -- it plays whatever it
  is handed, when it is handed it -- so timing comes from rendering up to the
  moment, exactly as amy_send_at() does. Goes through the real byte-stream
  parser, so running status and interleaved realtime bytes behave as they do
  for actual MIDI input.
  """
  _render_test_clock_to_ms(time)
  amy.inject_midi_bytes(list(midi_bytes))


class AmyTest:

  ref_dir = './tests/ref'
  test_dir = './tests/tst'

  def __init__(self):
    self.default_synths = False

  def test(self):
    name = self.__class__.__name__
    _amy.stop()
    _amy.start(1 if self.default_synths else 0)
    _reset_test_clock()
    self.run()

    samples = _finish_test_clock(1.0)
    amy.write(samples, os.path.join(self.test_dir, name + '.wav'))
    rms_x = dB(rms(samples))
    message = ('%-32s:' % name) + (' signal=%5.1f dB' % rms_x)

    ref_file = os.path.join(self.ref_dir, name + '.wav')
    try:
      expected_samples, _ = wavread(ref_file)

      rms_n = dB(rms(samples - expected_samples))
      message += (' err=%.1f dB' % rms_n)

    except FileNotFoundError:
      message += ' / Could not find ' + ref_file
      rms_n = 0
    except sf.LibsndfileError:
      message += ' / Error reading ' + ref_file
      rms_n = 0

    # Any value above this threshold counts as a failed test
    threshold = float(os.environ.get('AMY_TEST_THRESHOLD_DB', '-100.0'))
    test_passed = (rms_n <= threshold)
    return test_passed, message


class TestSineOsc(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SINE, freq=1000)
    amy_send_at(time=100, vel=1)
    amy_send_at(time=500, vel=0)


class TestPulseOsc(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.PULSE, freq=1000)
    amy_send_at(time=100, vel=1)
    amy_send_at(time=500, vel=0)


class TestSawDownOsc(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN)
    amy_send_at(time=100, note=48, vel=1)
    amy_send_at(time=900, vel=0)


class TestSawUpOsc(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SAW_UP)
    amy_send_at(time=100, note=46, vel=1)
    amy_send_at(time=500, vel=0)


class TestTriangleOsc(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.TRIANGLE, freq=1000)
    amy_send_at(time=100, vel=1)
    amy_send_at(time=500, vel=0)


class TestNoiseOsc(AmyTest):

  def run(self):
    # If this is the first time noise is called, the waveform should be deterministic.
    amy_send_at(time=0, osc=0, wave=amy.NOISE, freq=1000)
    amy_send_at(time=100, vel=1)
    amy_send_at(time=500, vel=0)


class TestKarplusStrong(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.KS, freq=220, feedback=0.98)
    amy_send_at(time=100, note=60, vel=1)
    amy_send_at(time=300, note=62, vel=1)
    amy_send_at(time=500, note=64, vel=1)
    amy_send_at(time=700, note=65, vel=1)
    amy_send_at(time=900, vel=0)



class TestPcm(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.PCM, preset=1)
    amy_send_at(time=100, vel=1)


class TestPcmShift(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.PCM, preset=10)
    # Cowbell with no note should play at "default" pitch, midi 69 (for that preset)
    amy_send_at(time=100, vel=1)
    # Specifying a note should shift its pitch.
    amy_send_at(time=500, note=70, vel=1)


class TestPcmPatchChange(AmyTest):
  """There was a bug where switching PCM preset would persist the base note of the preceding preset."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.PCM, preset=9)  # Clap
    amy_send_at(time=100, vel=1)
    amy_send_at(time=450, preset=10)  # Cowbell
    amy_send_at(time=500, vel=1)


class TestPcmTriggerPhase(AmyTest):
  """PCM: a trigger_phase (P) sent with a note-on sets the sample start point
     (start_frame / 2^23).   It persist for that osc."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.PCM, preset=1)
    amy_send_at(time=100, osc=0, phase=0.0005, vel=1)   # start ~4200 frames in
    amy_send_at(time=600, osc=0, vel=1)                 # starts at the same point again.


class TestPcmLoop(AmyTest):
  """Basic looping."""

  def run(self):
    amy.load_sample('sounds/partial_sources/CL SHCI A3 LP.wav', preset=1024, midinote=60)
    amy_send_at(time=0, osc=0, wave=amy.PCM, preset=1024, mode=amy.PCM_LOOP)
    amy_send_at(time=100, osc=0, vel=1)
    amy_send_at(time=500, osc=0, vel=0)  # Should play through end of sample


class TestPcmLoopStop(AmyTest):
  """Loop with immediate stop on note-off."""

  def run(self):
    amy.load_sample('sounds/partial_sources/CL SHCI A3 LP.wav', preset=1024, midinote=60)
    amy_send_at(time=0, osc=0, wave=amy.PCM, preset=1024, mode=amy.PCM_LOOP_STOP)
    amy_send_at(time=100, osc=0, vel=1)
    amy_send_at(time=500, osc=0, vel=0)  # Should stop immediately


class TestPcmLoopForever(AmyTest):
  """Looping continues after note-off, envelope is applied."""

  def run(self):
    amy.load_sample('sounds/partial_sources/CL SHCI A3 LP.wav', preset=1024, midinote=60)
    amy_send_at(time=0, osc=0, wave=amy.PCM, preset=1024, mode=amy.PCM_LOOP_FOREVER)
    amy_send_at(time=10, osc=0, eg0='0,1,700,0')
    amy_send_at(time=100, osc=0, vel=1)
    amy_send_at(time=200, osc=0, vel=0)  # Should keep looping and decay over 700ms


class TestPcmLoopEnvFilt(AmyTest):
  """Check that filter, amp-env, and pitch mod apply to PCM."""

  def run(self):
    amy.load_sample('sounds/partial_sources/CL SHCI A3 LP.wav', preset=1024, midinote=60)
    amy_send_at(time=0, osc=0, wave=amy.PCM, preset=1024, mode=amy.PCM_LOOP_FOREVER)
    amy_send_at(time=0, osc=0, filter_type=amy.FILTER_LPF24, filter_freq='1000,0,0,0,3',
             bp1='0,1,500,0,200,0')
    amy_send_at(time=0, osc=0, bp0='100,1,1000,0,1000,0')
    amy_send_at(time=0, osc=0, mod_source=1, freq=',,,,,0.1')
    amy_send_at(time=0, osc=1, freq=1, phase=0.5)
    # Highpitch playback breaks looping - BUG
    #amy_send_at(time=100, osc=0, note=72, vel=5)
    amy_send_at(time=100, osc=0, note=60, vel=5)
    amy_send_at(time=800, osc=0, vel=0)


class TestPcmPhaseLive(AmyTest):
  """Modifying the PCM freq while a sound is playing gives discontinuities.  Issue #916"""

  def run(self):
    amy_send_at(time=0, synth=10, patch=258)         # GM drums
    amy_send_at(time=100, synth=10, note=36, vel=1)  # long kick sounding
    # while it rings, sweep the pitch:
    # i10n35f479.727a1Q0.5G0F16000R0.7P0Z
    kwargs = {'amp': 1, 'pan': 0.5, 'filter_type': amy.FILTER_NONE, 'filter_freq': 16000, 'resonance': 0.7, 'phase': 0}
    amy_send_at(time=150, synth=10, note=36, freq=490, **kwargs)
    amy_send_at(time=175, synth=10, note=36, freq=523, **kwargs)
    amy_send_at(time=200, synth=10, note=36, freq=554, **kwargs)
    amy_send_at(time=225, synth=10, note=36, freq=575, **kwargs)
    amy_send_at(time=250, synth=10, note=36, freq=595, **kwargs)


class TestBuildYourOwnPartials(AmyTest):

  def run(self):
    # PARTIALS but you have to configure the freq and amp of each partial yourself
    num_partials = 16
    base_freq = constants.ZERO_LOGFREQ_IN_HZ
    base_osc = 0
    amy_send_at(time=0, osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials, eg0='0,1,30000,0')
    for i in range(1, num_partials + 1):
      # Set up each partial as the corresponding harmonic of the base_freq, with an amplitude of 1/N, 50ms attack, and a decay of 1 sec / N
      # Note that "vel sentivity" in amp actually means "Sensitivity to parent osc amplitude", since it is passed down through a modified vel.
      amy.send(osc=base_osc + i, wave=amy.PARTIAL, freq=base_freq * i, eg0='50,1,%d,0,50,0' % (1000 // i),
               amp='%.2f,0,1,1' % (1.0 / i))
    amy_send_at(time=100, osc=0, note=60, vel=0.5)
    amy_send_at(time=200, osc=0, note=72, vel=1)
    amy_send_at(time=800, osc=0, note=72, vel=0)


class TestBYOPVoices(AmyTest):

  def run(self):
    # Does build-your-own-partials work with the voices mechanism?
    num_partials = 4
    base_freq = constants.ZERO_LOGFREQ_IN_HZ
    s = 'v0w%dp%dZ' % (amy.BYO_PARTIALS, num_partials) + ''.join(['v%dw%dZ' % (i + 1, amy.PARTIAL) for i in range(num_partials)])
    amy_send_at(time=0, synth=1, num_voices=4, patch_string=s)
    for i in range(num_partials):
      amy_send_at(time=0, synth=1, osc=i + 1, freq=base_freq * (i + 1), bp0='50,1,%d,0,50,0' % (600 // (i + 1)))
    amy_send_at(time=100, synth=1, note=60, vel=1)
    amy_send_at(time=200, synth=1, note=63, vel=1)
    amy_send_at(time=300, synth=1, note=67, vel=1)
    amy_send_at(time=400, synth=1, note=70, vel=1)


class TestBYOPNoteOff(AmyTest):

  def run(self):
    # Partials were not seeing note-offs.
    num_partials = 8
    base_freq = constants.ZERO_LOGFREQ_IN_HZ
    s = 'v0w%dp%dZ' % (amy.BYO_PARTIALS, num_partials) + ''.join(['v%dw%dZ' % (i + 1, amy.PARTIAL) for i in range(num_partials)])
    amy.send(patch=1024, patch_string=s)
    amy_send_at(time=0, synth=1, num_voices=2, patch=1024)
    for i in range(num_partials):
      amy_send_at(time=0, synth=1, osc=i + 1, freq=base_freq * (i + 1), bp0='50,1,%d,%f,200,0' % (1000 // (i + 1), 1 / (i + 1)))
    amy_send_at(time=0, synth=1, osc=0, bp0='0,1,1000,0')  # Parent osc env is slow release to be able to see partials.
    amy_send_at(time=100, synth=1, note=60, vel=1)
    amy_send_at(time=700, synth=1, vel=0)


class TestInterpPartials(AmyTest):

  def run(self):
    # PARTIALS but each partial is interpolated from a table of pre-analyzed harmonic-sets.
    base_osc = 0
    num_partials = 25  # Doesn't do anything?
    amy_send_at(time=0, osc=base_osc, wave=amy.INTERP_PARTIALS, preset=0, amp='1,0,0,0')
    for i in range(1, num_partials + 1):
      amy.send(osc=base_osc + i, wave=amy.PARTIAL, amp='1,0,1,1')
    amy_send_at(time=50, osc=0, note=60, vel=0.1)
    amy_send_at(time=300, osc=0, note=67, vel=0.6)
    amy_send_at(time=550, osc=0, note=72, vel=1)
    amy_send_at(time=800, osc=0, vel=0)


class TestInterpPartialsRetrigger(AmyTest):

  def run(self):
    base_osc = 0
    num_partials = 20
    amy_send_at(time=0, osc=base_osc, wave=amy.INTERP_PARTIALS, preset=0, amp='1,0,0,0')
    for i in range(1, num_partials + 1):
      amy.send(osc=base_osc + i, wave=amy.PARTIAL)
    amy_send_at(time=50, osc=0, note=52, vel=0.7)
    amy_send_at(time=200, osc=0, note=52, vel=0.8)
    amy_send_at(time=350, osc=0, note=52, vel=0.9)
    amy_send_at(time=500, osc=0, vel=0)
    amy_send_at(time=510, osc=100, wave=amy.SINE, bp0='3,1,500,0,50,0')
    amy_send_at(time=550, osc=100, note=76, vel=1)
    amy_send_at(time=700, osc=100, note=76, vel=1)
    amy_send_at(time=850, osc=100, vel=0)


class TestInterpPartialsOutOfRange(AmyTest):
  """Notes above the top of the piano pitch table (MIDI 104) used to read out of
  bounds, which could permanently silence all AMY output.  They now extrapolate
  from the table edge, and in-range notes played afterwards must still sound.
  The out-of-range notes are quiet (vel=0.1) because extrapolated envelopes
  amplify cross-platform libm float differences; what matters here is that the
  out-of-range path runs, not how it sounds."""

  def run(self):
    amy_send_at(time=0, reset=amy.RESET_SYNTHS)
    amy_send_at(time=0, synth=1, num_voices=6, patch=256)
    t = 50
    for i in range(4):
      amy_send_at(time=t, synth=1, note=60 + i, vel=0.7)
      # Grouped by time (all three note-ons, then all three note-offs) rather
      # than by note, so the render clock (which can only move forward) sees
      # a non-decreasing sequence of times -- interleaving on/off per note
      # would ask it to rewind from t+150 back to t+50 on the next note.
      for note in (108, 112, 115):
        amy_send_at(time=t + 50, synth=1, note=note, vel=0.1)
      for note in (108, 112, 115):
        amy_send_at(time=t + 150, synth=1, note=note, vel=0)
      amy_send_at(time=t + 150, synth=1, note=60 + i, vel=0)
      t += 200
    # This final in-range note went permanently silent when the out-of-range
    # notes above read past the end of the tables.
    amy_send_at(time=t, synth=1, note=60, vel=1)


class TestSineEnv(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SINE, freq=1000)
    amy_send_at(time=0, osc=0, amp='1,0,1,1,0,0', bp0='50,1,200,0.1,50,0')
    amy_send_at(time=100, vel=.85)
    amy_send_at(time=500, vel=0)


class TestSineEnv2(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SINE, freq=1000)
    amy_send_at(time=0, osc=0, amp='1.0,0,1,1,0,0', bp0='0,0,200,5,200,0,0,0')
    amy_send_at(time=100, vel=1)
    amy_send_at(time=500, vel=0)
    # The DX7 algo is weird - attack is different from decay, envelope is clipped to 1.
    amy_send_at(time=500, osc=0, eg0_type=amy.ENVELOPE_DX7)
    amy_send_at(time=550, vel=1)
    amy_send_at(time=950, vel=0)


class TestSineAM(AmyTest):
  """Amplitude modulation was messed up by log-combination amp."""

  def run(self):
    amy_send_at(time=0, osc=1, wave=amy.SINE, freq=5)
    amy_send_at(time=0, osc=0, wave=amy.SINE, freq=1000, mod_source=1, amp='1,0,0,0,0,0.05')
    amy_send_at(time=100, vel=1)  # Needed to wake up osc, even though vel value is ignored
    amy_send_at(time=500, vel=0)  # Will not turn off osc since vel value is ignored / eg0 not engaged.
    amy_send_at(time=600, amp=0)  # Should silence osc.
    amy_send_at(time=800, amp=1)  # osc ready to go, but will still wait for nonzero vel note-on


class TestAlgo(AmyTest):

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=1, patch=21+128)
    amy_send_at(time=100, synth=1, note=58, vel=1)
    amy_send_at(time=500, synth=1, vel=0)


class TestAlgo2(AmyTest):

  def run(self):
    amy_send_at(time=0, volume=0.5)  # To counteract vel=2 without rewriting ref.
    amy_send_at(time=0, synth=1, num_voices=1, patch=128+24)
    amy_send_at(time=100, synth=1, note=58, vel=2)
    amy_send_at(time=500, synth=1, vel=0)


class TestWoodPiano(AmyTest):
  """Test 4-op FM voice to check unassigned voices behave OK."""

  def run(self):
    # The four-op WOOD PIANO patch
    amy_send_at(time=0, synth=1, num_voices=6, oscs_per_voice=5)
    amy_send_at(time=0, synth=1, osc=4, wave=amy.SINE, ratio=1, bp0='10,1,1000,0.8,100,0', amp='0.4,0,0,1', eg0_type=2, phase=0)
    amy_send_at(time=0, synth=1, osc=3, wave=amy.SINE, ratio=0.5, bp0='0,1,1000,0,100,0', amp='1,0,0,1', eg0_type=2, phase=0)
    amy_send_at(time=0, synth=1, osc=2, wave=amy.SINE, ratio=1, bp0='0,1,300,0.5,500,0.3,1000,0', amp='0.8,0,0,1,0,0', eg0_type=2, phase=0)
    amy_send_at(time=0, synth=1, osc=1, wave=amy.SINE, ratio=0.495, bp0='0,1,2000,0,300,0', amp='1,0,0,1,0,0', eg0_type=2, phase=0)
    # Osc 0 amp envelope is just to avoid truncating the FM output.
    amy_send_at(time=0, synth=1, osc=0, wave=amy.ALGO, algorithm=2, algo_source=',,4,3,2,1', bp0='0,1,1000,1,300,0', amp='4,0,1,1', freq='220,1,0,0,0,0')
    # Notes
    amy_send_at(time=100, synth=1, note=48, vel=1)
    amy_send_at(time=350, synth=1, note=48, vel=0)
    amy_send_at(time=400, synth=1, note=48, vel=1)
    amy_send_at(time=650, synth=1, note=48, vel=0)
    amy_send_at(time=700, synth=1, note=58, vel=1)
    amy_send_at(time=800, synth=1, note=58, vel=0)
    amy_send_at(time=800, synth=1, note=60, vel=1)
    amy_send_at(time=900, synth=1, note=60, vel=0)


class TestFMRepeat(AmyTest):
  """Douglas reports that the DX7 Marimba sometimes clicks at onset."""

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=1, patch=128+21)
    for i in range(5):
      t = 100 + round(i * 51200 / 441)
      amy_send_at(time=t, synth=1, note=32, vel=1)
      amy_send_at(time=t + 20, synth=1, vel=0)


class TestXanaduFM(AmyTest):
  """The Xanadu custom FM voice stopped working."""

  def run(self):
    amy.send(volume=100)
    #amy_send_at(time=0, osc=3, wave=amy.SINE, freq=1/7.5, phase=0.75, amp=.99)
    amy_send_at(time=0, osc=2, wave=amy.SINE, ratio=1, amp='0.5,0,0,0,0,0') #, mod_source=3)
    amy_send_at(time=0, osc=1, wave=amy.SINE, ratio=1, amp='1,0,0,1', bp0='1000,1,1000,0')
    amy_send_at(time=0, osc=0, wave=amy.ALGO, algorithm=1, algo_source=',,,,2,1', bp0='0,1,1000,1,2000,0')
    amy_send_at(time=100, osc=0, note=49, vel=1)
    amy_send_at(time=450, osc=0, note=49, vel=0)
    amy_send_at(time=550, osc=0, note=49, vel=1)
    amy_send_at(time=900, osc=0, note=49, vel=0)


class TestFilter(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy_send_at(time=100, note=48, vel=1.0)
    amy_send_at(time=900, vel=0)


class TestFilter24(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF24, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy_send_at(time=100, note=48, vel=1.0)
    amy_send_at(time=900, vel=0)


class TestFilterLFO(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=1, wave=amy.SINE, freq=6, amp=1.0)
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, mod_source=1, filter_freq='400,0,0,0,3,0.5', bp1='0,1,500,0,100,0')
    amy_send_at(time=100, note=48, vel=1.0)
    amy_send_at(time=500, vel=0)


class TestLFO(AmyTest):

  def run(self):
    # LFO mod used to be 1+x i.e. 0.9..1.1
    #amy_send_at(time=0, osc=1, wave=amy.SINE, freq=4, amp=0.1)
    # With unit-per-octave scaling, that's approx log2(0.9) = -0.152, log2(1.1) = 0.138
    amy_send_at(time=0, osc=1, wave=amy.SINE, freq=4, amp=0.138)
    amy_send_at(time=0, osc=0, wave=amy.SINE, mod_source=1, freq='0,1,0,0,0,1')
    amy_send_at(time=100, note=70, vel=1)
    amy_send_at(time=500, vel=0)


class TestDuty(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.PULSE, duty=0.1)
    amy_send_at(time=100, note=70, vel=1)
    amy_send_at(time=200, vel=0)
    amy_send_at(time=300, osc=0, wave=amy.PULSE, duty=0.9)
    amy_send_at(time=300, note=70, vel=1)
    amy_send_at(time=400, vel=0)


class TestPWM(AmyTest):

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.PULSE, mod_source=1, duty='0.5,0,0,0,0,0.25')
    amy_send_at(time=0, osc=1, wave=amy.SINE, freq=4, amp=1)
    amy_send_at(time=100, note=70, vel=1)
    amy_send_at(time=500, vel=0)


class TestGlobalEQ(AmyTest):

  def run(self):
    amy_send_at(time=0, eq="-10,10,3")
    amy_send_at(time=0, osc=0, wave=amy.SAW_UP)
    amy_send_at(time=100, note=46, vel=1)
    amy_send_at(time=500, vel=0)


class TestChorus(AmyTest):

  def run(self):
    # Turn on chorus.
    amy.send(chorus=1)
    # Note from TestFilter.
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy_send_at(time=100, note=48, vel=1.0)
    amy_send_at(time=900, vel=0)


class TestBrass(AmyTest):
  """One of the Juno-6 patches, spelled out."""

  def run(self):
    #amy_send_at(time=0, osc=0, wave=amy.SAW_UP, amp='0.85,0,1,1,0,0', freq='130.81,1,0,0,0,0', filter_type=amy.FILTER_LPF,
    #         resonance=0.167, bp0='60,1,740,0.9,200,0', filter_freq='6000,0.5,0,0,1,0',
    #         bp1='60,1,740,0.9,200,0')
    #amy_send_at(time=0, osc=0, wave=amy.SAW_UP, amp='0.85,0,1,1,0,0', freq='130.81,1,0,0,0,0', filter_type=amy.FILTER_LPF24,
    #         resonance=0.167, bp0='60,1,340,0.3,200,0', filter_freq='2000,0.5,0,0,4,0',
    #         bp1='60,1,340,0.3,200,0')
    osc_freq_str = str(constants.ZERO_LOGFREQ_IN_HZ / 2)
    amy_send_at(time=0, osc=1, wave=amy.SAW_UP, freq=osc_freq_str + ',1,0,0,0,0.02',
             amp='0.85,0,1,1,0,0', bp0='30,1,672,0.354,100,0',
             filter_type=amy.FILTER_LPF24, resonance=0.167,
             filter_freq='93.73,0.677,0,0,9.133,0', bp1='30,1,672,0.354,100,0',
             mod_source=2,
             )
    # Osc 2 is LFO for vibrato
    amy_send_at(time=0, osc=2, wave=amy.SINE, freq=3, bp0='156,1.0,100,1.0,100,0')
    amy_send_at(time=100, osc=1, note=76, vel=1.0)
    amy_send_at(time=300, osc=1, vel=0)
    amy_send_at(time=600, osc=1, note=76, vel=1.0)
    amy_send_at(time=800, osc=1, vel=0)
    # 'filter_freq': '93.73,0.677,0,0,4.567,0', 'bp1': '30,1,672,0.354,232,0'


class TestBrassAlt(AmyTest):
  """Reproduce TestBrass using the VCA on a SILENT osc."""

  def run(self):
    osc_freq_str = str(constants.ZERO_LOGFREQ_IN_HZ / 2)
    # Osc 1 is waveform, with vibrato mod but no amp env
    amy_send_at(time=0, osc=1, wave=amy.SAW_UP, freq=osc_freq_str + ',1,0,0,0,0.02',
             amp='1,0,0,0,0,0', mod_source=2)
    # Osc 2 is LFO for vibrato
    amy_send_at(time=0, osc=2, wave=amy.SINE, freq=3, bp0='156,1.0,100,1.0,100,0')
    # Osc 0 is VCF and amplitude env
    amy_send_at(time=0, osc=0, wave=amy.SILENT,
             amp='0.85,0,1,1,0,0', bp0='30,1,672,0.354,100,0',
             filter_type=amy.FILTER_LPF24, resonance=0.167,
             filter_freq='93.73,0.677,0,0,9.133,0', bp1='30,1,672,0.354,100,0',
             mod_source=2, chained_osc=1)
    amy_send_at(time=100, osc=0, note=76, vel=1.0)
    amy_send_at(time=300, osc=0, vel=0)
    amy_send_at(time=600, osc=0, note=76, vel=1.0)
    amy_send_at(time=800, osc=0, vel=0)


class TestBrass2(AmyTest):
  """Trying to catch the note-off thump."""

  def run(self):
    osc_freq_str = str(constants.ZERO_LOGFREQ_IN_HZ / 2)
    amy_send_at(time=0, osc=0, wave=amy.SAW_UP, amp='0.85,0,1,1', freq=osc_freq_str + ',1',
             resonance=0.713, filter_type=amy.FILTER_LPF24, filter_freq='93.726,0.677,0,0,9.134',
             bp0='30,1,672,0.354,232,0', bp1='30,1,672,0.354,232,0')
    amy_send_at(time=100, osc=0, note=60, vel=1.0)
    amy_send_at(time=600, osc=0, vel=0)


class TestGuitar(AmyTest):
  """Trying to catch the note-off zzzzzip."""

  def run(self):
    base_freq_str = str(constants.ZERO_LOGFREQ_IN_HZ / 2)
    amy_send_at(time=0, osc=0, wave=amy.SAW_UP, amp='0.756,0,1,1', freq=base_freq_str + ',1',
             filter_freq='16.23,0.236,0,0,11.181', resonance=0.753, filter_type=amy.FILTER_LPF24,
             bp0='6,1,51,0.425,153,0',
             bp1='6,1,51,0.425,153,0')
    amy_send_at(time=100, osc=0, note=60, vel=4.0)
    amy_send_at(time=150, osc=0, vel=0)
    amy_send_at(time=500, osc=0, note=60, vel=4.0)
    amy_send_at(time=550, osc=0, vel=0)


class TestBleep(AmyTest):
  """Test the tulip start-up beep."""

  def run(self):
    amy_send_at(time=0, wave=amy.SINE, freq=220)
    amy_send_at(time=0, osc=0, pan=0.9, vel=1)
    amy_send_at(time=150, osc=0, pan=0.1, freq=440)
    amy_send_at(time=300, osc=0, pan=0.5, vel=0)


class TestOverload(AmyTest):
  """Run the output very hot to check for clipping."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    amy_send_at(time=0, eq="12")
    amy_send_at(time=0, chorus=1)
    amy_send_at(time=100, note=48, vel=8.0)
    amy_send_at(time=900, vel=0)


class TestJunoPatch(AmyTest):
  """Known Juno patch."""

  def run(self):
    # Also test the synth mechanism.
    amy_send_at(time=0, synth=1, num_voices=4, patch=20)
    amy_send_at(time=50, synth=1, note=48, vel=1)
    amy_send_at(time=150, synth=1, note=60, vel=1)
    amy_send_at(time=250, synth=1, note=63, vel=1)
    amy_send_at(time=350, synth=1, note=67, vel=1)
    amy_send_at(time=600, synth=1, note=48, vel=0)
    amy_send_at(time=700, synth=1, note=60, vel=0)
    amy_send_at(time=800, synth=1, note=63, vel=0)
    amy_send_at(time=900, synth=1, note=67, vel=0)


class TestJunoClip(AmyTest):
  """Juno patch that used to clip until we trimmed the volumes in #802.  Now run with vel=5."""

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=4, patch=9)
    amy_send_at(time=50, synth=1, note=60, vel=5)
    amy_send_at(time=50, synth=1, note=57, vel=5)
    amy_send_at(time=50, synth=1, note=55, vel=5)
    amy_send_at(time=50, synth=1, note=52, vel=5)
    amy_send_at(time=800, synth=1, vel=0)
    amy_send_at(time=800, synth=1, vel=0)
    amy_send_at(time=800, synth=1, vel=0)
    amy_send_at(time=800, synth=1, vel=0)


class TestLowVcf(AmyTest):
  """Weird fxpt warble when hitting fundamental."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN,
             filter_type=amy.FILTER_LPF24, resonance=1.0,
             amp='0.85,0,1,1',
             filter_freq='161.28,0,0,0,5',
             bp0='0,1,0,0',
             bp1='0,1,600,0,1,0')
    amy_send_at(time=100, osc=0, note=48, vel=3)
    amy_send_at(time=800, osc=0, vel=0)


class TestLowerVcf(AmyTest):
  """Top16 LPF24 has issues with cf below fundamental?"""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN,
             filter_type=amy.FILTER_LPF24, resonance=4.0,
             amp='0.85,0,1,1',
             filter_freq='50,0,0,0,6',
             bp0='0,1,0,0',
             bp1='0,1,300,0,1,0')
    amy_send_at(time=100, osc=0, note=48, vel=3)
    amy_send_at(time=800, osc=0, vel=0)


class TestFlutesEq(AmyTest):
  """VCF leaving almost pure sine + HPF2 -> noise, clicks?"""

  def run(self):
    amy_send_at(time=0, eq="-15,8,8")
    osc_args = {'time':0, 'wave':amy.SAW_UP, 'filter_type':amy.FILTER_LPF24, 'resonance':1.75, 
        'bp0':'200,1,9800,0,100,0', 'bp1':'200,1,9800,0,100,0', 'filter_freq':'242,0.323'}
    amy_send_at(osc=0, **osc_args)
    amy_send_at(osc=1, **osc_args)
    amy_send_at(osc=2, **osc_args)
    amy_send_at(time=100, osc=0, note=48, vel=0.5)
    amy_send_at(time=200, osc=1, note=52, vel=0.5)
    amy_send_at(time=300, osc=2, note=55, vel=0.5)
    amy_send_at(time=900, osc=0, vel=0)
    amy_send_at(time=900, osc=1, vel=0)
    amy_send_at(time=900, osc=2, vel=0)


class TestOscBD(AmyTest):
  """Bass Drum as modulated sine-tone. amy.py:preset(5). """

  def run(self):
    # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
    amy_send_at(time=0, osc=1, wave=amy.SINE, amp=1, freq=0.25, phase=0.5)
    # Sine waveform always starts at phase 0 after retrigger.
    amy_send_at(time=0, osc=0, wave=amy.SINE, phase=0, bp0="0,1,500,0,0,0", freq=str(constants.ZERO_LOGFREQ_IN_HZ) + ",1,0,0,0,2", mod_source=1)
    amy_send_at(time=100, osc=0, note=84, vel=1)
    amy_send_at(time=350, osc=0, note=84, vel=1)
    amy_send_at(time=600, osc=0, note=84, vel=1)


class TestChainedOsc(AmyTest):
  """Two oscillators chained together behind a silent_osc."""

  def run(self):
    # TestFilter but on Saw + subosc with same envelope.
    osc_freq_str = str(constants.ZERO_LOGFREQ_IN_HZ / 2)
    #amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    #amy_send_at(time=0, osc=1, wave=amy.PULSE, filter_type=amy.FILTER_LPF, resonance=8.0, amp="0.2,0,1,1", freq="130.81,1", filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0')
    #amy_send_at(time=100, osc=0, note=48, vel=1.0)
    #amy_send_at(time=100, osc=1, note=48, vel=1.0)
    amy_send_at(time=0, osc=0, wave=amy.SILENT, filter_type=amy.FILTER_LPF, resonance=8.0, filter_freq='300,0,0,0,3', bp1='0,1,800,0.1,50,0.0', chained_osc=1)
    amy_send_at(time=0, osc=1, wave=amy.SAW_DOWN, chained_osc=2)
    amy_send_at(time=0, osc=2, wave=amy.PULSE, amp="0.2,0,1,1", freq=osc_freq_str + ',1,0,0,0,0,1')
    amy_send_at(time=100, osc=0, note=48, vel=1.0)
    #amy_send_at(time=100, osc=1, note=48, vel=1.0)
    amy_send_at(time=900, osc=0, vel=0)
    #amy_send_at(time=900, osc=1, vel=0)


class TestChainedModOsc(AmyTest):
  """A modulator that is itself modulated: slow LFO varies a vibrato LFO's depth."""

  def run(self):
    # osc 2: 1 Hz triangle, the "breathing" control signal.
    amy_send_at(time=0, osc=2, wave=amy.TRIANGLE, freq=1)
    # osc 1: 5 Hz vibrato LFO whose amplitude (i.e. vibrato depth) breathes
    # with osc 2 via its own mod_source.
    amy_send_at(time=0, osc=1, wave=amy.SINE, freq=5, amp={'const': 0.1, 'mod': 0.4}, mod_source=2)
    amy_send_at(time=0, osc=0, wave=amy.SINE, mod_source=1, freq={'mod': 0.1})
    amy_send_at(time=100, note=70, vel=1)
    amy_send_at(time=900, vel=0)


class TestModSourceLoopRejected(AmyTest):
  """A mod_source cycle (a->b->a) must be rejected at assignment, not recurse
  unboundedly at render time. The third send below would close a 1<->2 cycle and
  must be dropped; the render then proceeds identically to TestChainedModOsc."""

  def run(self):
    amy_send_at(time=0, osc=2, wave=amy.TRIANGLE, freq=1)
    amy_send_at(time=0, osc=1, wave=amy.SINE, freq=5, amp={'const': 0.1, 'mod': 0.4}, mod_source=2)
    amy_send_at(time=0, osc=2, mod_source=1)   # would close 1<->2: rejected
    amy_send_at(time=0, osc=0, wave=amy.SINE, mod_source=1, freq={'mod': 0.1})
    amy_send_at(time=100, note=70, vel=1)
    amy_send_at(time=900, vel=0)


class TestJunoTrumpetPatch(AmyTest):
  """I'm hearing a click in the Juno Trumpet patch.  Catch it."""

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=1, patch=2)
    amy_send_at(time=50, synth=1, note=60, vel=1)
    amy_send_at(time=200, synth=1, vel=0)
    amy_send_at(time=300, synth=1, note=60, vel=1)
    amy_send_at(time=450, synth=1, vel=0)


class TestJunoCheapTrumpetPatch(AmyTest):
  """Try out the 'cheap' LPF hack."""

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=2, patch=2)
    amy_send_at(time=0, synth=1, osc=0, filter_type=amy.FILTER_LPF)
    amy_send_at(time=50, synth=1, note=60, vel=1)
    amy_send_at(time=200, synth=1, vel=0)
    amy_send_at(time=300, synth=1, note=60, vel=1)
    amy_send_at(time=450, synth=1, vel=0)


class TestFilterReleaseGlitch(AmyTest):
  """See https://github.com/shorepine/amy/issues/126."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, filter_type=amy.FILTER_LPF24, filter_freq='100,0,0,6')
    amy_send_at(time=100, note=64, vel=1)
    amy_send_at(time=500, vel=0)


class TestPortamento(AmyTest):

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=3, patch=0)

    # Starting-point pitches...
    amy_send_at(time=50, synth=1, note=60, vel=1)
    amy_send_at(time=50, synth=1, note=64, vel=1)
    amy_send_at(time=50, synth=1, note=67, vel=1)

    # .. immediately start bending towards final pitches.
    amy_send_at(time=60, synth=1, osc=2, portamento=100)
    amy_send_at(time=60, synth=1, osc=3, portamento=100)
    amy_send_at(time=60, synth=1, osc=4, portamento=100)

    amy_send_at(time=60, synth=1, note=65, vel=1)
    amy_send_at(time=60, synth=1, note=69, vel=1)
    amy_send_at(time=60, synth=1, note=72, vel=1)

    amy_send_at(time=800, synth=1, vel=0)


class TestEcho(AmyTest):

  def run(self):
    amy.echo(level=0.5, delay_ms=200, feedback=0.7)
    amy_send_at(time=0, osc=0, bp0="0,1,200,0,0,0")

    amy_send_at(time=100, osc=0, note=48, vel=1)


class TestEchoLPF(AmyTest):

  def run(self):
    amy.echo(level=0.5, delay_ms=200, feedback=0.7, filter_coef=0.9)
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, bp0="0,1,200,0,0,0")

    amy_send_at(time=100, osc=0, note=48, vel=1)


class TestEchoHPF(AmyTest):

  def run(self):
    amy.echo(level=0.5, delay_ms=200, feedback=0.7, filter_coef=-0.9)
    amy_send_at(time=0, osc=0, wave=amy.SAW_DOWN, bp0="0,1,200,0,0,0")

    amy_send_at(time=100, osc=0, note=48, vel=1)


class TestVoiceManagement(AmyTest):
  """The 'synth' structure manages a set of voices."""

  def run(self):
    # Patch is bare sinewave oscillator but with a 100ms release.
    #amy.send(patch=1024, patch_string=amy.message(osc=0, wave=amy.SINE, bp0='0,1,1000,1,100,0'))
    #amy_send_at(time=10, synth=0, num_voices=3, patch=1024)
    patch_string = amy.message(osc=0, wave=amy.SINE, bp0='0,1,1000,0,100,0')
    amy_send_at(time=10, synth=0, num_voices=2, patch_string=patch_string)
    amy_send_at(time=100, synth=0, note=60, vel=1)
    amy_send_at(time=200, synth=0, note=72, vel=1)
    # Check if using the same string for a second synth reuses the same memory_patch (based on debug fprintfs).
    amy_send_at(time=200, synth=1, num_voices=1, patch_string=patch_string)
    amy_send_at(time=300, synth=1, note=84, vel=1)
    # We ran out of voices, this should steal the first one
    amy_send_at(time=400, synth=0, note=96, vel=1)
    # Stop one
    amy_send_at(time=500, synth=1, note=84, vel=0)
    # Stop all the rest - vel=0 without note= means all notes off.
    amy_send_at(time=600, synth=0, vel=0)


class TestVoiceStealing(AmyTest):
  """There's a bug with the default 6 voices."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    # Default juno synth.
    amy_send_at(time=40, synth=1, note=60, vel=1)
    amy_send_at(time=120, synth=1, note=64, vel=1)
    amy_send_at(time=200, synth=1, note=67, vel=1)
    amy_send_at(time=280, synth=1, note=70, vel=1)
    amy_send_at(time=360, synth=1, note=72, vel=1)
    amy_send_at(time=440, synth=1, note=76, vel=1)
    amy_send_at(time=520, synth=1, note=79, vel=1)
    amy_send_at(time=600, synth=1, note=82, vel=1)
    amy_send_at(time=800, synth=1, note=60, vel=0)
    amy_send_at(time=820, synth=1, note=64, vel=0)
    amy_send_at(time=840, synth=1, note=67, vel=0)
    amy_send_at(time=860, synth=1, note=70, vel=0)
    amy_send_at(time=880, synth=1, note=72, vel=0)
    amy_send_at(time=900, synth=1, note=76, vel=0)
    amy_send_at(time=920, synth=1, note=79, vel=0)
    amy_send_at(time=940, synth=1, note=82, vel=0)
    # Sent spurious note-offs, just to check that error report works
    print("expect to see excess note-off for notes 64, 82, 99")
    amy_send_at(time=940, synth=1, note=64, vel=0)
    amy_send_at(time=940, synth=1, note=82, vel=0)
    amy_send_at(time=940, synth=1, note=99, vel=0)


class TestVoiceStealDecay(AmyTest):
  """Issue #470 - voice stealing can cause clicks as note changes abruptly."""

  def run(self):
    amy.send(
      synth=0, num_voices=2,
      patch_string=amy.message(osc=0, wave=amy.SINE, bp0='50,1,200,0.5,50,0'),
    )
    amy_send_at(time=100, synth=0, note=40, vel=10)  # voice 0
    amy_send_at(time=200, synth=0, note=50, vel=2)   # voice 1
    amy_send_at(time=300, synth=0, note=60, vel=2)   # Steal voice 0, big click at t=0.3
    amy_send_at(time=400, synth=0, note=65, vel=2)   # Steal voice 1
    amy_send_at(time=500, synth=0, note=45, vel=10)  # Steal voice 0
    # Now add synth_delay
    amy_send_at(time=550, synth=0, synth_delay=50)
    amy_send_at(time=600, synth=0, note=70, vel=2)   # Steal voice 1
    amy_send_at(time=700, synth=0, note=75, vel=2)   # Steal voice 0, but no big click (at t=0.75)
    amy_send_at(time=800, synth=0, note=80, vel=2)   # Steal voice 1
    amy_send_at(time=900, synth=0, vel=0)  # All notes off


class TestVoiceStealClick(AmyTest):
  """There are still clicks on voice stealing and it's not cool."""

  def run(self):
    amy_send_at(time=0, synth=0, num_voices=1, oscs_per_voice=1)
    amy_send_at(time=0, synth=0, osc=0, wave=amy.SINE, bp0='0,0,100,1,200,0.8,500,0')
    # It's the filter being reset that's hurting us?
    amy_send_at(time=0, synth=0, osc=0, filter_type=amy.FILTER_LPF24, filter_freq='8000,0,0,0,0,0')
    # Send first chords
    amy_send_at(time=100, synth=0, note=60, vel=10)
    # Send second chords
    amy_send_at(time=500, synth=0, note=60, vel=10)
    # Notes off
    amy_send_at(time=800, synth=0, vel=0)

class TestOwBassClick(AmyTest):
  """Hearing clicks on OwBass??.  See https://github.com/shorepine/amy/issues/629. """

  def run(self):
    amy_send_at(time=0, synth=0, num_voices=1, oscs_per_voice=4)
    # Ow Bass reproduced by hand on AMYboard Editor.
    amy_send_at(time=10, synth=0, osc=0, wave=amy.SILENT, amp='1,,1,1', freq=220, filter_freq='20,1,,,5.443', resonance=4.381,
             filter_type=amy.FILTER_LPF24, bp0='13,1,0,1,16,0', bp1='16,1,0,0.878,52,0', mod_source=1, chained_osc=2)
    amy_send_at(time=10, synth=0, osc=1, wave=amy.TRIANGLE, amp=',,0', freq='2.3,0,,,,,0', bp0='5,1,100,1,10000,0')
    amy_send_at(time=10, synth=0, osc=2, wave=amy.PULSE, amp='0.551,,0', freq=220, duty=0.697, chained_osc=3, mod_source=1)
    amy_send_at(time=10, synth=0, osc=3, wave=amy.SAW_UP, amp='0.551,,0', freq=110, mod_source=1)
    amy_send_at(time=10, eq='7,-3,-3', echo='M0,500,,0,0', chorus='0,320,0.5,0.5')
    # Rapid repeated notes.
    amy_send_at(time=100, synth=0, note=48, vel=1)
    amy_send_at(time=250, synth=0, note=48, vel=0)
    amy_send_at(time=350, synth=0, note=48, vel=1)
    amy_send_at(time=500, synth=0, note=48, vel=0)
    amy_send_at(time=600, synth=0, note=48, vel=1)
    amy_send_at(time=750, synth=0, note=48, vel=0)


class TestMidiDrums(AmyTest):
  """Test MIDI drums on channel 10 via injection."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    # amy_inject_midi_at args are (time, midi_status_byte, midi_note, midi_vel)
    amy_inject_midi_at(100, 0x99, 35, 100)  # bass
    amy_inject_midi_at(400, 0x99, 35, 100)  # bass
    amy_inject_midi_at(400, 0x99, 37, 100)  # snare
    amy_inject_midi_at(700, 0x99, 37, 100)  # snare
    amy_inject_midi_at(900, 0x89, 37, 100)  # snare note off


class TestMidiDrumsPatch258(AmyTest):
  """Test MIDI drums on channel 1 using patch 258 via injection.  Should match TestMidiDrums."""

  def __init__(self):
    super().__init__()
    self.default_synths = True  # Need default synths to set chorus, EQ like TestMidiDrums

  def run(self):
    # The MIDI drums default has amp=5
    amy_send_at(time=0, synth=1, num_voices=1, patch=258)
    # amy_inject_midi_at args are (time, midi_status_byte, midi_note, midi_vel)
    amy_inject_midi_at(100, 0x90, 35, 100)  # bass
    amy_inject_midi_at(400, 0x90, 35, 100)  # bass
    amy_inject_midi_at(400, 0x90, 37, 100)  # snare
    amy_inject_midi_at(700, 0x90, 37, 100)  # snare
    amy_inject_midi_at(750, 0x80, 37, 100)  # snare note off (should be ignored)


class TestMidiRunningStatusClock(AmyTest):
  """Running status must survive System Real-Time bytes interleaved into the
  stream. Regression for shorepine/amy#747/#748: an interleaved MIDI clock
  (0xF8) or active-sensing (0xFE) byte used to clobber the stored status byte,
  so the following running-status note was silently dropped. Here three notes
  are sent with the status byte (0x90) given only once -- a clock byte sits
  before note 2 and an active-sensing byte before note 3 -- and all three
  (60, 62, 64) must sound."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    amy.inject_midi_bytes([
        0x90, 60, 100,   # note on, channel 1, note 60, velocity 100
        0xF8,            # interleaved MIDI clock (real-time) -- must not break running status
        62, 100,         # running status: note on 62 (status byte not resent)
        0xFE,            # interleaved active sensing (real-time)
        64, 100,         # running status: note on 64
    ])


class TestDefaultChan1Synth(AmyTest):
  """Test default setup of Juno synth on synth 1 (MIDI channel 1)."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    amy_send_at(time=100, synth=1, note=60, vel=1)
    amy_send_at(time=300, synth=1, note=63, vel=1)
    amy_send_at(time=500, synth=1, note=67, vel=1)
    amy_send_at(time=700, synth=1, note=74, vel=1)
    amy_send_at(time=800, synth=1, note=60, vel=0)
    amy_send_at(time=850, synth=1, note=63, vel=0)
    amy_send_at(time=900, synth=1, note=67, vel=0)
    amy_send_at(time=950, synth=1, note=74, vel=0)


class TestSynthDrums(AmyTest):
  """Test MIDI drums using synth-level note translation."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    amy_send_at(time=100, synth=10, note=35, vel=100/127)  # bass
    amy_send_at(time=400, synth=10, note=35, vel=100/127)  # bass
    amy_send_at(time=400, synth=10, note=37, vel=100/127)  # snare
    amy_send_at(time=700, synth=10, note=37, vel=100/127)  # snare
    amy_send_at(time=900, synth=10, note=37, vel=0)  # snare note off - ignored with current setup.


class TestSynthDrumsChan20(AmyTest):
  """Test MIDI drums using synth-level note translation on channel beyond base MIDI 16."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    synth = 20
    # The same GM drum kit the default synths put on channel 10, but on a synth
    # number no MIDI status byte could carry -- output should be identical to
    # TestSynthDrums.
    amy_send_at(time=50, synth=synth, patch=258)
    amy_send_at(time=100, synth=synth, note=35, vel=100/127)  # bass
    amy_send_at(time=400, synth=synth, note=35, vel=100/127)  # bass
    amy_send_at(time=400, synth=synth, note=37, vel=100/127)  # snare
    amy_send_at(time=700, synth=synth, note=37, vel=100/127)  # snare
    amy_send_at(time=900, synth=synth, note=37, vel=0)  # snare note off - ignored with current setup.


class TestBlueMonday(AmyTest):
  """Re-initing PCM samples was leading to discontinuity; test new mechanism to delay until zero crossing."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    amy_send_at(time=50, chorus=0)
    amy_send_at(time=100, synth=10, note=36, vel=120/127)  # bass
    amy_send_at(time=190, synth=10, note=36, vel=100/127)  # bass
    amy_send_at(time=280, synth=10, note=36, vel=100/127)  # bass
    amy_send_at(time=370, synth=10, note=36, vel=100/127)  # bass
    amy_send_at(time=460, synth=10, note=36, vel=120/127)  # bass
    amy_send_at(time=550, synth=10, note=36, vel=100/127)  # bass
    amy_send_at(time=640, synth=10, note=36, vel=100/127)  # bass
    amy_send_at(time=730, synth=10, note=36, vel=100/127)  # bass
    amy_send_at(time=820, synth=10, note=36, vel=120/127)  # bass


class TestSynthDrumsPanning(AmyTest):
  """Test synth-level note translation propagates other fields."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    amy_send_at(time=100, synth=10, note=50, vel=1, pan=0.95)
    amy_send_at(time=200, synth=10, note=50, vel=1, pan=0.85)
    amy_send_at(time=300, synth=10, note=47, vel=1, pan=0.7)
    amy_send_at(time=400, synth=10, note=47, vel=1, pan=0.55)
    amy_send_at(time=500, synth=10, note=45, vel=1, pan=0.45)
    amy_send_at(time=600, synth=10, note=45, vel=1, pan=0.3)
    amy_send_at(time=700, synth=10, note=41, vel=1, pan=0.15)
    amy_send_at(time=800, synth=10, note=41, vel=1, pan=0.05)


class TestSynthDrumsLevel(AmyTest):
  """Per-channel level (amp CONST — MIDI CC 7 / a mixer Level control) must
  scale kit drums and persist across hits: the kit note templates carry their
  per-drum gain in the mapping's velocity scale, NOT in amp, so a hit never
  rewrites the channel level (shorepine/amy drum-level fix)."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    # Same snare at three channel levels; each pair of hits checks the level
    # survives the previous note-on.
    amy_send_at(time=50, synth=10, amp=0.2)
    amy_send_at(time=100, synth=10, note=37, vel=1)
    amy_send_at(time=250, synth=10, note=37, vel=1)
    amy_send_at(time=400, synth=10, amp=1.0)
    amy_send_at(time=450, synth=10, note=37, vel=1)
    amy_send_at(time=600, synth=10, note=37, vel=1)
    amy_send_at(time=750, synth=10, amp=1.8)
    amy_send_at(time=800, synth=10, note=37, vel=1)


class TestSynthLevel(AmyTest):
  """Per-instrument level (iV / synth_level): scales every osc of a synth at
     render, default 1. Instrument-layer commands apply at receive time (not
     schedulable), so levels are set up front and contrasted across synths."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    amy_send_at(time=0, synth=2, num_voices=2, patch=0)
    amy_send_at(time=0, synth=2, synth_level=0.25)   # same patch as synth 1, quarter level
    amy_send_at(time=0, synth=10, synth_level=0.5)   # GM drums: scales every per-drum osc
    amy_send_at(time=100, synth=1, note=60, vel=1)   # full level
    amy_send_at(time=300, synth=1, vel=0)
    amy_send_at(time=400, synth=2, note=60, vel=1)   # quarter level
    amy_send_at(time=600, synth=2, vel=0)
    amy_send_at(time=700, synth=10, note=36, vel=1)
    amy_send_at(time=850, synth=10, note=42, vel=1)


class TestSynthDrumsStaticParams(AmyTest):
  """Under the #913 one-osc-per-drum structure, we should be able to set persistent params per drum sound (see #914)."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    CLAP = 39
    HIHAT = 42
    amy_send_at(time=100, synth=10, note=CLAP, vel=1)
    amy_send_at(time=200, synth=10, note=HIHAT, vel=1)
    # Move clap to the left
    amy_send_at(time=250, synth=10, note=CLAP, pan=0)
    amy_send_at(time=300, synth=10, note=CLAP, vel=1)
    # Move hihat to the right
    amy_send_at(time=400, synth=10, note=HIHAT, vel=1)
    amy_send_at(time=450, synth=10, note=HIHAT, pan=1)
    amy_send_at(time=500, synth=10, note=CLAP, vel=1)
    amy_send_at(time=600, synth=10, note=HIHAT, vel=1)


class TestSynthProgChange(AmyTest):
  """Test switchting default synth to DX7, do oscs allocate OK?"""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    # DX7 first patch, uses 9 oscs/voice, num_voices is inherited from previous init.
    amy_send_at(time=0, synth=1, patch=128)
    amy_send_at(time=100, synth=1, note=60, vel=1)
    amy_send_at(time=300, synth=1, note=63, vel=1)
    amy_send_at(time=500, synth=1, note=67, vel=1)
    amy_send_at(time=700, synth=1, note=74, vel=1)
    amy_send_at(time=800, synth=1, note=60, vel=0)
    amy_send_at(time=850, synth=1, note=63, vel=0)
    amy_send_at(time=900, synth=1, note=67, vel=0)
    amy_send_at(time=950, synth=1, note=74, vel=0)


class TestDoubleNoteOff(AmyTest):
  """Test for bug where release restarts if a second note-off is received (#319)."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SINE, bp0='0,1,100,1,1000,0')
    amy_send_at(time=100, osc=0, note=60, vel=1)
    amy_send_at(time=200, osc=0, vel=0)
    amy_send_at(time=600, osc=0, vel=0)


class TestSustainPedal(AmyTest):
  """Test sustain pedal."""

  def run(self):
    amy_send_at(time=0, reset=amy.RESET_SYNTHS)
    amy_send_at(time=0, synth=1, num_voices=4, patch=256)
    amy_send_at(time=50, synth=1, note=76, vel=1)
    amy_send_at(time=100, synth=1, note=76, vel=0)
    amy_send_at(time=150, synth=1, pedal=127)
    amy_send_at(time=250, synth=1, note=63, vel=1)
    amy_send_at(time=300, synth=1, note=63, vel=0)
    amy_send_at(time=450, synth=1, note=67, vel=1)
    amy_send_at(time=500, synth=1, note=67, vel=0)
    amy_send_at(time=650, synth=1, note=72, vel=1)   # This note is held across the pedal release
    amy_send_at(time=750, synth=1, pedal=0)
    amy_send_at(time=900, synth=1, note=72, vel=0)


class TestPatchFromEvents(AmyTest):
  """Test defining a patch from events with patch_number."""

  def __init__(self):
    super().__init__()
    self.default_synths = True   # So that the patch space is already partly populated.

  def run(self):
    osc_freq = constants.ZERO_LOGFREQ_IN_HZ / 2
    amy_send_at(time=0, patch=1039, reset=amy.RESET_PATCH)
    amy_send_at(time=0, patch=1039, osc=0, wave=amy.SAW_DOWN, bp0='0,1,1000,0.1,200,0', chained_osc=1)
    amy_send_at(time=0, patch=1039, osc=1, wave=amy.SINE, freq=osc_freq, bp0='0,1,500,0,200,0')
    amy_send_at(time=0, synth=0, num_voices=4, patch=1039)
    amy_send_at(time=100, synth=0, note=60, vel=1)
    amy_send_at(time=300, synth=0, note=64, vel=1)
    amy_send_at(time=500, synth=0, note=67, vel=1)
    amy_send_at(time=800, synth=0, vel=0)


class TestInvalidPatchNumber(AmyTest):
  """Test for crash with patch number out of range."""

  def run(self):
    patch = 25
    osc_freq = constants.ZERO_LOGFREQ_IN_HZ / 2
    print("expect to see 'patch number %d is out of range' twice" % patch)
    amy_send_at(time=0, patch=patch, osc=0, wave=amy.SAW_DOWN, bp0='0,1,1000,0.1,200,0', chained_osc=1)
    amy_send_at(time=0, patch=patch, osc=1, wave=amy.SINE, freq=osc_freq, bp0='0,1,500,0,200,0')
    amy_send_at(time=0, synth=0, num_voices=4, patch=patch)
    amy_send_at(time=100, synth=0, note=60, vel=1)
    amy_send_at(time=300, synth=0, note=64, vel=1)
    amy_send_at(time=500, synth=0, note=67, vel=1)
    amy_send_at(time=800, synth=0, vel=0)


class TestBreakpointsRealloc(AmyTest):
  """A default osc has only 8 breakpoints, but it should realloc to 24 if you try to set a long bpset."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SINE, bp0='100,1,100,0,100,1,100,0,100,1,100,0,100,1,100,0')
    amy_send_at(time=100, osc=0, note=60, vel=1)
    amy_send_at(time=900, osc=0, vel=0)


class TestFileTransfer(AmyTest):

  def test(self):
    _amy.stop()
    _amy.start(0)
    payload = ''.join(random.choice(string.ascii_letters + string.digits) for _ in range(2048))
    with tempfile.NamedTemporaryFile(mode='w', delete=True) as f:
      with tempfile.NamedTemporaryFile(mode='w+', delete=True) as g:
        f.write(payload)
        f.flush()
        os.fsync(f.fileno())
        amy.transfer_file(f.name, g.name)
        amy.render(0.1)
        g.seek(0)
        transferred = g.read()
        if transferred != payload:
          is_ok = False
          message = 'transfer file contents mismatch'
        else:
          is_ok = True
          message = 'TestFileTransfer: ok'
        return is_ok, message


class TestDiskSample(AmyTest):

  def run(self):
    amy.disk_sample('sounds/partial_sources/CL SHCI A3.wav', preset=1024, midinote=57)
    amy_send_at(time=50, osc=0, preset=1024, wave=amy.PCM_MIX, vel=2, note=57)


class TestDiskSampleWithSilentGap(AmyTest):

  def run(self):
    amy.disk_sample('sounds/partial_sources/CL SHCI A3 with gap.wav', preset=1024, midinote=57)
    amy_send_at(time=50, osc=0, preset=1024, wave=amy.PCM_MIX, vel=2, note=63)


class TestRestartFileSample(AmyTest):

  def run(self):
    amy.disk_sample('sounds/partial_sources/CL SHCI A3.wav', preset=1024, midinote=60)
    amy_send_at(time=50, osc=0, preset=1024, wave=amy.PCM_MIX, vel=2, note=72)
    amy_send_at(time=500, osc=0, preset=1024, wave=amy.PCM_MIX, vel=2, note=50)


class TestDiskSampleStopsOnNoteOff(AmyTest):
  """A streamed preset must honor the immediate-stop note-off like an
  in-memory one does. pcm_note_off stops by seeking phase to the end, but
  render_pcm resets phase to 0 every block for file presets, so the stop
  used to be discarded and the clip played on to end-of-file. Note-off at
  100ms, sample is ~256ms, so a regression shows up as extra audio."""

  def run(self):
    amy.disk_sample('sounds/partial_sources/CL SHCI A3.wav', preset=1024, midinote=57)
    amy_send_at(time=50, osc=0, preset=1024, wave=amy.PCM_MIX, vel=2, note=57)
    amy_send_at(time=100, osc=0, vel=0)


class TestDiskSampleLoopModeRefused(AmyTest):
  """A streamed preset can't loop (no seekable table), so asking for PCM_LOOP
  is refused outright rather than accepted and quietly not looped. The mode
  is the half that gets dropped, so the osc keeps the preset and its default
  PCM_PLAY_STOP -- which means note-off stops it. Output should therefore be
  identical to TestDiskSampleStopsOnNoteOff, which asks for no mode at all."""

  def run(self):
    amy.disk_sample('sounds/partial_sources/CL SHCI A3.wav', preset=1024, midinote=57)
    amy_send_at(time=50, osc=0, preset=1024, wave=amy.PCM_MIX, vel=2, note=57,
                mode=amy.PCM_LOOP)
    amy_send_at(time=100, osc=0, vel=0)


class TestDiskSampleStereo(AmyTest):

  def run(self):
    amy.disk_sample('sounds/220_440_stereo.wav', preset=1024, midinote=60)
    amy.disk_sample('sounds/220_440_stereo.wav', preset=1025, midinote=60)
    amy_send_at(time=50, osc=0, preset=1024, wave=amy.PCM_LEFT, pan=0, vel=1, note=60)
    amy_send_at(time=500, osc=1, preset=1025, wave=amy.PCM_RIGHT, pan=1, vel=1, note=60)


class TestLoadSample(AmyTest):
  """amy.load_sample streams base64 chunks via _send_transfer_chunk ->
  _send_wire_from_sysex, which is the only route that marks them as
  transfer payload (amy_add_message_with_sysex_flag -> parse_transfer_message).
  Regression coverage for that routing: send them unmarked and every chunk
  is parsed as ordinary wire commands instead, so the preset never gets its
  sample data and this renders silence."""

  def run(self):
    amy.reset()
    amy.load_sample('sounds/partial_sources/CL SHCI A3.wav', preset=1024, midinote=57)
    amy_send_at(time=50, osc=0, preset=1024, wave=amy.PCM_MIX, vel=2, note=57)


class TestSample(AmyTest):

  def run(self):
    amy.start_sample(preset=1024, source=amy.SAMPLE_FROM_OUTPUT, max_frames=22050, midinote=60)
    amy_send_at(time=0, synth=1, num_voices=4, patch=20)
    amy_send_at(time=50, synth=1, note=48, vel=1)
    amy_send_at(time=150, synth=1, note=60, vel=1)
    amy_send_at(time=250, synth=1, note=63, vel=1)
    # notes off
    amy_send_at(time=400, synth=1, note=48, vel=0)
    amy_send_at(time=400, synth=1, note=60, vel=0)
    amy_send_at(time=400, synth=1, note=63, vel=0)

    # play a pitched up version
    amy_send_at(osc=116, time=400, preset=1024, wave=amy.PCM_MIX, vel=1, note=72)
    amy_send_at(osc=116, time=700, preset=1024, wave=amy.PCM_MIX, vel=2, note=84)


class TestParamsInPatchCmd(AmyTest):

  def run(self):
    # Is it possible to *modify* a patch in the same command we install it?
    amy_send_at(time=0, synth=1, patch=0, num_voices=4, resonance=4)
    amy_send_at(time=50, synth=1, note=48, vel=1)
    amy_send_at(time=150, synth=1, note=60, vel=1)
    amy_send_at(time=250, synth=1, note=63, vel=1)
    # notes off
    amy_send_at(time=400, synth=1, note=48, vel=0)
    amy_send_at(time=400, synth=1, note=60, vel=0)
    amy_send_at(time=400, synth=1, note=63, vel=0)


class TestHPFHighBaseFreq(AmyTest):

  def run(self):
    amy_send_at(time=0, synth=1, patch=0, num_voices=4)
    amy_send_at(time=10, synth=1, osc=0, filter_type=amy.FILTER_HPF, filter_freq=1000)
    amy_send_at(time=50, synth=1, note=48, vel=10)
    amy_send_at(time=150, synth=1, note=60, vel=10)
    amy_send_at(time=250, synth=1, note=63, vel=10)
    # notes off
    amy_send_at(time=400, synth=1, note=48, vel=0)
    amy_send_at(time=400, synth=1, note=60, vel=0)
    amy_send_at(time=400, synth=1, note=63, vel=0)


class TestNotchFilter(AmyTest):

  def run(self):
    amy_send_at(time=0, synth=1, patch=0, num_voices=4)
    amy_send_at(time=10, synth=1, osc=0, filter_type=amy.FILTER_NOTCH, resonance=2)
    amy_send_at(time=20, synth=1, filter_freq='400,1,0,0,4', eg1='0,1,1000,0,100,0')
    amy_send_at(time=100, synth=1, note=72, vel=10)
    amy_send_at(time=800, synth=1, note=72, vel=0)


class TestWavetable(AmyTest):
  """Simple exercise of the wavetable oscillator, using default wavetable."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.WAVETABLE, duty='0.25,0,0,0,0.5', bp1='0,0,800,1,100,1', bp0='50,1,50,0')
    # preset=19,
    amy_send_at(time=50, note=50, vel=1)
    amy_send_at(time=850, vel=0)


class TestCopyingSynthConfig(AmyTest):
  """Duplicates TestJunoPatch, but after copying synth 1 to synth 3."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    # Make sure we've executed the default synth setup.
    amy_send_at(time=10, osc=0, amp=1)
    commands = amy.get_synth_commands(synth=1, dest_synth=3, num_voices=4, time=0)
    #print(commands)
    amy.send_raw(commands)
    amy_send_at(time=50, synth=3, note=48, vel=1)
    amy_send_at(time=150, synth=3, note=60, vel=1)
    amy_send_at(time=250, synth=3, note=63, vel=1)
    amy_send_at(time=350, synth=3, note=67, vel=1)
    amy_send_at(time=600, synth=3, note=48, vel=0)
    amy_send_at(time=700, synth=3, note=60, vel=0)
    amy_send_at(time=800, synth=3, note=63, vel=0)
    amy_send_at(time=900, synth=3, note=67, vel=0)


class TestGetSynthCommandsGetsBus(AmyTest):

  def test(self):
    _amy.stop()
    _amy.start(0)
    amy_send_at(time=0, synth=1, num_voices=4, oscs_per_voice=2)
    amy_send_at(time=0, synth=1, osc=0, wave=amy.SINE, freq=110, chained_osc=1)
    amy_send_at(time=0, synth=1, osc=1, wave=amy.SAW_UP, freq=880)
    amy_send_at(time=0, bus=2, volume=0.1)
    amy_send_at(time=0, synth=1, bus=2)
    amy_send_at(time=0, synth=1, echo=0.5)
    amy.render(1)  # Let the events execute.
    commands = amy.get_synth_commands(1)
    expected = """iv4in2Z
v0f110c1Z
v1w3f880Z
y2V0.1x0,0,0M0.5,500,,0,0k0,320,0.5,0.5h0,0.85,0.5,3000Z"""
    if commands != expected:
      is_ok = False
      message = self.__class__.__name__ + ': get_synth_commands mismatch: expected:\n++\n%s\n--\n;saw:\n++\n%s\n--;' % (expected, commands)
    else:
      is_ok = True
      message = self.__class__.__name__ + ' : ok'
    return is_ok, message


class TestGetSynthCommandsGetsMidiCcs(AmyTest):

  def test(self):
    _amy.stop()
    _amy.start(0)
    amy_send_at(time=0, synth=1, num_voices=4, oscs_per_voice=2)
    amy_send_at(time=0, synth=1, osc=0, wave=amy.SINE, freq=110, chained_osc=1)
    amy_send_at(time=0, synth=1, osc=1, wave=amy.SAW_UP, freq=880)
    amy.send_raw('i1ic5,0,0,10,0,hello')
    amy.send_raw('i1ic10,1,1,100,1,i%id%v')
    amy.render(1)  # Let the events execute.
    commands = amy.get_synth_commands(1)
    expected = """iv4in2Z
v0f110c1Z
v1w3f880Z
y0V1x0,0,0M0,500,,0,0k0,320,0.5,0.5h0,0.85,0.5,3000Z
ic5,0,0,10,0,helloZ
ic10,1,1,100,1,i%id%vZ"""
    if commands != expected:
      is_ok = False
      message = self.__class__.__name__ + ': get_synth_commands mismatch: expected:\n++\n%s\n--\n;saw:\n++\n%s\n--;' % (expected, commands)
    else:
      is_ok = True
      message = self.__class__.__name__ + ' : ok'
    return is_ok, message


class TestClearMidiCCs(AmyTest):
  """Test that ic255 clears the MIDI CC settings."""

  def test(self):
    _amy.stop()
    _amy.start(0)
    amy_send_at(time=0, synth=1, num_voices=4, oscs_per_voice=2)
    amy_send_at(time=0, synth=1, osc=0, wave=amy.SINE, freq=110, chained_osc=1)
    amy_send_at(time=0, synth=1, osc=1, wave=amy.SAW_UP, freq=880)
    amy.send_raw('i1ic5,0,0,10,0,hello')
    amy.send_raw('i1ic10,1,1,100,1,i%id%v')
    # Test that you can have other commands after the ic255 too.
    amy.send_raw('i1ic255v0f220')
    amy.render(1)  # Let the events execute.
    commands = amy.get_synth_commands(1)
    expected = """iv4in2Z
v0f220c1Z
v1w3f880Z
y0V1x0,0,0M0,500,,0,0k0,320,0.5,0.5h0,0.85,0.5,3000Z"""
    if commands != expected:
      is_ok = False
      message = self.__class__.__name__ + ' : get_synth_commands mismatch: expected:\n++\n%s\n--\n;saw:\n++\n%s\n--;' % (expected, commands)
    else:
      is_ok = True
      message = self.__class__.__name__ + ' : ok'
    return is_ok, message

class TestClearOneMidiCC(AmyTest):
  """Test that ic5 with no further args clears that MIDI CC."""

  def test(self):
    _amy.stop()
    _amy.start(0)
    amy_send_at(time=0, synth=1, num_voices=4, oscs_per_voice=2)
    amy_send_at(time=0, synth=1, osc=0, wave=amy.SINE, freq=110, chained_osc=1)
    amy_send_at(time=0, synth=1, osc=1, wave=amy.SAW_UP, freq=880)
    amy.send_raw('i1ic5,0,0,10,0,hello')
    amy.send_raw('i1ic10,1,1,100,1,i%id%v')
    amy.send_raw('i1ic5v0f220')
    amy.render(1)  # Let the events execute.
    commands = amy.get_synth_commands(1)
    expected = """iv4in2Z
v0f220c1Z
v1w3f880Z
y0V1x0,0,0M0,500,,0,0k0,320,0.5,0.5h0,0.85,0.5,3000Z
ic10,1,1,100,1,i%id%vZ"""
    if commands != expected:
      is_ok = False
      message = self.__class__.__name__ + ' : get_synth_commands mismatch: expected:\n++\n%s\n--\n;saw:\n++\n%s\n--;' % (expected, commands)
    else:
      is_ok = True
      message = self.__class__.__name__ + ' : ok'
    return is_ok, message


def send_midi(time=0, synth=1, note=60, vel=0):
  """Wrap MIDI note injection with an amy.send-like interface."""
  amy_inject_midi_at(time, 0x90 + (synth - 1), note, min(127, int(round(vel * 127))))


class TestOscResetIsScheduled(AmyTest):
  """There was a (mostly test-relevant) bug where the osc resets implied by num_voices=0 were being applied immediately, not when scheduled."""

  def run(self):
    # First install MIDI drums: chan 1
    amy_send_at(time=0, synth=1, patch=258)
    send_midi(time=100, synth=1, note=54, vel=1)
    send_midi(time=200, synth=1, note=66, vel=1)
    # But clearing the synth later would mess it up.
    amy_send_at(time=500, synth=1, num_voices=0)


class TestClearSynth(AmyTest):
  """Test that setting num_voices to zero totally clears a synth."""

  def run(self):
    # First install MIDI drums: chan 1
    amy_send_at(time=0, synth=1, patch=258)
    send_midi(time=100, synth=1, note=54, vel=1)

    # Clear the synth
    amy_send_at(time=250, synth=1, num_voices=0)
    send_midi(time=300, synth=1, note=54, vel=1)  # Should do nothing & print warning
    # Set up a new synth.  Play some of the drum-remapped notes to make sure drum remappings have been cleared.
    amy_send_at(time=350, synth=1, num_voices=6, patch=0)
    send_midi(time=400, synth=1, note=54, vel=1)
    send_midi(time=500, synth=1, note=56, vel=1)
    send_midi(time=600, synth=1, note=58, vel=1)
    send_midi(time=700, synth=1, note=54, vel=0)
    send_midi(time=800, synth=1, note=56, vel=0)
    send_midi(time=900, synth=1, note=58, vel=0)


class TestResetOscs(AmyTest):
  """Test that setting the number of oscs per voice resets the oscs."""

  def run(self):
    amy_send_at(time=0, synth=1, patch=0, num_voices=4)
    amy_send_at(time=10, synth=1, oscs_per_voice=5)  # Should cause oscs to reset.
    amy_send_at(time=50, synth=1, note=48, vel=1)    # Will sound all the oscs in unison (post #716)
    amy_send_at(time=400, synth=1, note=48, vel=0)


class TestPreset257(AmyTest):
  """There was a bug in the setting of K257 (amyboard web editor baseline)."""

  def run(self):
    amy_send_at(time=0, synth=1, patch=257, num_voices=4)
    amy_send_at(time=100, synth=1, note=48, vel=1)
    amy_send_at(time=500, synth=1, vel=0)


class TestPreset257MidiCCs(AmyTest):
  """Preset 257 now includes a range of default MIDI CC mappings."""

  def run(self):
    amy_send_at(time=0, synth=1, patch=257, num_voices=4)
    amy_send_at(time=100, synth=1, note=48, vel=1)
    # amy_inject_midi_at args are (time, midi_status_byte, midi_note, midi_vel)
    amy_inject_midi_at(400, 0xB0, 74, 127)  # Make the VCF freq be low - 1/4 of the way from 20 to 8000, so 89 Hz
    amy_send_at(time=800, synth=1, vel=0)


class TestProgChangeOnPreset257(AmyTest):
  """MIDI Program Change while on the AMYboard Web Editor base patch (257).

  With no prior Bank Select, AMY infers the bank from the current patch number.
  Patch 257 used to infer bank 2, so a PC selected an undefined patch in the
  258-383 range and the synth went silent (issue #758).  The inferred bank now
  falls back to bank 0 (Juno), so the note below sounds (Juno patch 5)."""

  def run(self):
    # AMYboard-style: synth 1 starts on the Web Editor base patch (257).
    amy.send(synth=1, patch=257, num_voices=1)
    # Program Change to program 5, no prior Bank Select.  Used to map to patch
    # 261 (undefined -> silent); now maps to Juno patch 5.
    amy.inject_midi_bytes([0xC0, 5])
    amy_send_at(time=100, synth=1, note=60, vel=1)
    amy_send_at(time=600, synth=1, note=60, vel=0)


class TestChangeSustain(AmyTest):
  """Check that you can rewrite just the sustain level in an EG without rewriting it all."""

  def run(self):
    amy_send_at(time=0, synth=1, patch=257, num_voices=4)
    amy_send_at(time=10, synth=1, bp0=',,,0.8', bp1=',,,0.8')
    amy_send_at(time=100, synth=1, note=48, vel=1)
    amy_send_at(time=500, synth=1, vel=0)


def float_or_val(str, error_val=None):
  """Like float, but returns None if string doesn't parse."""
  try:
    return float(str)
  except ValueError:
    return error_val


class TestResetPreset(AmyTest):
  """Setting a synth to a patch should rewrite all the params even if it's the same patch."""

  def run(self):
    amy_send_at(time=0, synth=1, patch=257, num_voices=4)
    amy_send_at(time=10, synth=1, bp0=',,,0.8', bp1=',,,0.8')
    amy_send_at(time=20, synth=1, patch=257)
    amy_send_at(time=100, synth=1, note=48, vel=1)
    amy_send_at(time=500, synth=1, vel=0)
    amy_send_at(time=750, synth=1, note=48, vel=1)
    amy_send_at(time=950, synth=1, vel=0)


class TestBuses(AmyTest):
  """You can assign synths to different buses to get independent FX."""

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=4, patch=22, bus=0, pan=0.2)  # A37 Pizzicato
    amy_send_at(time=0, bus=0, reverb=1, echo=0)
    #amy_send_at(time=0, synth=2, num_voices=4, patch=22, bus=1, pan=0.8)
    amy_send_at(time=0, synth=2, num_voices=4, patch=22)
    amy_send_at(time=0, synth=2, bus=1, pan=0.8)
    amy_send_at(time=0, bus=1, reverb=0, echo='1,100,,0.5,0.5')
    # Mixdown for buses 0 and 1: volume is a scalar addressed at a bus.
    amy_send_at(time=0, bus=0, volume=2)
    amy_send_at(time=0, bus=1, volume=0.5)
    amy_send_at(time=100, synth=1, note=60, vel=5)
    amy_send_at(time=300, synth=2, note=63, vel=5)
    amy_send_at(time=500, synth=1, note=67, vel=5)
    amy_send_at(time=700, synth=2, note=70, vel=5)


class TestZeroFreqModPhase(AmyTest):
  """Test that we can set a constant value for mod_osc via its phase."""

  def run(self):
    # Make ext0 come from osc 1
    amy_send_at(time=0, osc=0, freq={'const': 440, 'mod': 1}, mod_source=1)
    amy_send_at(time=0, osc=1, freq=0)  # Starts in phase 0, so sin = 0
    amy_send_at(time=100, osc=0, vel=1)
    amy_send_at(time=200, osc=1, phase=0.25)  # sin(0.25 pi) = 1.0, octave jump
    amy_send_at(time=400, osc=0, vel=0)


class TestCVFromOsc(AmyTest):
  """Facility to simulate CV input from an osc, for testing."""

  def run(self):
    # Make ext0 come from osc 1
    amy.set_cv_from_osc(0, 1)
    amy_send_at(time=0, osc=0, freq={'const': 440, 'note': 1, 'ext0': 1})
    amy_send_at(time=0, osc=1, freq=4.0, amp=0.1)
    amy_send_at(time=100, osc=0, vel=1)
    amy_send_at(time=900, osc=0, vel=0)


class TestCVTrigger(AmyTest):
  """Test events triggered by CV transitions, using simulated CV-from-osc."""

  def run(self):
    # Setup the simulated CV input as 4 Hz sine
    amy.set_cv_from_osc(0, 1)
    amy_send_at(time=0, osc=1, freq=4)
    # Osc 0 gives a little tone.
    amy_send_at(time=0, osc=0, freq=440, eg0='0,1,200,0,200,0')
    # Tone is triggered when CV osc passes 0.5.
    amy.send(cv_trigger='0,0.5,0.1,1,0,1,v0l1')


class TestCVTriggerNote(AmyTest):
  """Test pitch input of CV-triggered events."""

  def run(self):
    # Setup the simulated CV input 0 as 4 Hz sine
    amy.set_cv_from_osc(0, 1)
    amy_send_at(time=0, osc=1, freq=4)
    # Setup the simulated CV input 1 (pitch) as slow ramp
    amy.set_cv_from_osc(1, 2)
    amy_send_at(time=0, osc=2, freq=1, wave=amy.SAW_UP)
    # Osc 0 gives a little tone.
    amy_send_at(time=0, osc=0, freq=440, eg0='0,1,200,0,200,0')
    # Tone is triggered when CV osc passes 0.5.
    amy.send(cv_trigger='0,0.5,0.1,1,0.5,0,v0l1n%v')


class TestCVTriggerNoteOff(AmyTest):
  """Test pitch input of CV-triggered events including note off."""

  def run(self):
    amy.send(reset=amy.RESET_TIMEBASE)
    # Setup the simulated CV input 0 as 4 Hz sine
    amy.set_cv_from_osc(0, 1)
    amy_send_at(time=0, osc=1, freq=4)
    # Osc 0 gives a gated tone.
    amy_send_at(time=0, osc=0, freq=440, eg1='0,1,1000,0,100,0')
    # Note on when CV osc passes 0.5 rising (reset lower than trigger).
    amy_send_at(time=0, cv_trigger='0,0.5,0.1,1,1,0,v0l1n%v')
    # Note off when CV osc passes 0.5 falling (reset higher than trigger).
    amy_send_at(time=0, cv_trigger='0,0.5,0.6,v0l0')
    # Also test that we can cancel the trigger events by clearing them
    # midway through last tone.  All triggers on CV0 are cancelled together.
    #amy_send_at(time=800, cv_trigger='0')
    # Oh, cv_trigger is executed on message parse, not deferred to a timed
    # event, so this cleared the events before they even started.


class TestAllVoiceOscsGetNoteOn(AmyTest):
  """#716 modifies behavior of sending note-on to a synth/voice to send it to all oscs."""

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=2, oscs_per_voice=3)
    amy_send_at(time=0, synth=1, osc=0, mod_source=1)  # Mark osc 1 as a mod, should NOT get note-ons
    amy_send_at(time=0, synth=1, osc=1, freq=10)
    amy_send_at(time=0, synth=1, osc=2, freq=660)  # Second sounding osc
    amy_send_at(time=100, synth=1, osc=0, note=84, vel=1)   # Trigger osc 0 only
    amy_send_at(time=200, synth=1, osc=0, note=84, vel=0)
    amy_send_at(time=300, synth=1, note=84, vel=1)   # Trigger both oscs
    amy_send_at(time=400, synth=1, note=84, vel=0)
    # Check mod_osc is working and not being shifted by note.  10 Hz should do one mod cycle in 0.1 s note.
    amy_send_at(time=450, synth=1, osc=0, freq={'mod': 0.2})
    amy_send_at(time=500, synth=1, osc=0, note=84, vel=1)   # Trigger osc 0 only
    amy_send_at(time=600, synth=1, osc=0, note=84, vel=0)


class TestNoteGoesToZero(AmyTest):
  """Per issue #720, we don't want to stop a note just because it's silent if it hasn't had a note-off."""

  def run(self):
    # 15 Hz AM modulator
    amy_send_at(time=0, osc=0, freq=15)
    # Osc 1 on left has AM that doesn't push it to zero
    amy_send_at(time=0, osc=1, eg0="50,1,300,0.5,500,0", mod_source=0, amp={'const': 0.5, 'mod': 0.5}, pan=0)
    # Osc 2 on the right has much deeper AM that pushes to zero in the troughs.  It will be killed in the first release trough.
    amy_send_at(time=0, osc=2, eg0="50,1,300,0.5,500,0", mod_source=0, amp={'const': 0.01, 'mod': 1.0}, pan=1)
    amy_send_at(time=100, osc=1, note=60, vel=1)
    amy_send_at(time=100, osc=2, note=60, vel=1)
    # Start release at 600 ms.
    amy_send_at(time=600, osc=1, note=60, vel=0)
    amy_send_at(time=600, osc=2, note=60, vel=0)


class TestSynthGlobalFX(AmyTest):
  """Issue #755: synth-directed FX commands were being ignored.  Result should match TestJunoPatch"""

  def run(self):
    # Also test the synth mechanism.
    amy_send_at(time=0, synth=1, num_voices=4, patch=20)
    amy_send_at(time=0, chorus=0)   # Turn off the chorus
    amy_send_at(time=0, synth=1, chorus=1)  #  Turn it back on with synth-directed command.
    amy_send_at(time=50, synth=1, note=48, vel=1)
    amy_send_at(time=150, synth=1, note=60, vel=1)
    amy_send_at(time=250, synth=1, note=63, vel=1)
    amy_send_at(time=350, synth=1, note=67, vel=1)
    amy_send_at(time=600, synth=1, note=48, vel=0)
    amy_send_at(time=700, synth=1, note=60, vel=0)
    amy_send_at(time=800, synth=1, note=63, vel=0)
    amy_send_at(time=900, synth=1, note=67, vel=0)


class TestSynthBusCmds(AmyTest):
  """Test that FX commands to nondefault bus via synth is right.   Should match TestBuses. """

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=4, patch=22, bus=0, pan=0.2)  # A37 Pizzicato
    amy_send_at(time=0, bus=0, reverb=1, echo=0)
    amy_send_at(time=0, synth=2, num_voices=4, patch=22, bus=1, pan=0.8)
    amy_send_at(time=0, synth=2, reverb=0, echo='1,100,,0.5,0.5')  # Bus implied by synth.
    # Mixdown for buses 0 and 1: volume is a scalar addressed at a bus.
    amy_send_at(time=0, bus=0, volume=2)
    amy_send_at(time=0, bus=1, volume=0.5)
    amy_send_at(time=100, synth=1, note=60, vel=5)
    amy_send_at(time=300, synth=2, note=63, vel=5)
    amy_send_at(time=500, synth=1, note=67, vel=5)
    amy_send_at(time=700, synth=2, note=70, vel=5)


class TestOscAndBusCommands(AmyTest):
  """Bus-directed events are special; check they are handled right."""

  def run(self):
    # Setup bus 1 with echo so we can tell when it's used
    amy_send_at(time=0, synth=1, num_voices=2, oscs_per_voice=1)
    amy_send_at(time=10, synth=1, osc=0, wave=amy.SAW_UP, eg0='0,1,200,0,200,0')
    amy_send_at(time=20, synth=2, num_voices=2, oscs_per_voice=1, bus=1, echo='1,50,,0.5,0.5', pan=1)
    #amy_send_at(time=20, synth=2, num_voices=2, oscs_per_voice=1)
    #amy_send_at(time=20, synth=2, bus=1)
    #amy_send_at(time=20, synth=2, echo='1,50,,0.5,0.5')
    #amy_send_at(time=20, synth=2, pan=1)
    ##
    amy_send_at(time=30, synth=2, osc=0, wave=amy.SAW_DOWN, eg0='0,1,200,0,200,0')
    #
    amy_send_at(time=100, synth=1, note=60, vel=1)  # pan 0.5, no FX
    amy_send_at(time=200, synth=2, note=66, vel=1)  # pan 1, 50ms echo
    # Change properties via synth, also with osc-directed pan
    amy_send_at(time=250, synth=1, eq='10,-20,10', pan=0)
    #amy_send_at(time=250, synth=1, eq='10,-20,10')
    #amy_send_at(time=250, synth=1, pan=0)
    ##
    amy_send_at(time=300, synth=1, note=60, vel=1)   # pan 0, mid cut
    amy_send_at(time=400, synth=2, note=66, vel=1)   # pan 1, 50ms echo (as before)
    # Change the synth's bus while altering the bus
    amy_send_at(time=550, synth=1, bus=1, echo='1,90,,0.5,0.5')
    #amy_send_at(time=550, synth=1, bus=1)
    #amy_send_at(time=550, bus=1, echo='1,90,,0.5,0.5')
    ##
    amy_send_at(time=650, synth=2, bus=0)
    ##
    amy_send_at(time=700, synth=1, note=72, vel=1)   # pan 0, flat EQ, 90ms echo
    amy_send_at(time=800, synth=2, note=78, vel=1)   # pan 1, mid cut, no echo
    # Modify bus without referencing synth
    amy_send_at(time=850, bus=1, echo='1,20,,0.5,0.5')
    amy_send_at(time=900, synth=1, note=72, vel=1)   # pan 0, flat EQ, 20ms echo


class TestGrabMidiNotes(AmyTest):
  """Test that grab_midi_notes=False works.  This test should not include the drums."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    # Disable MIDI on ch10
    amy_send_at(time=0, synth=10, grab_midi_notes=False);
    # amy_inject_midi_at args are (time, midi_status_byte, midi_note, midi_vel)
    amy_inject_midi_at(100, 0x90, 48, 100)  # low note
    amy_inject_midi_at(400, 0x99, 35, 100)  # bass drum (should not sound)
    amy_inject_midi_at(500, 0x80, 48, 0)  # low note off
    amy_inject_midi_at(700, 0x99, 37, 100)  # snare (should not sound)
    amy_inject_midi_at(900, 0x89, 37, 100)  # snare note off


class TestMidiNoteCmd(AmyTest):
  """midi_note_cmd sets a particular command for a particular midi note."""

  def run(self):
    amy_send_at(time=0, synth=1, num_voices=4, patch=0, synth_flags=1)
    amy_send_at(time=0, synth=10, patch=258)  # MIDI drums
    # midi_note_cmd = <midi note>,log,min,max,offset,wire_cmd
    amy_send_at(time=0, synth=1, midi_note_cmd='64,0,0,1,0,' + amy.message(synth=10, note=56, vel='%v'))
    # Synth 2 is not defined but we can still set up midi_note_cmds for it.  Note=-1 means all notes (%n)
    amy_send_at(time=0, synth=2, midi_note_cmd='-1,0,0,1,0,' + amy.message(synth=1, note='%n', vel='%v'))
    # Note 63 should play as normal, note 64 should cause cowbell.
    send_midi(time=100, synth=1, note=63, vel=1)
    send_midi(time=200, synth=1, note=64, vel=1)
    send_midi(time=300, synth=1, note=63, vel=0)
    send_midi(time=400, synth=1, note=64, vel=0)
    # Notes to synth 2 should play on synth 1, including further forwarding to synth 10 (as above).
    send_midi(time=500, synth=2, note=63, vel=1)
    send_midi(time=600, synth=2, note=64, vel=1)
    send_midi(time=700, synth=2, note=63, vel=0)
    send_midi(time=800, synth=2, note=64, vel=0)


class TestTranceGlitch(AmyTest):
  """Investigating a glitch that occurs in 'Emo Trance Backbeat' on AMYboard World."""

  def run(self):
    amy_send_at(time=0, volume=0.1)
    amy_send_at(time=0, synth=2, patch=55, num_voices=6)
    #for n in [40, 43, 47, 52, 55]:
    for n in [52]:
      amy_send_at(time=100, synth=2, note=n, vel=2.75)


class TestPatch32Glitch(AmyTest):
  """One of the excessive glitch voices reported."""

  def run(self):
    amy_send_at(time=0, synth=2, patch=32, num_voices=4)
    amy_send_at(time=100, synth=2, note=71, vel=1)
    amy_send_at(time=200, synth=2, note=71, vel=0)
    amy_send_at(time=300, synth=2, note=71, vel=1)
    amy_send_at(time=400, synth=2, note=71, vel=0)
    amy_send_at(time=500, synth=2, note=71, vel=1)
    amy_send_at(time=600, synth=2, note=71, vel=0)
    amy_send_at(time=700, synth=2, note=71, vel=1)
    amy_send_at(time=800, synth=2, note=71, vel=0)


class TestSequencer(AmyTest):
  """Sequenced notes on a default synth, fired periodically."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    amy_send_at(time=100, ticks='20,24,0', synth=1, note=64, vel=1)


class TestSequencedSynthDrums(AmyTest):
  """Test that sequencer works even for notes being interpreted as NOTES_VIA_MIDI (it didn't at first)."""

  def __init__(self):
    super().__init__()
    self.default_synths = True

  def run(self):
    # The sequencer working on the SYNTH_FLAGS_NOTES_VIA_MIDI synth 10 (38 = Acoustic Snare).
    amy_send_at(time=100, ticks='20,24,0', synth=10, note=38, vel=1)


class TestSequencerOsc(AmyTest):
  """Sequenced osc events fire on AMY (sample) time, even in offline rendering.

  At the default 108 bpm and 48 PPQ, one sequencer tick is ~11.6 ms, so one
  second of rendering covers 86 ticks."""

  def run(self):
    amy_send_at(time=0, osc=0, wave=amy.SINE, freq=1000)
    # Absolute-tick events: note on at tick 20 (~231 ms), off at tick 40 (~463 ms).
    amy.send(osc=0, vel=1, ticks="20,0,1")
    amy.send(osc=0, vel=0, ticks="40,0,2")
    # Periodic event: a lower note every 60 ticks, lands once at ~694 ms.
    amy.send(osc=1, wave=amy.SINE, freq=500, vel=1, ticks="0,60,3")
    amy_send_at(time=900, osc=1, vel=0)


class TestDumpState(AmyTest):
  """Exercise amy.dump_state() against a golden."""

  def test(self):
    _amy.stop()
    _amy.start(0)
    amy_send_at(time=0, synth=1, num_voices=4, oscs_per_voice=2)
    amy_send_at(time=0, synth=1, osc=0, wave=amy.SINE, freq=110, chained_osc=1)
    amy_send_at(time=0, synth=1, osc=1, wave=amy.SAW_UP, freq=880)
    amy_send_at(time=0, synth=2, num_voices=1, oscs_per_voice=1, bus=1, volume=0.5)
    amy.send_raw('i1ic10,1,1,100,1,i%id%v')
    amy.send_raw('i1io39,0,0,1,0,i%in40l%v')
    amy.render(1)  # Let the events execute.
    commands = amy.dump_state()
    expected = """i1ic255Z
i1iv4in2Z
i1v0f110c1Z
i1v1w3f880Z
i1ic10,1,1,100,1,i%id%vZ
i1io39,0,0,1,0,i%in40l%vZ
i2ic255Z
i2iv1in1y1Z
i2v0Z
y0V1x0,0,0M0,500,,0,0k0,320,0.5,0.5h0,0.85,0.5,3000Z
y1V0.5x0,0,0M0,500,,0,0k0,320,0.5,0.5h0,0.85,0.5,3000Z
"""
    if commands != expected:
      is_ok = False
      message = self.__class__.__name__ + ': get_synth_commands mismatch: expected:\n++\n%s\n--\n;saw:\n++\n%s\n--;' % (expected, commands)
    else:
      is_ok = True
      message = self.__class__.__name__ + ' : ok'
    return is_ok, message


class TestLoadSyx(AmyTest):
  """fm.load_syx: DX7 bank sysex -> user patch, checked against the direct conversion path."""

  def test(self):
    from . import fm
    _amy.stop()
    _amy.start(0)
    voice_num = 213  # 'TUB BELLS ', not one of the 128 built-ins
    dx7_patch = fm.DX7Patch.from_patch_number(voice_num)
    # Round-trip through a real bank sysex (pack, header, checksum).
    syx = fm.make_bank_syx([dx7_patch.get_bytestream()])
    # Capture the stored-patch wire message so we can compare it.
    captured = []
    saved_override = amy.override_send
    amy.override_send = captured.append
    name = fm.load_syx(syx, voice=0, patch=1024)
    amy.override_send = saved_override
    # The stored patch must match what the direct (non-sysex) path produces.
    amy.log_patch()
    fm.AMYPatch.from_dx7(dx7_patch).send_to_AMY(reset=False)
    expected = 'u1024' + amy.retrieve_patch()
    problems = []
    if name != 'TUB BELLS ':
      problems.append('bad name %r' % name)
    if len(captured) != 1:
      problems.append('expected 1 wire message, got %d' % len(captured))
    elif captured[0] != expected:
      problems.append('stored %r != direct %r' % (captured[0][:60], expected[:60]))
    else:
      amy.send_raw(captured[0])  # actually store it this time
    # And it must make sound.
    amy.send(synth=1, num_voices=1, patch=1024)
    amy.send(synth=1, note=64, vel=1)
    _reset_test_clock()
    samples = _finish_test_clock(1.0)
    level = dB(rms(samples))
    if level < -60:
      problems.append('rendered level %.1f dB, expected audible' % level)
    if problems:
      return False, self.__class__.__name__ + ': ' + '; '.join(problems)
    return True, self.__class__.__name__ + ' : ok (%.1f dB)' % level


def main(argv):
  if len(argv) > 1 and argv[1] == 'quiet':
    quiet = True
    del argv[1]
  else:
    quiet = False

  if len(argv) > 1:
    # Override location of reference files.
    AmyTest.ref_dir = argv[1]

  do_all_tests = True

  oks = []
  errors = []

  if do_all_tests:
    for testClass in AmyTest.__subclasses__():
      test_object = testClass()
      is_ok, message = test_object.test()
      if not quiet:
        print(message)
      if is_ok:
        oks.append(message)
      else:
        errors.append(message)
  else:
    #TestPcmShift().test()
    #TestChorus().test()
    #TestBleep().test()
    #TestBrass().test()
    #TestBrass2().test()
    #print(TestSineEnv().test()[1])
    #TestSawDownOsc().test()
    print(TestGuitar().test()[1])
    #TestFilter().test()
    #print(TestAlgo().test()[1])
    #TestBleep().test()
    #TestChainedOsc().test()
    #TestJunoPatch().test()
    #TestJunoTrumpetPatch().test()
    #TestPcmLoop().test()
    #TestBYOPNoteOff().test()
    #TestInterpPartials().test()
    #TestVoiceStealing().test()
    #TestSustainPedal().test()
    #TestPatchFromEvents().test()
    #TestVoiceStealDecay().test()
    #TestRestartFileSample().test()
    #TestDiskSample().test()
    #print(TestFileTransfer().test()[1])
    #print(TestVoiceStealClick().test()[1])
    #print(TestOwBassClick().test()[1])
    #print(TestXanaduFM().test()[1])
    #print(TestInterpPartials().test()[1])

  if not quiet:
    amy.send(debug=0)

  # Sort errors by severity
  errors = sorted(errors,
                  key=lambda x: float_or_val(x.split(' ')[-2].split('=')[-1], error_val=1000),
                  reverse=True)
  # Write out failed_tests.txt even if it's empty.
  with open("failed_tests.txt", "wt") as f:
    for e in errors:
      f.write(e.split(' ')[0] + '\n')

  if errors:
    print(len(oks), "tests pass,", len(errors), "tests failed:")
    print('\n'.join(errors))
    if not quiet:
      sys.exit(1)
  else:
    print(len(oks), "tests pass")


if __name__ == "__main__":
  main(sys.argv)

