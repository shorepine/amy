# juno.py
# Convert juno-106 sysex patches to Amy


import amy
import json
import math
import time

try:
  math.exp2(1)
  def exp2(x):
    return math.exp2(x)
except AttributeError:
  def exp2(x):
    return math.pow(2.0, x)

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
  #return 12 * exp2(0.066 * midi) - 12


def to_decay_time(val):
  """Convert a midi value (0..127) to a time for ADSR."""
  # time = 12 * np.exp(np.log(120) * midi/100)
  # time is time to decay to 1/2; Amy envelope times are to decay to exp(-3) = 0.05
  # return np.log(0.05) / np.log(0.5) * time
  # from Arturia video
  #return 80 * exp2(0.066 * val * 127) - 80
  return 80 * exp2(0.085 * val * 127) - 80


def to_release_time(val):
  """Convert a midi value (0..127) to a time for ADSR."""
  #time = 100 * np.exp(np.log(16) * midi/100)
  #return np.log(0.05) / np.log(0.5) * time
  # from Arturia video
  #return 70 * exp2(0.066 * val * 127) - 70
  return 70 * exp2(0.066 * val * 127) - 70


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
  return float("%.3f" % (0.6 * exp2(0.04 * val * 127) - 0.1))


def to_lfo_delay(val):
  """Convert a midi value (0..127) to a time for lfo_delay."""
  #time = 100 * np.exp(np.log(16) * midi/100)
  #return float("%.3f" % (np.log(0.05) / np.log(0.5) * time))
  # from Arturia video
  return float("%.3f" % (18 * exp2(0.066 * val * 127) - 13))


def to_resonance(val):
  # Q goes from 0.5 to 16 exponentially
  return float("%.3f" % (0.7 * exp2(4.0 * val)))


def to_filter_freq(val):
  # filter_freq goes from ? 100 to 6400 Hz with 18 steps/octave
  #return float("%.3f" % (100 * np.exp2(midi / 20.0)))
  # from Arturia video
  #return float("%.3f" % (6.5 * exp2(0.11 * val * 127)))
  #return float("%.3f" % (25 * exp2(0.055 * val * 127)))
  #return float("%.3f" % (25 * exp2(0.083 * val * 127)))
  return float("%.3f" % (13 * exp2(0.0938 * val * 127)))


def ffmt(val):
  """Format float values as max 3 dp, but less if possible."""
  return "%.5g" % float("%.3f" % val)


_PATCHES = [["A11 Brass Set 1", [20, 49, 0, 102, 0, 35, 13, 58, 0, 86, 108, 3, 49, 45, 32, 0, 81, 17]], ["A12 Brass Swell", [6, 48, 0, 56, 0, 43, 17, 26, 0, 84, 75, 64, 118, 38, 37, 70, 82, 25]], ["A13 Trumpet", [52, 45, 8, 102, 0, 55, 34, 24, 1, 59, 127, 5, 66, 48, 16, 0, 50, 9]], ["A14 Flutes", [60, 43, 1, 0, 0, 55, 32, 10, 11, 41, 127, 23, 81, 0, 18, 0, 50, 1]], ["A15 Moving Strings", [63, 0, 0, 39, 0, 77, 20, 4, 0, 111, 34, 13, 87, 88, 35, 14, 26, 16]], ["A16 Brass & Strings", [35, 0, 0, 56, 0, 76, 17, 4, 0, 41, 78, 44, 66, 53, 44, 23, 73, 24]], ["A17 Choir", [59, 14, 13, 25, 0, 59, 94, 2, 0, 62, 127, 68, 11, 127, 48, 0, 74, 9]], ["A18 Piano I", [20, 49, 0, 80, 0, 65, 12, 10, 0, 27, 103, 0, 66, 0, 30, 86, 42, 17]], ["A21 Organ I", [54, 15, 0, 53, 0, 43, 76, 14, 1, 127, 100, 0, 10, 82, 0, 23, 41, 28]], ["A22 Organ II", [44, 15, 0, 53, 0, 53, 76, 14, 1, 85, 74, 0, 10, 82, 0, 58, 74, 28]], ["A23 Combo Organ", [75, 21, 9, 57, 0, 63, 70, 4, 0, 109, 96, 0, 48, 43, 46, 61, 44, 13]], ["A24 Calliope", [82, 40, 11, 0, 0, 87, 27, 17, 0, 56, 89, 7, 127, 127, 6, 48, 74, 11]], ["A25 Donald Pluck", [76, 21, 9, 57, 0, 73, 105, 15, 0, 78, 82, 2, 5, 43, 10, 127, 44, 7]], ["A26 Celeste* (1 oct.up)", [28, 0, 0, 0, 0, 33, 24, 54, 0, 38, 96, 0, 44, 0, 81, 15, 44, 25]], ["A27 Elect. Piano I", [59, 0, 0, 0, 0, 16, 31, 61, 6, 35, 127, 1, 85, 43, 40, 0, 41, 1]], ["A28 Elect. Piano II", [0, 0, 0, 71, 0, 50, 69, 7, 0, 80, 102, 0, 68, 0, 22, 0, 73, 17]], ["A31 Clock Chimes* (1 oct. up)", [59, 0, 0, 0, 22, 44, 127, 0, 0, 127, 104, 0, 48, 0, 51, 127, 68, 27]], ["A32 Steel Drums", [33, 53, 0, 32, 9, 71, 46, 26, 0, 127, 127, 0, 26, 0, 37, 33, 74, 27]], ["A33 Xylophone", [0, 0, 0, 0, 0, 29, 24, 54, 0, 51, 127, 0, 29, 29, 38, 15, 44, 25]], ["A34 Brass III", [52, 20, 0, 35, 0, 66, 24, 11, 0, 12, 127, 58, 100, 94, 37, 22, 82, 25]], ["A35 Fanfare", [47, 0, 0, 70, 0, 44, 0, 32, 0, 67, 33, 72, 104, 75, 49, 50, 89, 24]], ["A36 String III", [48, 27, 0, 102, 0, 71, 14, 0, 0, 84, 73, 63, 31, 127, 45, 0, 26, 16]], ["A37 Pizzicato", [60, 18, 0, 102, 0, 66, 2, 5, 0, 42, 127, 0, 11, 0, 12, 0, 90, 0]], ["A38 High Strings", [58, 14, 0, 102, 0, 84, 8, 2, 0, 71, 77, 18, 44, 127, 40, 0, 12, 8]], ["A41 Bass clarinet", [52, 45, 8, 0, 0, 48, 36, 25, 8, 58, 104, 11, 75, 0, 25, 0, 41, 17]], ["A42 English Horn", [47, 45, 9, 102, 0, 70, 48, 7, 0, 27, 127, 8, 81, 26, 16, 0, 42, 1]], ["A43 Brass Ensemble", [52, 45, 0, 102, 0, 46, 44, 29, 1, 59, 103, 16, 103, 97, 34, 31, 82, 17]], ["A44 Guitar", [86, 58, 0, 65, 0, 12, 15, 71, 0, 30, 96, 0, 52, 54, 31, 0, 57, 17]], ["A45 Koto", [90, 28, 0, 94, 0, 40, 58, 29, 0, 75, 127, 0, 56, 0, 39, 2, 74, 1]], ["A46 Dark Pluck", [31, 0, 0, 87, 0, 38, 61, 27, 0, 71, 90, 0, 52, 15, 63, 97, 10, 25]], ["A47 Funky I", [4, 0, 0, 73, 0, 31, 23, 51, 0, 41, 71, 0, 30, 36, 2, 64, 89, 29]], ["A48 Synth Bass I (unison)", [58, 16, 1, 57, 50, 32, 0, 66, 0, 35, 75, 0, 34, 0, 36, 102, 41, 25]], ["A51 Lead I", [72, 88, 18, 0, 0, 40, 97, 36, 0, 52, 99, 0, 45, 73, 0, 0, 42, 29]], ["A52 Lead II", [24, 88, 0, 53, 0, 45, 0, 43, 0, 52, 56, 1, 23, 91, 75, 79, 57, 20]], ["A53 Lead III", [86, 71, 21, 102, 0, 60, 31, 18, 1, 76, 127, 0, 66, 48, 11, 0, 50, 21]], ["A54 Funky II", [4, 0, 0, 48, 0, 5, 23, 81, 0, 41, 100, 0, 30, 39, 2, 57, 74, 21]], ["A55 Synth Bass II", [90, 21, 0, 0, 0, 43, 0, 47, 0, 0, 78, 0, 37, 9, 22, 45, 49, 28]], ["A56 Funky III", [4, 0, 0, 9, 101, 38, 54, 45, 0, 10, 79, 0, 24, 0, 2, 127, 57, 29]], ["A57 Thud Wah", [62, 0, 0, 102, 8, 78, 89, 34, 0, 0, 106, 0, 0, 102, 45, 32, 26, 26]], ["A58 Going Up", [0, 0, 0, 100, 28, 79, 127, 37, 0, 65, 75, 0, 108, 18, 127, 0, 34, 19]], ["A61 Piano II", [1, 0, 0, 72, 0, 47, 0, 39, 0, 5, 78, 0, 98, 0, 32, 115, 42, 16]], ["A62 Clav", [66, 15, 1, 102, 0, 7, 11, 86, 2, 0, 120, 0, 39, 48, 14, 0, 73, 21]], ["A63 Frontier Organ", [79, 15, 3, 85, 0, 69, 54, 0, 0, 52, 127, 0, 0, 127, 0, 29, 42, 28]], ["A64 Snare Drum (unison)", [52, 27, 0, 0, 91, 94, 11, 17, 0, 0, 101, 0, 25, 0, 30, 64, 34, 17]], ["A65 Tom Toms (unison)", [62, 16, 8, 0, 127, 53, 4, 40, 0, 21, 101, 0, 30, 15, 40, 58, 33, 24]], ["A66 Timpani (unison)", [0, 0, 0, 35, 127, 25, 0, 54, 0, 24, 127, 1, 56, 26, 71, 47, 33, 17]], ["A67 Shaker", [21, 71, 0, 58, 127, 89, 45, 1, 17, 80, 127, 10, 9, 0, 0, 0, 34, 1]], ["A68 Synth Pad", [56, 0, 5, 43, 0, 37, 0, 84, 0, 127, 51, 0, 85, 75, 62, 37, 10, 24]], ["A71 Sweep I", [78, 0, 0, 102, 28, 115, 40, 67, 0, 0, 97, 0, 63, 127, 82, 42, 12, 18]], ["A72 Pluck Sweep", [68, 48, 0, 68, 0, 60, 111, 9, 6, 98, 45, 0, 91, 0, 77, 125, 26, 8]], ["A73 Repeater", [77, 20, 9, 101, 0, 97, 30, 56, 0, 57, 87, 14, 0, 41, 0, 44, 74, 23]], ["A74 Sweep II", [88, 72, 15, 97, 0, 66, 102, 68, 0, 70, 101, 0, 89, 53, 78, 48, 74, 25]], ["A75 Pluck Bell", [51, 0, 5, 17, 0, 39, 0, 49, 0, 127, 49, 0, 85, 75, 53, 49, 12, 24]], ["A76 Dark Synth Piano", [127, 0, 0, 0, 0, 56, 86, 6, 0, 71, 79, 0, 47, 0, 50, 127, 26, 26]], ["A77 Sustainer", [71, 0, 5, 46, 0, 39, 0, 84, 0, 127, 57, 0, 85, 75, 70, 49, 42, 24]], ["A78 Wah Release", [89, 14, 1, 25, 0, 72, 88, 19, 0, 66, 104, 0, 84, 0, 29, 87, 10, 19]], ["A81 Gong (play low chords)", [127, 0, 0, 102, 56, 70, 107, 5, 0, 72, 91, 3, 48, 127, 98, 0, 73, 19]], ["A82 Resonance Funk", [59, 0, 0, 0, 0, 26, 127, 45, 0, 89, 127, 0, 19, 0, 22, 0, 33, 17]], ["A83 Drum Booms* (1 oct. down)", [0, 0, 0, 102, 127, 42, 0, 34, 0, 58, 127, 0, 36, 15, 49, 46, 33, 25]], ["A84 Dust Storm", [0, 0, 0, 0, 127, 52, 85, 0, 44, 94, 127, 88, 91, 28, 85, 0, 33, 17]], ["A85 Rocket Men", [8, 32, 0, 102, 97, 73, 114, 8, 0, 56, 127, 0, 89, 127, 104, 0, 36, 3]], ["A86 Hand Claps", [59, 0, 0, 0, 127, 17, 88, 54, 0, 55, 127, 1, 11, 0, 8, 0, 33, 5]], ["A87 FX Sweep", [127, 65, 116, 102, 127, 71, 44, 83, 0, 94, 127, 0, 94, 0, 112, 127, 41, 18]], ["A88 Caverns", [0, 0, 0, 102, 127, 68, 118, 0, 0, 69, 107, 0, 13, 38, 47, 0, 33, 8]], ["B11 Strings", [57, 45, 0, 55, 0, 85, 0, 0, 0, 108, 52, 59, 32, 86, 40, 0, 26, 24]], ["B12 Violin", [66, 45, 20, 0, 0, 77, 0, 0, 0, 120, 110, 43, 45, 57, 26, 0, 50, 24]], ["B13 Chorus Vibes", [72, 45, 0, 0, 0, 10, 0, 59, 0, 23, 88, 0, 89, 47, 74, 0, 74, 25]], ["B14 Organ 1", [45, 42, 0, 73, 0, 60, 72, 14, 0, 127, 58, 0, 0, 0, 0, 97, 10, 29]], ["B15 Harpsichord 1", [23, 0, 0, 54, 0, 127, 127, 90, 0, 86, 125, 0, 60, 0, 31, 35, 44, 0]], ["B16 Recorder", [78, 0, 0, 8, 0, 6, 0, 46, 0, 127, 127, 5, 21, 127, 30, 0, 42, 17]], ["B17 Perc. Pluck", [62, 38, 8, 0, 0, 0, 0, 90, 0, 73, 65, 0, 16, 78, 116, 0, 74, 25]], ["B18 Noise Sweep", [127, 0, 0, 0, 127, 2, 0, 77, 0, 104, 92, 14, 78, 108, 120, 0, 33, 24]], ["B21 Space Chimes", [99, 0, 0, 0, 0, 70, 127, 0, 0, 74, 85, 0, 25, 0, 60, 0, 76, 24]], ["B22 Nylon Guitar", [72, 45, 0, 29, 0, 57, 0, 26, 0, 25, 115, 0, 89, 0, 32, 0, 42, 1]], ["B23 Orchestral Pad", [22, 45, 0, 105, 0, 33, 0, 55, 0, 36, 0, 29, 88, 50, 52, 127, 26, 24]], ["B24 Bright Pluck", [0, 0, 0, 58, 0, 57, 0, 40, 0, 86, 104, 0, 25, 42, 44, 0, 42, 17]], ["B25 Organ Bell", [78, 0, 6, 68, 0, 61, 0, 0, 0, 127, 62, 0, 0, 127, 26, 85, 90, 25]], ["B26 Accordion", [0, 0, 0, 64, 0, 77, 0, 14, 0, 74, 66, 8, 11, 110, 9, 80, 90, 1]], ["B27 FX Rise 1", [116, 0, 0, 0, 0, 108, 127, 16, 127, 64, 0, 0, 0, 127, 84, 0, 68, 26]], ["B28 FX Rise 2", [108, 0, 0, 0, 0, 94, 127, 80, 23, 127, 0, 0, 127, 51, 71, 0, 2, 26]], ["B31 Brass", [51, 127, 0, 73, 0, 0, 0, 94, 0, 127, 72, 3, 44, 51, 11, 0, 82, 28]], ["B32 Helicopter", [106, 0, 0, 48, 0, 82, 5, 93, 127, 0, 72, 0, 0, 35, 76, 127, 10, 27]], ["B33 Lute", [52, 0, 2, 105, 0, 29, 0, 35, 0, 86, 127, 0, 48, 34, 87, 0, 42, 25]], ["B34 Chorus Funk", [77, 94, 0, 73, 0, 47, 34, 53, 0, 65, 42, 0, 11, 34, 0, 79, 90, 29]], ["B35 Tomita", [78, 0, 0, 105, 0, 49, 125, 15, 2, 127, 57, 0, 14, 79, 0, 0, 36, 31]], ["B36 FX Sweep 1", [127, 0, 0, 0, 0, 58, 127, 16, 76, 64, 0, 0, 0, 127, 85, 0, 68, 24]], ["B37 Sharp Reed", [21, 0, 0, 73, 0, 33, 0, 53, 0, 65, 55, 2, 18, 112, 0, 0, 60, 28]], ["B38 Bass Pluck", [0, 0, 0, 60, 0, 37, 29, 28, 0, 43, 92, 0, 42, 35, 0, 0, 57, 24]], ["B41 Resonant Rise", [52, 0, 15, 0, 0, 66, 107, 31, 0, 73, 62, 0, 52, 39, 0, 0, 17, 30]], ["B42 Harpsichord 2", [0, 0, 0, 105, 0, 127, 95, 0, 0, 127, 107, 0, 45, 0, 0, 35, 60, 17]], ["B43 Dark Ensemble", [0, 0, 0, 57, 0, 55, 0, 0, 0, 107, 67, 21, 0, 127, 35, 75, 90, 27]], ["B44 Contact Wah", [78, 0, 0, 81, 0, 61, 101, 46, 0, 127, 127, 0, 0, 0, 0, 0, 42, 23]], ["B45 Noise Sweep 2", [127, 0, 0, 0, 127, 127, 0, 83, 0, 104, 116, 127, 79, 26, 83, 0, 68, 26]], ["B46 Glassy Wah", [0, 94, 0, 28, 0, 24, 34, 61, 0, 65, 112, 6, 18, 50, 38, 0, 42, 25]], ["B47 Phase Ensemble", [11, 45, 5, 105, 0, 79, 0, 0, 0, 127, 99, 84, 45, 57, 49, 0, 10, 24]], ["B48 Chorused Bell", [0, 11, 0, 105, 0, 59, 127, 0, 0, 127, 62, 0, 62, 0, 57, 101, 76, 24]], ["B51 Clav", [50, 11, 0, 91, 0, 32, 0, 63, 0, 0, 108, 0, 43, 55, 0, 7, 41, 9]], ["B52 Organ 2", [81, 0, 0, 104, 0, 54, 106, 7, 0, 127, 79, 0, 0, 0, 0, 0, 81, 29]], ["B53 Bassoon", [84, 38, 9, 90, 0, 35, 0, 43, 9, 98, 127, 5, 127, 106, 2, 0, 41, 1]], ["B54 Auto Release Noise Sweep", [127, 0, 0, 0, 127, 127, 0, 127, 0, 104, 107, 0, 79, 57, 83, 0, 68, 26]], ["B55 Brass Ensemble", [25, 94, 0, 73, 0, 47, 34, 35, 0, 65, 39, 6, 68, 67, 38, 0, 90, 24]], ["B56 Ethereal", [69, 0, 0, 105, 0, 47, 120, 0, 0, 127, 60, 92, 50, 127, 55, 59, 26, 24]], ["B57 Chorus Bell 2", [72, 45, 0, 29, 0, 0, 102, 62, 0, 90, 30, 0, 117, 0, 120, 0, 90, 25]], ["B58 Blizzard", [1, 0, 0, 105, 127, 56, 89, 7, 38, 121, 127, 78, 98, 87, 78, 0, 36, 0]], ["B61 E. Piano with Tremolo", [44, 0, 0, 21, 0, 22, 0, 35, 7, 107, 103, 0, 65, 60, 98, 127, 44, 16]], ["B62 Clarinet", [69, 22, 9, 0, 0, 35, 0, 43, 7, 98, 104, 5, 127, 106, 7, 0, 42, 1]], ["B63 Thunder", [127, 0, 0, 0, 127, 2, 0, 77, 0, 104, 107, 0, 22, 108, 120, 0, 33, 24]], ["B64 Reedy Organ", [35, 18, 0, 73, 0, 33, 0, 53, 0, 65, 69, 3, 44, 109, 0, 0, 42, 28]], ["B65 Flute / Horn", [34, 0, 0, 59, 0, 25, 0, 42, 0, 65, 22, 21, 73, 71, 48, 0, 25, 24]], ["B66 Toy Rhodes", [30, 45, 127, 0, 0, 58, 127, 0, 0, 127, 107, 0, 89, 0, 39, 0, 36, 0]], ["B67 Surf's Up", [1, 0, 0, 105, 127, 84, 11, 0, 49, 54, 75, 33, 97, 127, 121, 0, 36, 10]], ["B68 OW Bass", [50, 0, 0, 50, 0, 63, 84, 65, 0, 127, 70, 127, 0, 107, 0, 47, 57, 31]], ["B71 Piccolo", [70, 28, 0, 14, 0, 4, 0, 63, 16, 98, 87, 16, 103, 106, 21, 0, 60, 9]], ["B72 Melodic Taps", [99, 0, 0, 0, 127, 72, 112, 0, 0, 127, 127, 0, 24, 0, 24, 0, 65, 0]], ["B73 Meow Brass", [51, 127, 0, 73, 0, 45, 100, 35, 0, 65, 84, 4, 90, 0, 27, 0, 50, 28]], ["B74 Violin (high)", [71, 45, 22, 0, 0, 89, 0, 0, 3, 120, 85, 43, 45, 57, 26, 0, 52, 24]], ["B75 High Bells", [30, 45, 127, 0, 0, 76, 127, 0, 0, 127, 111, 0, 22, 52, 59, 0, 36, 0]], ["B76 Rolling Wah", [36, 0, 0, 105, 0, 60, 9, 0, 127, 0, 77, 34, 55, 127, 105, 0, 42, 24]], ["B77 Ping Bell", [0, 11, 0, 104, 0, 76, 127, 0, 0, 127, 83, 0, 23, 23, 57, 0, 76, 24]], ["B78 Brassy Organ", [16, 0, 0, 68, 0, 8, 0, 87, 0, 52, 14, 0, 36, 109, 0, 56, 89, 24]], ["B81 Low Dark Strings", [21, 45, 5, 81, 0, 71, 0, 0, 0, 103, 40, 83, 23, 109, 42, 0, 25, 24]], ["B82 Piccolo Trumpet", [51, 127, 0, 73, 0, 0, 0, 94, 0, 127, 97, 3, 44, 51, 11, 0, 50, 28]], ["B83 Cello", [57, 45, 21, 0, 0, 75, 0, 3, 1, 45, 109, 48, 65, 90, 34, 0, 49, 24]], ["B84 High Strings", [80, 0, 10, 70, 0, 92, 0, 0, 0, 71, 48, 27, 57, 57, 41, 0, 28, 24]], ["B85 Rocket Men", [108, 0, 0, 0, 0, 110, 127, 80, 63, 127, 0, 0, 127, 51, 89, 0, 2, 26]], ["B86 Forbidden Planet", [50, 11, 0, 44, 0, 29, 4, 88, 5, 95, 79, 0, 48, 23, 42, 9, 76, 1]], ["B87 Froggy", [78, 0, 0, 0, 0, 55, 127, 43, 0, 127, 77, 0, 0, 0, 0, 127, 41, 23]], ["B88 Owgan", [50, 0, 0, 45, 0, 38, 84, 32, 0, 127, 101, 0, 49, 55, 0, 56, 57, 25]]]
def get_juno_patch(patch_number):
  # json was created by:
  # import javaobj, json
  # pobj = javaobj.load(open('juno106_factory_patches.ser', 'rb'))
  # patches = [(p.name, list(p.sysex)) for p in pobj.v.elementData if p is not None]
  # with open('juno106patches.json', 'w') as f:
  #   f.write(json.dumps(patches))
  #pobj = javaobj.load(open('juno106_factory_patches.ser', 'rb'))
  #patch = pobj.v.elementData[patch_number]
  global _PATCHES
  name, sysex = _PATCHES[patch_number]
  return name, bytes(sysex)


class JunoPatch:
  """Encapsulates information in a Juno Patch."""
  name = ""
  lfo_rate = 0
  lfo_delay_time = 0
  dco_lfo = 0
  dco_pwm = 0
  dco_noise = 0
  vcf_freq = 0
  vcf_res = 0
  vcf_env = 0
  vcf_lfo = 0
  vcf_kbd = 0
  vca_level = 0
  env_a = 0
  env_d = 0
  env_s = 0
  env_r = 0
  dco_sub = 0
  stop_16 = False
  stop_8 = False
  stop_4 = False
  pulse = False
  saw = False
  chorus = 0
  pwm_manual = False  # else lfo
  vca_gate = False  # else env
  vcf_neg = False  # else pos
  hpf = 0
  portamento = 0
  # Map of setup_fn: [params triggering setup]
  post_set_fn = {'lfo': ['lfo_rate', 'lfo_delay_time'],
                 'dco': ['dco_lfo', 'dco_pwm', 'dco_noise', 'dco_sub',
                         'stop_16', 'stop_8', 'stop_4',
                         'pulse', 'saw', 'pwm_manual', 'vca_level', 'vca_gate',
                         'portamento'],
                 'vcf': ['vcf_neg', 'vcf_env', 'vcf_freq', 'vcf_lfo', 'vcf_res', 'vcf_kbd'],
                 'env': ['env_a', 'env_d', 'env_s', 'env_r'],
                 'cho': ['chorus', 'hpf']}

  # These lists name the fields in the order they appear in the sysex.
  FIELDS = ['lfo_rate', 'lfo_delay_time', 'dco_lfo', 'dco_pwm', 'dco_noise',
           'vcf_freq', 'vcf_res', 'vcf_env', 'vcf_lfo', 'vcf_kbd', 'vca_level',
           'env_a', 'env_d', 'env_s', 'env_r', 'dco_sub']
  # After the 16 integer values, there are two bytes of bits.
  BITS1 = ['stop_16', 'stop_8', 'stop_4', 'pulse', 'saw']
  BITS2 = ['pwm_manual', 'vcf_neg', 'vca_gate']
  # Non-sysex state: Do we use the cheaper 12 dB/oct VCF?
  cheap_filter = False

  # Patch number we're based on, if any.
  patch_number = None
  # Name, if any
  name = None
  # Params that have been changed since last send_to_AMY.
  dirty_params = set()
  # Flag to defer param updates.
  defer_param_updates = False
  # Cache for sub-osc freq_coefs.
  sub_freq = '440'
  # List of the 5 basic oscs that need cloning.
  oscs_to_clone = set()
  # Amy synth
  amy_synth = None

  @staticmethod
  def from_patch_number(patch_number):
    name, sysexbytes = get_juno_patch(patch_number)
    return JunoPatch.from_sysex(sysexbytes, name, patch_number)

  @classmethod
  def from_sysex(cls, sysexbytes, name=None, patch_number=None):
    """Decode sysex bytestream into JunoPatch fields."""
    assert len(sysexbytes) == 18
    result = JunoPatch()
    result.name = name
    result.patch_number = patch_number
    result._init_from_sysex(sysexbytes)
    return result

  def _init_from_patch_number(self, patch_number):
    self.patch_number = patch_number
    self.name, sysexbytes = get_juno_patch(patch_number)
    self._init_from_sysex(sysexbytes)

  def _init_from_sysex(self, sysexbytes):
    # The first 16 bytes are sliders.
    for index, field in enumerate(self.FIELDS):
      setattr(self, field, int(sysexbytes[index])/127.0)
    # Then there are two bytes of switches.
    for index, field in enumerate(self.BITS1):
      setattr(self, field, (int(sysexbytes[16]) & (1 << index)) > 0)
    # Chorus has a weird mapping.  Bit 5 is ~Chorus, bit 6 is ChorusI-notII
    setattr(self, 'chorus', [2, 0, 1, 0][int(sysexbytes[16]) >> 5])
    for index, field in enumerate(self.BITS2):
      setattr(self, field, (int(sysexbytes[17]) & (1 << index)) > 0)
    # Bits 3 & 4 also have flipped endianness & sense.
    setattr(self, 'hpf', [3, 2, 1, 0][int(sysexbytes[17]) >> 3])
    # Nonstandard extension: Put "cheap" flag in bit 5 of byte 2.
    self.cheap_filter = int(sysexbytes[17] & (1 << 5)) > 0

  def to_sysex(self):
    """Return the 18 byte SYSEX corresponding to current object state."""
    byte_values = []
    # 16 continuous values in order specified by self.FIELDS.
    for field in self.FIELDS:
      byte_values.append(int(round(127.0 * getattr(self, field))))
    # 2 bytes of booleans.
    val = 0
    for index, field in enumerate(self.BITS1):
      val |= (1 << index) if getattr(self, field) else 0
    # Chorus has a weird mapping.  Bit 5 is ~Chorus, bit 6 is ChorusI-notII
    val |= ([1, 2, 0][getattr(self, 'chorus')]) << 5
    byte_values.append(val)
    val = 0
    for index, field in enumerate(self.BITS2):
      val |= (1 << index) if getattr(self, field) else 0
    # Bits 3 & 4 also have flipped endianness & sense.
    val |= ([3, 2, 1, 0][getattr(self, 'hpf')]) << 3
    # Nonstandard extension: Put "cheap" flag in bit 5 of byte 2.
    if self.cheap_filter:
      val |= (1 << 5)
    byte_values.append(val)
    return bytes(byte_values)

  def set_synth(self, amy_synth):
    self.amy_synth = amy_synth

  def _breakpoint_string(self):
    """Format a breakpoint string from the ADSR parameters reaching a peak."""
    return "%d,%s,%d,%s,%d,0" % (
      to_attack_time(self.env_a), ffmt(1.0), to_decay_time(self.env_d),
      ffmt(to_level(self.env_s)), to_release_time(self.env_r)
    )

  def _amp_coef_string(self, level):
    if self.vca_gate:
      # 'Gate' is provided by EG1, which is left unset == gate.
      return ',,%s,0,1' % ffmt(max(.001, to_level(level) * to_level(self.vca_level)))
    else:
      return ',,%s,1,0' % ffmt(max(.001, to_level(level) * to_level(self.vca_level)))

  def _freq_coef_string(self, base_freq):
    return '%s,1,,,,%s,1' % (
      ffmt(base_freq), ffmt(0.03 * to_level(self.dco_lfo)))

  def base_freq(self):
    # Only one of stop_{16,8,4} should be set.
    base_freq = 261.63  # The mid note
    if self.stop_16:
      base_freq /= 2
    elif self.stop_4:
      base_freq *= 2
    return base_freq

  def _portamento_ms(self, port_val):
    # Portamento maps exponentially from 1 = 10 ms to 127 = 2560 ms
    if port_val == 0:
      return 0
    return int(round(20 * math.pow(2, port_val * 127 / 30)))

  def init_AMY(self):
    """Output AMY commands to set up patches on all the allocated synth.
    Send amy.send(osc=0, note=50, vel=1) afterwards."""
    #amy.reset()
    # base_osc is pulse/PWM
    # base_osc + 1 is SAW
    # base_osc + 2 is SUBOCTAVE
    # base_osc + 3 is NOISE
    # base_osc + 4 is LFO
    #   env0 is VCA
    #   env1 is VCF
    # These are the canonical oscs (to add to amy.send(synth=..)).
    self.pwm_osc = 0
    self.saw_osc = 1
    self.sub_osc = 2
    self.nse_osc = 3
    self.lfo_osc = 4
    self.voice_oscs = [
      self.pwm_osc, self.saw_osc, self.sub_osc, self.nse_osc
    ]
    # One-time setup of oscs.
    self.amy_send(osc=self.lfo_osc, wave=amy.TRIANGLE, amp='1,,0,1')
    filter_type = amy.FILTER_LPF if self.cheap_filter else amy.FILTER_LPF24
    self.amy_send(osc=self.pwm_osc, wave=amy.PULSE,
                  mod_source=self.lfo_osc, chained_osc=self.saw_osc,
                  filter_type=filter_type)
    self.amy_send(osc=self.saw_osc, wave=amy.SAW_UP,
                  mod_source=self.lfo_osc, chained_osc=self.sub_osc)
    self.amy_send(osc=self.sub_osc, wave=amy.PULSE,
                  mod_source=self.lfo_osc, chained_osc=self.nse_osc)
    self.amy_send(osc=self.nse_osc, wave=amy.NOISE,
                  mod_source=self.lfo_osc)
    # Setup all the variable params.
    self.update_lfo()
    self.update_dco()
    self.update_vcf()
    self.update_env()
    self.update_cho()
    
  def update_lfo(self):
    lfo_args = {'freq': to_lfo_freq(self.lfo_rate),
                'bp0': '%i,1.0,10000,0' % to_lfo_delay(self.lfo_delay_time)}
    self.amy_send(osc=self.lfo_osc, **lfo_args)

  def update_dco(self):
    base_freq = self.base_freq()
    freq_str = self._freq_coef_string(base_freq)
    # PWM square wave.
    const_duty = 0
    lfo_duty = to_level(self.dco_pwm)
    if self.pwm_manual:
      # Swap duty parameters.
      const_duty, lfo_duty = lfo_duty, const_duty
    port_ms = self._portamento_ms(self.portamento)
    self.amy_send(
      osc=self.pwm_osc,
      amp=self._amp_coef_string(float(self.pulse)),
      freq=freq_str,
      portamento=port_ms,
      duty='%s,,,,,%s' % (
        ffmt(0.5 + 0.5 * const_duty), ffmt(0.5 * lfo_duty)
      ),
    )
    self.amy_send(
      osc=self.saw_osc,
      amp=self._amp_coef_string(float(self.saw)),
      freq=freq_str,
      portamento=port_ms,
    )
    self.amy_send(
      osc=self.sub_osc,
      amp=self._amp_coef_string(float(self.dco_sub)),
      freq=self._freq_coef_string(base_freq / 2.0),
      portamento=port_ms,
    )
    self.amy_send(
      osc=self.nse_osc,
      amp=self._amp_coef_string(float(self.dco_noise)),
    )

  def update_vcf(self):
    vcf_env_polarity = -1.0 if self.vcf_neg else 1.0
    self.amy_send(osc=self.pwm_osc, resonance=to_resonance(self.vcf_res),
                  filter_freq='%s,%s,,%s,,%s' % (
                    ffmt(to_filter_freq(self.vcf_freq)),
                    ffmt(to_level(self.vcf_kbd)),
                    ffmt(11 * vcf_env_polarity * to_level(self.vcf_env)),
                    ffmt(1.25 * to_level(self.vcf_lfo))))

  def update_env(self):
    bp0_coefs = self._breakpoint_string()
    self.amy_send(osc=self.pwm_osc, bp0=bp0_coefs)
    self.amy_send(osc=self.saw_osc, bp0=bp0_coefs)
    self.amy_send(osc=self.sub_osc, bp0=bp0_coefs)
    self.amy_send(osc=self.nse_osc, bp0=bp0_coefs)

  def update_cho(self):
    # Chorus & HPF
    eq_l = eq_m = eq_h = 0
    if self.hpf == 0:
      eq_l = 7
      eq_m = -3
      eq_h = -3
    elif self.hpf == 1:
      pass
    elif self.hpf == 2:
      eq_l = -8
    elif self.hpf == 3:
      eq_l = -15
      eq_m = 8
      eq_h = 8
    cho_args = {
      'eq': '%s,%s,%s' % (str(eq_l), str(eq_m), str(eq_h)),
    }
    if self.chorus == 0:
      cho_args['chorus'] = '0'
    else:
      # We choose juno 60-style I+II for chorus=3.  Juno 6-style would be freq=8 depth=0.25
      cho_args['chorus'] = '1,,%s,%s' % (
        (0, 0.5, 0.83, 1)[self.chorus],  # chorus_freq
        (0, 0.5, 0.5, 0.08)[self.chorus],  # chorus_depth
      )
    # *Don't* send to oscs, these ones are global.
    amy.send(**cho_args)

  # Setters for each Juno UI control
  def set_param(self, param, val):
    setattr(self, param,  val)
    if self.defer_param_updates:
      self.dirty_params.add(param)
    else:
      for group, params in self.post_set_fn.items():
        if param in params:
          getattr(self, 'update_' + group)()

  def send_deferred_params(self):
    for group, params in self.post_set_fn.items():
      if self.dirty_params.intersection(params):
        getattr(self, 'update_' + group)()
    self.dirty_params = set()
    self.defer_param_updates = False

  def set_patch(self, patch):
    self._init_from_patch_number(patch)
    #print("New patch", patch, ":", self.name)
    self.init_AMY()

  def set_sysex(self, sysex):
    self._init_from_sysex(sysex)
    #print("New patch", patch, ":", self.name)
    self.init_AMY()

  def set_pitch_bend(self, value):
    # Global, not osc-specific.
    amy.send(pitch_bend=value)

  def amy_send(self, osc, **kwargs):
    if self.amy_synth:
      amy.send(synth=self.amy_synth, osc=osc, **kwargs)
    else:
      amy.send(osc=osc, **kwargs)
