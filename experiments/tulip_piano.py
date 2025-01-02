"""Piano notes generated on amy/tulip."""
# Uses the partials amplitude breakpoints and residual written by piano-partials.ipynb.

import json
import time
import amy

try:
    import midi
    have_midi = True
except:
    print("midi not found")
    have_midi = False

try:
    from ulab import numpy as np
except:
    import numpy as np

# Read in the params file written by piano_heterodyne.ipynb
# Contents:
#   sample_times_ms - single vector of fixed log-spaced envelope sample times (in int16 integer ms).
#   notes - the MIDI numbers corresponding to each note described.
#   velocities - The (MIDI) strike velocities available for each note, the same for all notes.
#   num_harmonics - Array of (num_notes * num_velocities) counts of how many harmonics are defined for each note+vel combination.
#   harmonics_freq - Vector of (total_num_harmonics) int16s giving freq for each harmonic in "MIDI cents" i.e. 6900 = 440 Hz.
#   harmonics_mags - Array of (total_num_harmonics, num_sample_times) uint8s giving envelope samples for each harmonic.  In dB, between 0 and 100.

# We load this in as a python file, it's easier for porting to systems without filesystems
from piano_params import notes_params


NOTES = np.array(notes_params['notes'], dtype=np.int8)
VELOCITIES = np.array(notes_params['velocities'], dtype=np.int8)
NUM_HARMONICS = np.array(notes_params['num_harmonics'], dtype=np.int16)
assert len(NUM_HARMONICS) == len(NOTES) * len(VELOCITIES)
NUM_MAGS = len(notes_params['harmonics_mags'][0])
# Add in a derived diff-times and start-harmonic fields
# Reintroduce the initial zero-time...
SAMPLE_TIMES = np.array([0] + notes_params['sample_times_ms'])
#.. so we can neatly calculate the time-deltas needed for BP strings.
DIFF_TIMES = SAMPLE_TIMES[1:] - SAMPLE_TIMES[:-1]
# Lookup to find first harmonic for nth note.
START_HARMONIC = np.zeros(len(NUM_HARMONICS), dtype=np.int16)
for i in range(len(NUM_HARMONICS)):  # (No cumsum in ulab.numpy)
    START_HARMONIC[i] = np.sum(NUM_HARMONICS[:i])
# We build a single array for all the harmonics with the frequency as the
# first column, followed by the envelope magnitudes.  Then, we can pull
# out the entire description for a given note/velocity pair simply by
# pulling out NUM_HARMONICS[harmonic_index] rows starting at
# START_HARMONIC[harmonic_index]
FREQ_MAGS = np.zeros((np.sum(NUM_HARMONICS), 1 + NUM_MAGS), dtype=np.int16)
FREQ_MAGS[:, 0] = np.array(notes_params['harmonics_freq'], dtype=np.int16)
FREQ_MAGS[:, 1:] = np.array(notes_params['harmonics_mags'], dtype=np.int16)

def harms_params_from_note_index_vel_index(note_index, vel_index):
    """Retrieve a (log-domain) harms_params list for a given note/vel index pair."""
    # A harmonic is represented as a [freq_cents, mag1_db, mag2_db, .. mag20_db] row.
    # A note is represented as NUM_HARMONICS (usually 20) rows.
    note_vel_index = note_index * len(VELOCITIES) + vel_index
    num_harmonics = NUM_HARMONICS[note_vel_index]
    start_harmonic = START_HARMONIC[note_vel_index]
    harms_params = FREQ_MAGS[start_harmonic : start_harmonic + num_harmonics, :]
    return harms_params

def interp_harms_params(hp0, hp1, alpha):
    """Return harm_param list that is alpha of the way to hp1 from hp0."""
    # hp_ is [[freq_h1, mag1, mag2, ...], [freq_h2, mag1, mag2, ..], ...]
    num_harmonics = min(hp0.shape[0], hp1.shape[0])
    # Assume the units are log-scale, so linear interpolation is good.
    return hp0[:num_harmonics] + alpha * (hp1[:num_harmonics] - hp0[:num_harmonics])

def cents_to_hz(cents):
    """Convert 'Midi cents' frequency to Hz.  6900 cents -> 440 Hz"""
    return 440 * (2 ** ((cents - 6900) / 1200.0))
  
def db_to_lin(d):
    """Convert the db-scale magnitudes to linear.  0 dB -> 0.00001, so 100 dB -> 1.0."""
    # Clip anything below 0.001 to zero.
    return np.maximum(0, 10.0 ** ((d - 100) / 20.0) - 0.001)

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
    # We interpolate to describe a note at both strike indices,
    # then interpolate those to get the strike.
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
    for partial in range(1, num_partials + 1):
        bp_string = '0,0,' + ','.join("%d,0" % t for t in DIFF_TIMES)
        # We append a release segment to die away to silence over 200ms on note-off.
        bp_string += ',200,0'
        amy_send(osc=base_osc + partial, wave=amy.PARTIAL, bp0=bp_string, eg0_type=amy.ENVELOPE_TRUE_EXPONENTIAL, **kwargs)

def setup_piano_voice(harms_params, base_osc=0, voices=None, time=None, sequence=None):
    """Configure a set of PARTIALs oscs to play a particular note and velocity."""
    num_partials = len(harms_params)
    amy_send(osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials,
           voices=voices, time=time, sequence=sequence)
    if time is not None:
        base_cmd = 't' + str(time)
    else:
        base_cmd = ''
    if voices is not None:
        base_cmd += 'r' + str(voices)
    if sequence is not None:
        base_cmd += 'H' + str(sequence)
    for i in range(num_partials):
        # Omit the time-deltas from the list to save space.  The osc will keep the ones we set up in init_piano_voice.
        #env_vals = db_to_lin(harms_params[i, 1:])
        #bp_string = ',,' + ','.join(",%.3f" % val for val in env_vals)
        # bp_strings beginning with ".." are in special integer-dB format for fast transcoding.
        env_vals = harms_params[i, 1:]
        bp_string = '..,,' + ','.join(",%d" % val for val in env_vals)
        # Add final release.
        bp_string += ',200,0'
        f0_hz = cents_to_hz(harms_params[i, 0])
        #amy_send(osc=base_osc + 1 + i, freq=f0_hz, bp0=bp_string, voices=voices, time=time)
        # Special-case construction of the Wire Protocol message to save time
        amy.send_raw(
            base_cmd + 'v' + str(base_osc + i + 1) + ('f%.1f' % f0_hz) + 'A' + bp_string + 'Z'
        )

def piano_note_on(note=60, vel=1, **kwargs):
    if vel == 0:
        # Note off.
        amy.send(vel=0, **kwargs)
    else:
        hps = harms_params_for_note_vel(note, round(vel * 127))
        setup_piano_voice(hps, **kwargs)
        # We already configured the freuquencies and magnitudes in setup, so
        # the note on is completely neutral.
        amy_send(note=60, vel=1, **kwargs)


def amy_send(**kwargs):
    amy.send(**kwargs)



amy.reset()
time.sleep(0.1)  # to let reset happen.
amy.send(store_patch='1024,v0w10Zv%dw%dZ')
amy.send(voices='0,1,2,3', load_patch=1024)
num_partials = NUM_HARMONICS[0]
patch_string = 'v0w10Zv%dw%dZ' % (num_partials + 1, amy.PARTIAL)
print("Loaded dpwe piano on patch #1024, AMY voices 0,1,2,3")

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
    print("Added Tulip synth object to respond to MIDI channel 1")
