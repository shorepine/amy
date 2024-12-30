"""Piano notes generated on amy/tulip."""

# Uses the partials amplitude breakpoints and residual written by piano-partials.ipynb.

import json
import time
import amy

try:
    import midi
    have_midi = True
except:
    have_midi = False

try:
    from ulab import numpy as np
except:
    import numpy as np

# Read in the params file written by piano_heterodyne.ipynb
# Contents are:
#   sample_times_ms - single vector of fixed log-spaced envelope sample times (in int16 integer ms)
#   notes - Array of MIDI notes
#   velocities - Array of strike velocities available for each note, the same for all notes
#   num_harmonics - Array of (num_notes * num_velocities) counts of how many harmonics are defined for each note+vel.
#   harmonics_freq - Vector of (total_num_harmonics) int16s giving freq for each harmonic in cents 6900 = 440 Hz.
#   harmonics_mags - Array of (total_num_harmonics, num_sample_times) uint8s giving envelope samples for each harmonic.  In dB re: -130.

try:
    with open('piano-params.json', 'r') as f:
        notes_params = json.load(f)
except OSError:
    # Special case for piano_examples.py
    with open('examples/piano-params.json', 'r') as f:
        notes_params = json.load(f)

NOTES = np.array(notes_params['notes'], dtype=np.int8)
VELOCITIES = np.array(notes_params['velocities'], dtype=np.int8)
NUM_HARMONICS = np.array(notes_params['num_harmonics'], dtype=np.int16)
assert len(NUM_HARMONICS) == len(NOTES) * len(VELOCITIES)
NUM_MAGS = len(notes_params['harmonics_mags'][0])
FREQ_MAGS = np.zeros((np.sum(NUM_HARMONICS), 1 + NUM_MAGS), dtype=np.int16)
FREQ_MAGS[:, 0] = np.array(notes_params['harmonics_freq'], dtype=np.int16)
FREQ_MAGS[:, 1:] = np.array(notes_params['harmonics_mags'], dtype=np.int16)
# Add in a derived diff-times and start-harmonic fields
last_time = 0
DIFF_TIMES = []
for sample_time in notes_params['sample_times_ms']:
  DIFF_TIMES.append(sample_time - last_time)
  last_time = sample_time
START_HARMONIC = np.zeros(len(NUM_HARMONICS), dtype=np.int16)
start_h = 0
for i, n in enumerate(NUM_HARMONICS):
    START_HARMONIC[i] = start_h
    start_h += n
#print(f'{NUM_HARMONICS=} {START_HARMONIC=}')


def interp_harms_params(hp0, hp1, alpha):
    """Return harm_param list that is alpha of the way to hp1 from hp0."""
    # hp_ is [freq, mag0, mag1, ...]
    num_harmonics = min(hp0.shape[0], hp1.shape[0])
    # Assume the units are log-scale, so linear interpolation is good.
    return hp0[:num_harmonics] + alpha * (hp1[:num_harmonics] - hp0[:num_harmonics])


def cents_to_hz(cents):
    """Convert freq cents back to Hzin hz to real-valued midi: 60 = C4 = 261.63, 69 = A4 = 440.0."""
    return 440 * (2 ** ((cents - 6900) / 1200.0))
  

def db_to_lin(d):
    # There's a -100 (-130 + 30) dB downshift.  Clip anything below 0.001 to zero, to avoid E-05 format etc.
    return np.maximum(0, 10.0 ** ((d - 100) / 20.0) - 0.001)


def harm_param_to_lin(bps):
    """Convert harm_param from (cents, db0, db1, ...) to (hz, lin0, lin1, ...)."""
    return np.concatenate([np.array([cents_to_hz(bps[0])]), db_to_lin(bps[1:])])


def harms_params_to_lin(harms_params):
    """Convert a list of harm_params from log to lin format."""
    return [harm_param_to_lin(hp) for hp in harms_params]


def harms_params_from_note_index_vel_index(note_index, vel_index):
    """Reconstruct a (log-domain) harms_params list from the globals given note and vel indices."""
    note_vel_index = note_index * len(VELOCITIES) + vel_index
    num_harmonics = NUM_HARMONICS[note_vel_index]
    start_harmonic = START_HARMONIC[note_vel_index]
    harms_params = FREQ_MAGS[start_harmonic : start_harmonic + num_harmonics, :]
    #print(f'{note_index=} {vel_index=} {start_harmonic=} {harms_params=}')
    return harms_params


def harms_params_for_note_vel(note, vel):
    """Convert midi note and velocity into an interpolated harms_params list of harmonic specifications."""
    note = np.clip(note, NOTES[0], NOTES[-1])
    vel = np.clip(vel, VELOCITIES[0], VELOCITIES[-1])
    note_index = -1 + np.sum(NOTES[:-1] <= note)  # at most the last-but-one value.
    strike_index = -1 + np.sum(VELOCITIES[:-1] <= vel)
    lower_note = NOTES[note_index]
    upper_note = NOTES[note_index + 1]
    note_alpha = (note - lower_note) / (upper_note - lower_note)
    lower_strike = VELOCITIES[strike_index]
    upper_strike = VELOCITIES[strike_index + 1]
    strike_alpha = (vel - lower_strike) / (upper_strike - lower_strike)
    #print(f'{lower_note=} {note_alpha=} {lower_strike=} {strike_alpha=}')
    harms_params = interp_harms_params(
        interp_harms_params(
            harms_params_from_note_index_vel_index(note_index, strike_index),
            harms_params_from_note_index_vel_index(note_index + 1, strike_index),
            note_alpha,
        ),
        interp_harms_params(
            harms_params_from_note_index_vel_index(note_index, strike_index + 1),
            harms_params_from_note_index_vel_index(note_index + 1, strike_index + 1),
            note_alpha,
        ),
        strike_alpha,
    )
    return harms_params


def init_piano_voice(num_partials, base_osc=0, **kwargs):
  """One-time initialization of the unchanging parts of the partials voices."""
  amy_send(osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials, amp={'eg0': 0}, **kwargs)
  harm_nums = range(1, num_partials + 1)
  for i in harm_nums:
    bp_string = '0,0,' + ','.join("%d,0" % t for t in DIFF_TIMES)
    bp_string += ',200,0'
    amy_send(osc=base_osc + i, wave=amy.PARTIAL, bp0=bp_string, eg0_type=amy.ENVELOPE_TRUE_EXPONENTIAL, **kwargs)


def setup_piano_voice_for_note_vel(note, vel, base_osc=0, **kwargs):
  """Set up a sequence of oscs to play a particular note and velocity."""
  harms_params = harms_params_for_note_vel(note, vel)
  num_partials = len(harms_params)
  amy_send(osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials, **kwargs)
  for i in range(num_partials):
    f0 = cents_to_hz(harms_params[i, 0])
    h_vals = db_to_lin(harms_params[i, 1:])
    bp_string = ',,' + ','.join(",%.3f" % val for val in h_vals)
    bp_string += ',200,0'
    amy_send(osc=base_osc + 1 + i, freq=f0, bp0=bp_string, **kwargs)


def piano_note_on(note=60, vel=1, **kwargs):
    if vel == 0:
        # Note off.
        amy.send(vel=0, **kwargs)
    else:
        setup_piano_voice_for_note_vel(
            note, round(vel * 127), **kwargs
        )
        # We already configured the freuquencies and magnitudes in setup, so
        # the note on is completely neutral.
        amy_send(note=60, vel=1, **kwargs)


#piano_note_on(60, 1)
#time.sleep(1.0)
#piano_note_on(60, 0)

def amy_send(**kwargs):
    amy.send(**kwargs)
    #m = amy.message(**kwargs)
    #print('amy.send(' + m + ')')
    #amy.send_raw(m)


amy.reset()
time.sleep(0.1)  # to let reset happen.
#amy.send(store_patch='1024,v0w10Zv%dw%dZ')
#amy.send(voices='0,1,2,3', load_patch=1024)
num_partials = NUM_HARMONICS[0]
patch_string = 'v0w10Zv%dw%dZ' % (num_partials + 1, amy.PARTIAL)


if have_midi:
    synth_obj = midi.Synth(num_voices=4, patch_string=patch_string)
    #voices = '0,1,2,3'
    voices = ",".join([str(a) for a in synth_obj.amy_voice_nums])
    #print("voices=", voices)
    # We have to intercept the note-on events of the Synth voice objects.
    class PianoVoiceObject:
        """Substitute for midi.VoiceObject for Piano voices."""

        def __init__(self, amy_voice):
            self.amy_voice = amy_voice

        def note_on(self, note, vel, time=None, sequence=None):
            #amy.send(time=time, voices=self.amy_voice, note=note, vel=vel, sequence=sequence)
            piano_note_on(note, vel, voices=self.amy_voice, time=time, sequence=sequence)

        def note_off(self, time=None, sequence=None):
            amy.send(time=time, voices=self.amy_voice, vel=0, sequence=sequence)


    # Intercept the voice objects for our synth
    synth_obj.voice_objs = [PianoVoiceObject(obj.amy_voice) for obj in synth_obj.voice_objs]
    for voice_obj in synth_obj.voice_objs:
        init_piano_voice(num_partials, voices=voice_obj.amy_voice)
        time.sleep(0.05)  # Let the amy queue catch up.

    midi.config.add_synth_object(channel=1, synth_object=synth_obj)
