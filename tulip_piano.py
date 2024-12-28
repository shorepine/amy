"""Piano notes generated on amy/tulip."""

# Uses the partials amplitude breakpoints and residual written by piano-partials.ipynb.

import json
import time
import tulip
import amy
import midi
import math

#params_file = 'track02-C4-bps.json'
#residu_file = 'M1_Piano_C3_hprModel_residual_0.25s.wav'
#residu_file = 'track02-C4-resid-0.25s.wav'

residu_file = 'resid-0.25s.wav'
#pcm_patch = tulip.load_sample(residu_file)

# Read in the params file written by piano_heterodyne.ipynb
# Contents are:
#   sample_times_ms - single vector of fixed log-spaced envelope sample times (in int16 integer ms)
#   notes - Array of MIDI notes
#   velocities - Array of strike velocities available for each note, the same for all notes
#   num_harmonics - Array of (num_notes * num_velocities) counts of how many harmonics are defined for each note+vel.
#   harmonics_freq - Vector of (total_num_harmonics) int16s giving freq for each harmonic in cents 6900 = 440 Hz.
#   harmonics_mags - Array of (total_num_harmonics, num_sample_times) uint8s giving envelope samples for each harmonic.  In dB re: -130.

params_file = 'piano-params.json'
with open(params_file, 'r') as f:
    notes_params = json.load(f)
# Add in a derived diff-times field
last_time = 0
diff_times = []
for sample_time in notes_params['sample_times_ms']:
  diff_times.append(sample_time - last_time)
  last_time = sample_time
notes_params['diff_times'] = diff_times


def interp_harm_param(hp0, hp1, alpha):
    """Return harm_param list that is alpha of the way to hp1 from hp0."""
    # hp_ is [freq, [(time0, mag0), (time1, mag1), ...]].
    # Assume the units are log-scale, so linear interpolation is good.
    hp = []
    for h0, h1 in zip(hp0, hp1):
        f_a = h0[0] + alpha * (h1[0] - h0[0])
        bps = []
        for bp0, bp1 in zip(h0[1], h1[1]):
            bp = bp0[0] + alpha * (bp1[0] - bp0[0]), bp0[1] + alpha * (bp1[1] - bp0[1])
            bps.append(bp)
        hp.append((f_a, bps))
    return hp


def cents_to_hz(cents):
    """Convert freq cents back to Hzin hz to real-valued midi: 60 = C4 = 261.63, 69 = A4 = 440.0."""
    return 440 * (2 ** ((cents - 6900) / 1200))
  

def db_to_lin(d):
    return math.pow(10.0, (d - 130.0) / 20.0)


def harm_param_to_lin(bps):
    """Convert harm_param from (cents, (delta_ms, db)) to (hz, (delta_ms, lin))."""
    lin_bp = [(t, db_to_lin(m)) for t, m in bps[1]]
    return cents_to_hz(bps[0]), lin_bp


def harms_params_to_lin(harms_params):
    """Convert a list of harm_params from log to lin format."""
    return [harm_param_to_lin(hp) for hp in harms_params]


def harms_params_from_note_index_vel_index(note_index, vel_index, notes_params):
    """Reconstruct a (log-domain) harms_params list from the notes_params struct given note and vel indices."""
    note_vel_index = note_index * len(notes_params['velocities']) + vel_index
    harmonics_per_note_vel = notes_params['num_harmonics']
    harmonics_mags = notes_params['harmonics_mags']
    num_harmonics_this_note = harmonics_per_note_vel[note_vel_index]
    start_harmonic = sum(harmonics_per_note_vel[:note_vel_index])
    harms_params = []
    diff_times = []
    last_time = 0
    for harmonic in range(start_harmonic, start_harmonic + num_harmonics_this_note):
        time_val_pairs = [(0, 0)] + list(zip(notes_params['diff_times'], harmonics_mags[harmonic]))
        # Coerce val into normal float, convert db mag to lin.
        # time_val_pairs = [(time, db_to_lin(-130 + val)) for time, val in time_val_pairs]
        harms_params.append((notes_params['harmonics_freq'][harmonic], time_val_pairs))
    return harms_params


def harms_params_for_note_vel(note, vel, notes_params):
    """Convert midi note and velocity into an interpolated harms_params list of harmonic specifications."""
    notes = notes_params['notes']
    velocities = notes_params['velocities']
    num_harmonics = notes_params['num_harmonics']
    assert len(num_harmonics) == len(notes) * len(velocities)
    lower_note_index = max([i for i in range(len(notes)) if notes[i] <= note])
    upper_note_index = 0 if note < notes[0] else min(lower_note_index + 1, len(notes) - 1)
    lower_strike_index = max([0] + [i for i in range(len(velocities)) if velocities[i] <= vel])
    upper_strike_index = 0 if vel < velocities[0] else min(lower_strike_index + 1, len(velocities) - 1)
    lower_note = notes[lower_note_index]
    upper_note = notes[upper_note_index]
    note_alpha = 0 if lower_note == upper_note else (note - lower_note) / (upper_note - lower_note)
    lower_strike = velocities[lower_strike_index]
    upper_strike = velocities[upper_strike_index]
    strike_alpha = 0 if lower_strike == upper_strike else (vel - lower_strike) / (upper_strike - lower_strike)
    harms_params = interp_harm_param(
        interp_harm_param(
            harms_params_from_note_index_vel_index(lower_note_index, lower_strike_index, notes_params),
            harms_params_from_note_index_vel_index(upper_note_index, lower_strike_index, notes_params),
            note_alpha,
        ),
        interp_harm_param(
            harms_params_from_note_index_vel_index(lower_note_index, upper_strike_index, notes_params),
            harms_params_from_note_index_vel_index(upper_note_index, upper_strike_index, notes_params),
            note_alpha,
        ),
        strike_alpha,
    )
    return harms_params_to_lin(harms_params)


def setup_oscs(base_osc, note, vel, notes_params, **kwargs):
  """Set up a sequence of oscs to play a particular note and velocity."""
  harms_params = harms_params_for_note_vel(note, vel, notes_params)
  num_partials = len(harms_params)
  amy.send(osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials, amp={'eg0': 0}, **kwargs)
  for i in range(num_partials):
    f0 = harms_params[i][0]
    bp_vals = harms_params[i][1]
    bp_string = ','.join("%d,%.3f" % (n, max(0, 100 * val - 0.001)) for n, val in bp_vals)
    bp_string += ',200,0'
    amy.send(osc=base_osc + 1 + i, wave=amy.PARTIAL, freq=f0, bp0=bp_string, eg0_type=amy.ENVELOPE_TRUE_EXPONENTIAL, **kwargs)


def piano_note_on(note, vel=1, osc=0, **kwargs):
  setup_oscs(osc, note, round(vel * 127), notes_params, **kwargs)
  amy.send(osc=0, note=60, vel=1, **kwargs)  # We already put pitch and velocity into the setup.


#piano_note_on(60, 1)
#time.sleep(1.0)
#piano_note_on(60, 0)

amy.reset()
time.sleep(0.1)  # to let reset happen.
#amy.send(store_patch='1024,v0w10Zv%dw%dZ')
#amy.send(voices='0,1,2,3', load_patch=1024)
num_partials = notes_params['num_harmonics'][0]
patch_string = 'v0w10Zv%dw%dZ' % (num_partials + 1, amy.PARTIAL)
synth_obj = midi.Synth(num_voices=4, patch_string=patch_string)
#voices = '0,1,2,3'
voices = ",".join([str(a) for a in synth_obj.amy_voice_nums])
print("voices=", voices)

# We have to intercept the note-on events of the Synth voice objects.
class PianoVoiceObject:
    """Substitute for midi.VoiceObject for Piano voices."""

    def __init__(self, amy_voice):
        self.amy_voice = amy_voice

    def note_on(self, note, vel, time=None, sequence=None):
        #amy.send(time=time, voices=self.amy_voice, note=note, vel=vel, sequence=sequence)
        piano_note_on(note, vel, osc=0, voices=self.amy_voice, time=time, sequence=sequence)

    def note_off(self, time=None, sequence=None):
        amy.send(time=time, voices=self.amy_voice, vel=0, sequence=sequence)

# Intercept the voice objects for our synth
synth_obj.voice_objs = [PianoVoiceObject(obj.amy_voice) for obj in synth_obj.voice_objs]

midi.config.add_synth_object(channel=1, synth_object=synth_obj)

