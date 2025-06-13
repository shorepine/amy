# AMY module
from .constants import *
from . import examples
import collections
import time
try:
    import c_amy as _amy  # Import the C module
    live = _amy.live
except ImportError:
    # C module is not required, so pass
    pass



# If set, inserts func as time for every call to send(). Will not override an explicitly set time
insert_time = None

# If set, calls this instead of amy.send()
override_send = None

mess = []
log = False

show_warnings = True

block_cb = None

# Return a millis() epoch number for use in AMY timing
# On most computers, this uses ms since midnight using datetime
# On things like Tulip, this use ms since boot
def millis():
    try:
        import datetime
        d = datetime.datetime.now()
        return int((datetime.datetime.utcnow() - datetime.datetime(d.year, d.month, d.day)).total_seconds()*1000)
    except ImportError:
        import tulip
        return tulip.ticks_ms()


# Removes trailing 0s and x.0000s from floating point numbers to trim wire message size
# Fun historical trivia: this function caused a bug so bad that Dan had to file a week-long PR for micropython
# https://github.com/micropython/micropython/pull/8905
def trunc(number):
    if type(number) == str:
        if number.strip() == '':
            return ''
        number = float(number)
    if(type(number)==float):
        return ('%.6f' % number).rstrip('0').rstrip('.')
    return str(number)

def trunc3(number):
    if(type(number)==float):
        return ('%.3f' % number).rstrip('0').rstrip('.')
    return str(number)

def trim_trailing(vals, pred):
    """Remove any contiguous suffix of values that return False under pred."""
    bools = [pred(x) for x in vals[::-1]]
    suffix_len = bools.index(True)
    if suffix_len:
        return vals[:-suffix_len]
    return vals

def parse_ctrl_coefs(coefs):
    """Convert various acceptable forms of ControlCoefficient specs to the canonical string.

    ControlCoefficients determine how amplitude, frequency, filter frequency, PWM duty, and pan
    are calculated from underlying parameters on the fly.  For each control input, they specify
    seven coefficients which are multiplied by (0) a constant value of 1, (1) the log-frequency from
    the note-on command, (2) the velocity from the note-on command, (3) Envelope Generator 0's value,
    (4) Envelope Generator 1's value, (5) the modulating osicllator input, and (6) the global pitch
    bend value.  The sum of these scaled values is used as the control input. (Amplitude is a special
    case where the individual values are *multiplied* rather than added, and values whose coefficients
    are zero are skipped).

    The wire protocol expects these coefficients to be specified as a single vector, e.g. "f220,1,0,0,0,0,1".
    It also accepts some values to be left unspecified; only the specified values are changed.  So "f,,,,0.01"
    will add EG1 modulation to pitch but not change its base value etc.  As a special case, a single value
    (e.g. "f440") will change the constant offset for a parameter but leave its other modulations in place.

    The Python API accepts multiple kinds of input:
     * A scalar numeric value: freq=440
     * A list of values in the format accepted by the wire protocol: freq=',,,,0.01'.
     * A Python list of values, where None can be used to indicate "unspecified": freq=[None, None, None, None, 0.01].  Where the list is shorter than the expected 7 values, the remainder are treated as None (analogous to the wire-protocol string).
     * A Python dict providing values for some subset of the coefficients.  The only acceptable keys are 'const', 'note', 'vel', 'eg0', 'eg1', 'mod', and 'bend'.
    """
    # Pass through ready-formed strings, and convert single values to single value strings
    if isinstance(coefs, str):
        return ','.join(trunc(x) for x in coefs.split(','))
    if isinstance(coefs, int) or isinstance(coefs, float):
        return trunc(coefs)
    # Convert a dict into a list of values.
    dict_fields = ['const', 'note', 'vel', 'eg0', 'eg1', 'mod', 'bend']
    if isinstance(coefs, dict):
        coef_list = [None] * len(dict_fields)
        for key, value in coefs.items():
            if key not in dict_fields:
                raise ValueError('\'%s\' is not a recognized CtrlCoef field %s' % (key, str(dict_fields)))
            coef_list[dict_fields.index(key)] = value
        coefs = coef_list
    assert isinstance(coefs, list)
    coefs = trim_trailing(coefs, lambda x: x is not None)

    def to_str(x):
        if x is None:
            return ''
        return str(x)

    return ','.join([to_str(x) for x in coefs])

def parse_list_or_comma_string(obj):

    def str_none_is_empty(s):
        if s is None:
            return ""
        return str(s)

    if isinstance(obj, list):
        return ','.join(map(str_none_is_empty, obj))
    return str(obj)

def str_of_int(arg):
    """Cast arg to an int, but then convert it to a str for the wire string."""
    return str(int(arg))


_KW_MAP_LIST = [   # Order matters because patch_string must come last.
    ('osc', 'vI'), ('wave', 'wI'), ('note', 'nF'), ('vel', 'lF'), ('amp', 'aC'), ('freq', 'fC'), ('duty', 'dC'), ('feedback', 'bF'), ('time', 'tI'),
    ('reset', 'SI'), ('phase', 'PF'), ('pan', 'QC'), ('client', 'gI'), ('volume', 'VF'), ('pitch_bend', 'sF'), ('filter_freq', 'FC'), ('resonance', 'RF'),
    ('bp0', 'AL'), ('bp1', 'BL'), ('eg0_type', 'TI'), ('eg1_type', 'XI'), ('debug', 'DI'), ('chained_osc', 'cI'), ('mod_source', 'LI'), 
    ('eq', 'xL'), ('filter_type', 'GI'), ('ratio', 'IF'), ('latency_ms', 'NI'), ('algo_source', 'OL'), ('load_sample', 'zL'),
    ('algorithm', 'oI'), ('chorus', 'kL'), ('reverb', 'hL'), ('echo', 'ML'), ('patch', 'KI'), ('voices', 'rL'),
    ('external_channel', 'WI'), ('portamento', 'mI'), ('sequence', 'HL'), ('tempo', 'jF'),
    ('synth', 'iI'), ('pedal', 'ipI'), ('synth_flags', 'ifI'), ('num_voices', 'ivI'), ('to_synth', 'itI'), ('grab_midi_notes', 'imI'), # 'i' is prefix for some two-letter synth-level codes.
    ('preset', 'pI'), ('num_partials', 'pI'), # Note alaising.
    ('patch_string', 'uS'),  # patch_string MUST be last because we can't identify when it ends except by end-of-message.
]
_KW_PRIORITY = {k: i for i, (k, _) in enumerate(_KW_MAP_LIST)}   # Maps each key to its index within _KW_MAP_LIST.
_KW_MAP = dict(_KW_MAP_LIST)

_ARG_HANDLERS = {
    'I': str_of_int, 'F': trunc, 'S': str, 'L': parse_list_or_comma_string, 'C': parse_ctrl_coefs,
}

# Construct an AMY message
def message(**kwargs):
    #print("message:", kwargs)
    # Each keyword maps to two or three chars, first one or two are the wire protocol prefix, last is an arg type code
    # I=int, F=float, S=str, L=list, C=ctrl_coefs
    global show_warnings, _KW_MAP, _KW_PRIORITY, _ARG_HANDLERS
    if show_warnings:
        # Check for possible user confusions.
        if 'voices' in kwargs and 'preset' in kwargs and 'osc' not in kwargs:
            print('You specified \'voices\' and \'preset\' but not \'osc\' so your command will apply to the voice\'s osc 0.')
        if 'voices' in kwargs and 'synth' in kwargs and not ('patch' in kwargs or 'patch_string' in kwargs):
            print('You specified both \'synth\' and \'voices\' in a non-\'patch\'/\'patch_string\' message, but \'synth\' defines the voices.')
        if 'patch_string' in kwargs and not ('patch' in kwargs or 'synth' in kwargs or 'voices' in kwargs):
            print('\'patch_string\' is only valid with a \'patch\' or to define a new \'synth\' or \'voices\'.')
            # And yet we plow ahead...
        if 'patch_string' in kwargs:
            # Try to avoid mistakenly calling 'patch_string' when you meant 'patch'.
            if not isinstance(kwargs['patch_string'], str):
                raise ValueError('\'patch_string\' should be a wire command string, not \'' + str(kwargs['patch_string']) + '\'.')
        if 'num_partials' in kwargs:
            if 'preset' in kwargs:
                raise ValueError('You cannot use \'num_partials\' and \'preset\' in the same message.')
            if 'wave' not in kwargs or kwargs['wave'] != BYO_PARTIALS:
                raise ValueError('\'num_partials\' must be used with \'wave\'=BYO_PARTIALS.')

    if(insert_time is not None and 'time' not in kwargs):
        kwargs['time'] = insert_time()

    # Validity check all the passed args.
    prioritized_keys = []
    for key, arg in kwargs.items():
        if key not in _KW_MAP:
            raise ValueError('Unknown keyword ' + key)
        priority = _KW_PRIORITY[key]
        if arg is None:
            # Ignore time=None or sequence=None
            if key != 'time' and key != 'sequence':
                raise ValueError('No arg for key ' + key)
        else:
            prioritized_keys.append((priority, key))
    # Sort by priority, then strip the priority value.
    prioritized_keys = [e[1] for e in sorted(prioritized_keys)]
    # We process the passed args by testing each entry in the known keys in order, to make sure 'patch_string' is added last.
    m = ''
    for key in prioritized_keys:
        map_code = _KW_MAP[key]
        arg = kwargs[key]
        type_code = map_code[-1]
        wire_code = map_code[:-1]
        m += wire_code + _ARG_HANDLERS[type_code](arg)
    #print("message:", m)
    return m + 'Z'


def send_raw(m):
    # override_send is used by e.g. Tulip, to send messages in a different way than C or UDP
    global mess, log
    global override_send
    if(override_send is not None):
        override_send(m)
    else:
        _amy.send_wire(m)
    if(log): mess.append(m)

def log_patch():
    global mess, log
    # start recording a patch
    log = True
    mess = []

def retrieve_patch():
    global mess, log
    log = False
    s = "".join(mess)
    mess =[]
    return s


# Convenience function to store an in-memory AMY patch
# Call this, then call stop_store_patch(patch_number) when you're done
saved_override = None

def amy_do_nothing(message):
    return

def start_store_patch():
    global saved_override, override_send
    saved_override = override_send
    override_send = amy_do_nothing
    log_patch()

def stop_store_patch(patch):
    global saved_override, override_send
    override_send = saved_override

    m = "u"+str(patch)+retrieve_patch()
    send_raw(m)

# Send an AMY message to amy
def send(**kwargs):
    m = message(**kwargs)

    send_raw(m)


# Plots a time domain and spectra of audio
def show(data):
    import matplotlib.pyplot as plt
    import numpy as np
    fftsize = len(data)
    windowlength = fftsize
    window = np.hanning(windowlength)
    wavepart = data[:len(window)]
    logspecmag = 20 * np.log10(np.maximum(1e-10, 
        np.abs(np.fft.fft(wavepart * window)))[:(fftsize // 2 + 1)])
    freqs = AMY_SAMPLE_RATE * np.arange(len(logspecmag)) / fftsize
    plt.subplot(211)
    times = np.arange(len(wavepart)) / AMY_SAMPLE_RATE
    plt.plot(times, wavepart, '.')
    plt.subplot(212)
    plt.plot(freqs, logspecmag, '.-')
    plt.ylim(np.array([-100, 0]) + np.max(logspecmag))
    plt.show()

# Writes a WAV file of rendered data
def write(data, filename):
    import scipy.io.wavfile as wav
    import numpy as np
    wav.write(filename, int(AMY_SAMPLE_RATE), (32768.0 * data).astype(np.int16))

# Play a rendered sound out of default sounddevice
def play(samples):
    import sounddevice as sd
    sd.play(samples)

# Render AMY's internal buffer to a numpy array of floats
def render(seconds):
    import numpy as np
    # Output a npy array of samples
    frame_count = int((seconds*AMY_SAMPLE_RATE)/AMY_BLOCK_SIZE)
    frames = []
    for f in range(frame_count):
        frames.append( np.array(_amy.render_to_list())/32768.0 )
    return np.hstack(frames).reshape((-1, AMY_NCHANS))

def restart():
    _amy.stop()
    _amy.start()

def inject_midi(a, b, c, d=None):
    if d is None:
        _amy.inject_midi(a, b, c)
    else:
        _amy.inject_midi(a, b, c, d)
    
def unload_sample(patch=0):
    s= "%d,%d" % (patch, 0)
    send(load_sample=s)
    print("Patch %d unloaded from RAM" % (patch))


try:
    import base64
    def b64(b):
        return base64.b64encode(b)
except ImportError:
    import ubinascii
    def b64(b):
        return ubinascii.b2a_base64(b)[:-1]

def load_sample_bytes(b, stereo=False, preset=0, midinote=60, loopstart=0, loopend=0, sr=AMY_SAMPLE_RATE):
    # takes in a python bytes obj instead of filename
    from math import ceil
    if(stereo):
        # just choose first channel
        b = bytes([b[j] for i in range(0,len(b),4) for j in (i,i+1)])
    n_frames = len(b)/2
    s = "%d,%d,%d,%d,%d,%d" % (preset, n_frames, sr, midinote, loopstart, loopend)
    send(load_sample=s)
    last_f = 0
    for i in range(ceil(n_frames/94)):
        frames_bytes = b[last_f:last_f+188]
        message = b64(frames_bytes)
        send_raw(message.decode('ascii'))
        last_f = last_f + 188
    print("Loaded sample over wire protocol. Preset #%d. %d bytes, %d frames, midinote %d" % (preset, n_frames*2, n_frames, midinote))

def load_sample(wavfilename, preset=0, midinote=0, loopstart=0, loopend=0):
    from math import ceil
    from . import wave
    # tulip has ubinascii, normal has base64
    w = wave.open(wavfilename, 'r')
    
    if(loopstart==0): 
        if(hasattr(w,'_loopstart')): 
            loopstart = w._loopstart
    if(loopend==0): 
        if(hasattr(w,'_loopend')): 
            loopend = w._loopend
    if(midinote==0): 
        if(hasattr(w,'_midinote')): 
            midinote = w._midinote
        else:
            midinote=60

    # Tell AMY we're sending over a sample
    s = "%d,%d,%d,%d,%d,%d" % (preset, w.getnframes(), w.getframerate(), midinote, loopstart, loopend)
    send(load_sample=s)
    # Now generate the base64 encoded segments, 188 bytes / 94 frames at a time
    # why 188? that generates 252 bytes of base64 text. amy's max message size is currently 255.
    for i in range(ceil(w.getnframes()/94)):
        frames_bytes = w.readframes(94)
        if(w.getnchannels()==2):
            # de-interleave and just choose the first channel
            frames_bytes = bytes([frames_bytes[j] for i in range(0,len(frames_bytes),4) for j in (i,i+1)])
        message = b64(frames_bytes)
        send_raw(message.decode('ascii'))
    print("Loaded sample over wire protocol. Preset #%d. %d bytes, %d frames, midinote %d" % (preset, w.getnframes()*2, w.getnframes(), midinote))


"""
    Convenience functions
"""

def reset(osc=None, **kwargs):
    if(osc is not None):
        send(reset=osc, **kwargs)
    else:
        send(reset=RESET_ALL_OSCS, **kwargs) 





"""
    Chorus control
"""
def chorus(level=-1, max_delay=-1, freq=-1, amp=-1):
    chorus_level = ''
    chorus_delay = ''
    chorus_freq = ''
    chorus_depth = ''
    if (level >= 0):
        chorus_level = str(level)
    if (max_delay >= 0):
        chorus_delay = str(max_delay)
    if (freq >= 0):
        chorus_freq = str(freq)
    if (amp >= 0):
        chorus_depth = str(amp)
    chorus_arg = "%s,%s,%s,%s" % (chorus_level, chorus_delay, chorus_freq, chorus_depth)
    send(chorus=chorus_arg)

"""
    Reverb control
"""
def reverb(level=-1, liveness=-1, damping=-1, xover_hz=-1):
    reverb_level = ''
    reverb_liveness = ''
    reverb_damping = ''
    reverb_xover = ''
    if (level >= 0):
        reverb_level = str(level)
    if (liveness >= 0):
        reverb_liveness = str(liveness)
    if (damping >= 0):
        reverb_damping = str(damping)
    if (xover_hz >= 0):
        reverb_xover = str(xover_hz)
    reverb_arg = "%s,%s,%s,%s" % (reverb_level, reverb_liveness, reverb_damping, reverb_xover)
    send(reverb=reverb_arg)

"""
    Echo control
"""
def echo(level=None, delay_ms=None, max_delay_ms=None, feedback=None, filter_coef=None):
    echo_level = ''
    echo_delay_ms = ''
    echo_max_delay_ms = ''
    echo_feedback = ''
    echo_filter_coef = ''
    if level is not None:
        echo_level = str(level)
    if delay_ms is not None:
        echo_delay_ms = str(delay_ms)
    if max_delay_ms is not None:
        echo_max_delay_ms = str(max_delay_ms)
    if feedback is not None:
        echo_feedback = str(feedback)
    if filter_coef is not None:
        echo_filter_coef = str(filter_coef)
    echo_arg = '%s,%s,%s,%s,%s' % (echo_level, echo_delay_ms, echo_max_delay_ms, echo_feedback, echo_filter_coef)
    send(echo=echo_arg)
