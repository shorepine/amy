
# piano.py
# examples from piano.html
import amy
from . import piano_params

def piano_example(base_note=72, volume=5, send_command=amy.send, init_command=lambda: None):
    amy.send(reset=amy.RESET_TIMEBASE)
    amy.send(time=0, volume=volume)
    init_command()
    send_command(time=50, voices='0', note=base_note, vel=0.05)
    send_command(time=435, voices='0', note=base_note, vel=0)
    send_command(time=450, voices='0', note=base_note, vel=0.63)
    send_command(time=835, voices='0', note=base_note, vel=0)
    send_command(time=850, voices='0', note=base_note, vel=1.0)
    send_command(time=1485, voices='0', note=base_note, vel=0)
    send_command(time=1500, voices='1', note=base_note - 24, vel=0.6)
    send_command(time=2100, voices='2', note=base_note + 24, vel=1.0)
    send_command(time=3000, voices='1', note=base_note - 24, vel=0)
    send_command(time=3000, voices='2', note=base_note + 24, vel=0)


def juno_example():
	piano_example(base_note=74, volume=10, 
		init_command=lambda: amy.send(time=0, voices='0,1,2', patch_number=7))

def dx7_example():
	piano_example(base_note=50, volume=25, 
              init_command=lambda: amy.send(time=0, voices='0,1,2', patch_number=137))


"""Piano notes generated on amy/tulip."""
# Uses the partials amplitude breakpoints and residual written by piano_heterodyne.ipynb.
try:
    from ulab import numpy as np
except ImportError:
    import numpy as np

# Read in the params file written by piano_heterodyne.ipynb
# Contents:
#   sample_times_ms - single vector of fixed log-spaced envelope sample times (in int16 integer ms).
#   notes - the MIDI numbers corresponding to each note described.
#   velocities - The (MIDI) strike velocities available for each note, the same for all notes.
#   num_harmonics - Array of (num_notes * num_velocities) counts of how many harmonics are defined for each note+vel combination.
#   harmonics_freq - Vector of (total_num_harmonics) int16s giving freq for each harmonic in "MIDI cents" i.e. 6900 = 440 Hz.
#   harmonics_mags - Array of (total_num_harmonics, num_sample_times) uint8s giving envelope samples for each harmonic.  In dB, between 0 and 100.

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
for i in range(len(NUM_HARMONICS)):  # No cumsum in ulab.numpy
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
    amy.send(osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials, amp={'eg0': 0}, **kwargs)
    for partial in range(1, num_partials + 1):
        bp_string = '0,0,' + ','.join("%d,0" % t for t in DIFF_TIMES)
        # We append a release segment to die away to silence over 200ms on note-off.
        bp_string += ',200,0'
        amy.send(osc=base_osc + partial, wave=amy.PARTIAL, bp0=bp_string, eg0_type=amy.ENVELOPE_TRUE_EXPONENTIAL, **kwargs)

def setup_piano_voice(harms_params, base_osc=0, **kwargs):
    """Configure a set of PARTIALs oscs to play a particular note and velocity."""
    num_partials = len(harms_params)
    amy.send(osc=base_osc, wave=amy.BYO_PARTIALS, num_partials=num_partials, **kwargs)
    for i in range(num_partials):
        f0_hz = cents_to_hz(harms_params[i, 0])
        env_vals = db_to_lin(harms_params[i, 1:])
        # Omit the time-deltas from the list to save space.  The osc will keep the ones we set up in init_piano_voice.
        bp_string = ',,' + ','.join(",%.3f" % val for val in env_vals)
        # Add final release.
        bp_string += ',200,0'
        amy.send(osc=base_osc + 1 + i, freq=f0_hz, bp0=bp_string, **kwargs)

patch_string = 'v0w10Zv%dw%dZ' % (NUM_HARMONICS[0] + 1, amy.PARTIAL)

# The lowest note provides an upper-bound on the number of partials we need to allocate.
def init_piano_voices(num_partials=NUM_HARMONICS[0]):
    amy.send(patch_number='1024', patch=patch_string)
    amy.send(voices='0,1,2', patch_number=1024)
    init_piano_voice(num_partials, voices='0,1,2')
    # piano_note_on (below) overwrites these settings before each note,
    # but pre-configure each note to C4.mf so we can experiment.
    setup_piano_voice(harms_params_for_note_vel(note=60, vel=80), voices='0,1,2')

def play_piano_partials_1():
	piano_example(base_note=62, volume=5, init_command=init_piano_voices)


def piano_note_on(note=60, vel=1, **kwargs):
    if vel == 0:
        # Note off.
        amy.send(vel=0, **kwargs)
    else:
        setup_piano_voice(harms_params_for_note_vel(note, round(vel * 127)), **kwargs)
        # We already configured the pitches and magnitudes in setup, so
        # the note and vel of the note-on are always the same.
        amy.send(note=60, vel=1, **kwargs)

def play_piano_partials_2():
	piano_example(base_note=62,
              init_command=init_piano_voices,
              send_command=piano_note_on)



