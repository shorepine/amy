# juno.py
# Convert juno-106 sysex patches to Amy

import amy
import javaobj
import numpy as np
import time

from dataclasses import dataclass, field
from typing import List, Dict, Any


  # Range is from 10 ms to 12 sec i.e. 1200.
  # (12 sec is allegedly the max decay time of the EG, see
  # page 32 of Juno-106 owner's manual,
  # https://cdn.roland.com/assets/media/pdf/JUNO-106_OM.pdf .)
  # Return int value in ms
  #time = 0.01 * np.exp(np.log(1e3) * midi / 127.0)
  # midi 30 is ~ 200 ms, 50 is ~ 1 sec, so
  #        D=30 
  # 
  # from demo at https://www.synthmania.com/Roland%20Juno-106/Audio/Juno-106%20Factory%20Preset%20Group%20A/14%20Flutes.mp3
  # A11 Brass set        A=3  D=49  S=45 R=32 -> 
  # A14 Flute            A=23 D=81  S=0  R=18 -> A=200ms, R=200ms, D=0.22 to 0.11 in 0.830 s / 0.28 to 0.14 in 0.92     R = 0.2 to 0.1 in 0.046
  # A16 Brass & Strings  A=44 D=66  S=53 R=44 -> A=355ms, R=        
  # A15 Moving Strings   A=13 D=87       R=35 -> A=100ms, R=600ms,
  # A26 Celeste          A=0  D=44  S=0  R=81 -> A=2ms             D=0.48 to 0.24 in 0.340 s                           R = 0.9 to 0.5 in 0.1s; R clearly faster than D
  # A27 Elect Piano      A=1  D=85  S=43 R=40 -> A=14ms,  R=300ms
  # A28 Elect. Piano II  A=0  D=68  S=0  R=22 ->                   D=0.30 to 0.15 in 0.590 s  R same as D?
  # A32 Steel Drums      A=0  D=26  S=0  R=37 ->                   D=0.54 to 0.27 in 0.073 s
  # A34 Brass III        A=58 D=100 S=94 R=37 -> A=440ms, R=1000ms
  # A35 Fanfare          A=72 D=104 S=75 R=49 -> A=600ms, R=1200ms 
  # A37 Pizzicato        A=0  D=11  S=0  R=12 -> A=6ms,   R=86ms   D=0.66 to 0.33 in 0.013 s
  # A41 Bass Clarinet    A=11 D=75  S=0  R=25 -> A=92ms,  R=340ms, D=0.20 to 0.10 in 0.820 s /                            R = 0.9 to 0.45 in 0.070
  # A42 English Horn     A=8  D=81  S=21 R=16 -> A=68ms,  R=240ms,
  # A45 Koto             A=0  D=56  S=0  R=39 ->                   D=0.20 to 0.10 in 0.160 s
  # A46 Dark Pluck       A=0  D=52  S=15 R=63 ->
  # A48 Synth Bass I     A=0  D=34  S=0  R=36 ->                   D=0.60 to 0.30 in 0.096 s
  # A56 Funky III        A=0  D=24  S=0  R=2                       D 1/2 in 0.206
  # A61 Piano II         A=0  D=98  S=0  R=32                      D 1/2 in 1.200
  # A  0   1   8   11   13   23   44  58
  # ms 6   14  68  92   100  200  355 440
  # D            11  24  26  34  44  56  68  75  81  98
  # 1/2 time ms  13  206 73  96  340 160 590 830 920 1200
  # R  12  16  18  25   35   37   40
  # ms 86  240 200 340  600  1000 300

# Notes from video https://www.youtube.com/watch?v=zWOs16ccB3M
# A 0 -> 1ms  1.9 -> 30ms  3.1 -> 57ms  3.7 -> 68ms  5.4  -> 244 ms  6.0 -> 323ms  6.3 -> 462ms  6.5 -> 502ms
# D 3.3 -> 750ms

# Addendum: See online emulation
# https://github.com/pendragon-andyh/junox
# based on set of isolated samples
# https://github.com/pendragon-andyh/Juno60

  
def to_attack_time(val):
  """Convert a midi value (0..127) to a time for ADSR."""
  # From regression of sound examples
  return 6 + 8 * val * 127
  # from Arturia video
  #return 12 * np.exp2(0.066 * midi) - 12

def to_decay_time(val):
  """Convert a midi value (0..127) to a time for ADSR."""
  # time = 12 * np.exp(np.log(120) * midi/100)
  # time is time to decay to 1/2; Amy envelope times are to decay to exp(-3) = 0.05
  # return np.log(0.05) / np.log(0.5) * time
  # from Arturia video
  return 80*np.exp2(0.066 * val * 127) - 80
  

def to_release_time(val):
  """Convert a midi value (0..127) to a time for ADSR."""
  #time = 100 * np.exp(np.log(16) * midi/100)
  #return np.log(0.05) / np.log(0.5) * time
  # from Arturia video
  return 70*np.exp2(0.066 * val * 127) - 70


def to_level(val):
  # Map midi to 0..1, linearly.
  return val


def level_to_amp(level):
  # level is 0.0 to 1.0; amp is 0.001 to 1.0
  if level == 0.0:
    return 0.0
  return float("%.3f" % (0.001 * np.exp(level * np.log(1000.0))))


def to_lfo_freq(val):
  # LFO frequency in Hz varies from 0.5 to 30
  # from Arturia video
  return float("%.3f" % (0.6 * np.exp2(0.042 * val * 127) - 0.1))


def to_lfo_delay(val):
  """Convert a midi value (0..127) to a time for lfo_delay."""
  #time = 100 * np.exp(np.log(16) * midi/100)
  #return float("%.3f" % (np.log(0.05) / np.log(0.5) * time))
  # from Arturia video
  return float("%.3f" % (18 * np.exp2(0.066 * val * 127) - 13))


def to_resonance(val):
  # Q goes from 0.5 to 16 exponentially
  return float("%.3f" % (0.5 * np.exp2(5.0 * val)))


def to_filter_freq(val):
  # filter_freq goes from ? 100 to 6400 Hz with 18 steps/octave
  #return float("%.3f" % (100 * np.exp(np.log(2) * midi / 20.0)))
  # from Arturia video
  return float("%.3f" % (6.5 * np.exp2(0.11 * val * 127)))


def ffmt(val):
  """Format float values as max 3 dp, but less if possible."""
  return "%.5g" % float("%.3f" % val)


@dataclass
class JunoPatch:
  """Encapsulates information in a Juno Patch."""
  name: str = ""
  lfo_rate: float = 0
  lfo_delay_time: float = 0
  dco_lfo: float = 0
  dco_pwm: float = 0
  dco_noise: float = 0
  vcf_freq: float = 0
  vcf_res: float = 0
  vcf_env: float = 0
  vcf_lfo: float = 0
  vcf_kbd: float = 0
  vca_level: float = 0
  env_a: float = 0
  env_d: float = 0
  env_s: float = 0
  env_r: float = 0
  dco_sub: float = 0
  stop_16: bool = False
  stop_8: bool = False
  stop_4: bool = False
  pulse: bool = False
  triangle: bool = False
  chorus: int = 0
  pwm_manual: bool = False  # else lfo
  vca_gate: bool = False  # else env
  vcf_neg: bool = False  # else pos
  hpf: int = 0
  # Functions to be called after setting params.
  post_set_fn: dict = field(default_factory=dict)
  dispatch_fns: list = field(default_factory=list)
  
  # These lists name the fields in the order they appear in the sysex.
  FIELDS = ['lfo_rate', 'lfo_delay_time', 'dco_lfo', 'dco_pwm', 'dco_noise',
           'vcf_freq', 'vcf_res', 'vcf_env', 'vcf_lfo', 'vcf_kbd', 'vca_level',
           'env_a', 'env_d', 'env_s', 'env_r', 'dco_sub']
  # After the 16 integer values, there are two bytes of bits.
  BITS1 = ['stop_16', 'stop_8', 'stop_4', 'pulse', 'triangle']
  BITS2 = ['pwm_manual', 'vcf_neg', 'vca_gate']

  @staticmethod
  def from_patch_number(patch_number):
    pobj = javaobj.load(open('juno106_factory_patches.ser', 'rb'))
    patch = pobj.v.elementData[patch_number]
    return JunoPatch.from_sysex(bytes(patch.sysex), name=patch.name)

  @classmethod
  def from_sysex(cls, sysexbytes, name=None):
    """Decode sysex bytestream into JunoPatch fields."""
    assert len(sysexbytes) == 18
    result = JunoPatch(name=name)
    # The first 16 bytes are sliders.
    for index, field in enumerate(cls.FIELDS):
      setattr(result, field, int(sysexbytes[index])/127.0)
    # Then there are two bytes of switches.
    for index, field in enumerate(cls.BITS1):
      setattr(result, field, (int(sysexbytes[16]) & (1 << index)) > 0)
    # Chorus has a weird mapping.  Bit 5 is ~Chorus, bit 6 is ChorusI-notII
    setattr(result, 'chorus', [2, 0, 1, 0][int(sysexbytes[16]) >> 5])
    for index, field in enumerate(cls.BITS2):
      setattr(result, field, (int(sysexbytes[17]) & (1 << index)) > 0)
    # Bits 3 & 4 also have flipped endianness & sense.
    setattr(result, 'hpf', [3, 2, 1, 0][int(sysexbytes[17]) >> 3])
    return result

  def _breakpoint_string(self, peak_val):
    """Format a breakpoint string from the ADSR parameters reaching a peak."""
    return "%d,%s,%d,%s,%d,0" % (
      to_attack_time(self.env_a), ffmt(peak_val), to_attack_time(self.env_a) + to_decay_time(self.env_d),
      ffmt(peak_val * to_level(self.env_s)), to_release_time(self.env_r)
    )

  def send_to_AMY(self, base_osc=0):
    """Output AMY commands to set up the patch.
    Send amy.send(osc=<base_osc + 1>, note=50, vel=1) afterwards."""
    amy.reset()
    # base_osc is pulse/PWM
    # base_osc + 1 is SAW
    # base_osc + 2 is SUBOCTAVE
    # base_osc + 3 is NOISE
    # base_osc + 4 is LFO
    #   env0 is VCA
    #   env1 is VCF

    lfo_osc = base_osc + 4
    next_osc = base_osc
    # Only one of stop_{16,8,4} should be set.
    base_freq = 261.63  # The mid note
    if self.stop_16:
      base_freq /= 2
    elif self.stop_4:
      base_freq *= 2
    osc_args = {
      'amp': '0,0,%s,1,0,0' % ffmt(to_level(self.vca_level)),
      'freq': '%s,1,0,0,0,%s' % (ffmt(base_freq), ffmt(0.03 * to_level(self.dco_lfo))),
      'filter_type': amy.FILTER_LPF24,
      'resonance': to_resonance(self.vcf_res),
      'mod_source': lfo_osc,
    }
    if not self.vca_gate:
      osc_args['bp0'] = self._breakpoint_string(1.0)
    vcf_env_polarity = -1.0 if self.vcf_neg else 1.0
    osc_args['filter_freq'] = '%s,%s,0,0,%s,%s' % (
      ffmt(to_filter_freq(self.vcf_freq)),
      ffmt(to_level(self.vcf_kbd)),
      ffmt(20 * vcf_env_polarity * to_level(self.vcf_env)),
      ffmt(5 * to_level(self.vcf_lfo))
    )
    osc_args['bp1'] = self._breakpoint_string(1.0)

    lfo_args = {'osc': lfo_osc, 'wave': amy.TRIANGLE, 'freq': to_lfo_freq(self.lfo_rate),
                'amp': '1,0,0,1,0,0',
                'bp0': '%i,1.0,%i,1.0,10000,0' % (to_lfo_delay(self.lfo_delay_time), to_lfo_delay(self.lfo_delay_time))}
    print('about to send lfo:', lfo_args)
    amy.send(**lfo_args)

    # PWM square wave.
    pulse_args = {}
    if self.pulse:
      const_duty = 0
      lfo_duty = to_level(self.dco_pwm)
      if self.pwm_manual:
        # Swap duty parameters.
        const_duty, lfo_duty = lfo_duty, const_duty
      pulse_args = {
        'osc': next_osc,
        'wave': amy.PULSE,
        'duty': '%s,0,0,0,0,%s' % (ffmt(0.5 + 0.5 * const_duty), ffmt(0.5 * lfo_duty)),
      }
      next_osc += 1
      pulse_args |= osc_args
      if self.triangle or self.dco_sub or self.dco_noise:
        pulse_args['chained_osc'] = next_osc
      print('about to send pulse:', pulse_args)
      amy.send(**pulse_args)

    # Triangle wave.
    tri_args = {}
    if self.triangle:
      tri_args = {
        'osc': next_osc,
        'wave': amy.SAW_UP,
      }
      next_osc += 1
      tri_args |= osc_args
      if self.dco_sub or self.dco_noise:
        tri_args['chained_osc'] = next_osc
      print('about to send tri:', tri_args)
      amy.send(**tri_args)

    # sub wave.
    sub_args = {}
    if self.dco_sub:
      sub_args = {
        'osc': next_osc,
        'wave': amy.PULSE,
      }
      next_osc += 1
      sub_args |= osc_args
      # Overwrite freq.
      sub_args['freq'] = '%s,1,0,0,0,%s' % (ffmt(base_freq / 2.0), ffmt(to_level(self.dco_lfo)))
      # Overwrite amp.
      sub_args['amp'] = '%s,0,%s,1,0,0' % (ffmt(to_level(self.dco_sub)), ffmt(to_level(self.vca_level)))
      if self.dco_noise:
        sub_args['chained_osc'] = next_osc
      print('about to send sub:', sub_args)
      amy.send(**sub_args)

    # noise.
    noise_args = {}
    if self.dco_noise:
      noise_args = {
        'osc': next_osc,
        'wave': amy.NOISE,
      }
      next_osc += 1
      noise_args |= osc_args
      # Overwrite amp.
      noise_args['amp'] = '%s,0,%s,1,0,0' % (ffmt(to_level(self.dco_noise)), ffmt(to_level(self.vca_level)))
      next_osc += 1
      # Nothing more to chain
      print('about to send noise:', noise_args)
      amy.send(**noise_args)

    # Chorus & HPF
    gen_args = {}
    eq_l = eq_m = eq_h = 0
    if self.hpf == 0:
      eq_l = 10
    elif self.hpf == 1:
      pass
    elif self.hpf == 2:
      eq_l = -8
    elif self.hpf == 3:
      eq_l = -15
      eq_m = 8
      eq_h = 8
    gen_args = {'eq_l': eq_l, 'eq_m': eq_m, 'eq_h': eq_h}
    if self.chorus == 0:
      gen_args['chorus_level'] = 0
    else:
      gen_args['chorus_level'] = 1
      gen_args['osc'] = amy.CHORUS_MOD_SOURCE
      gen_args['amp'] = 0.5
      if self.chorus == 1:
        gen_args['freq'] = 0.5
      elif self.chorus == 2:
        gen_args['freq'] = 0.83
      elif self.chorus == 3:
        gen_args['freq'] = 0.83
        gen_args['amp'] = 0.05
    print('about to sent gen:', gen_args)
    amy.send(**gen_args)

    # Report what we sent.
    print(lfo_args, pulse_args, tri_args, sub_args, noise_args, gen_args)

  # Setters for each Juno UI control
  def set_param(self, param, val):
    set_attr(self, param,  val)
    if self.post_set_fn[param]:
      self.post_set_fn[param](param, val)
    for fn in self.dispatch_fns:
      fn()
    self.dispatch_fns = []
