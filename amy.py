import time

BLOCK_SIZE = 256
AMY_SAMPLE_RATE = 44100.0
AMY_NCHANS = 2
AMY_OSCS = 120
MAX_QUEUE = 400
SINE, PULSE, SAW_DOWN, SAW_UP, TRIANGLE, NOISE, KS, PCM, ALGO, PARTIAL, PARTIALS, BYO_PARTIALS, INTERP_PARTIALS, AUDIO_IN0, AUDIO_IN1, CUSTOM, OFF = range(17)
FILTER_NONE, FILTER_LPF, FILTER_BPF, FILTER_HPF, FILTER_LPF24 = range(5)
ENVELOPE_NORMAL, ENVELOPE_LINEAR, ENVELOPE_DX7, ENVELOPE_TRUE_EXPONENTIAL = range(4)
RESET_SEQUENCER, RESET_ALL_OSCS, RESET_TIMEBASE, RESET_AMY = (4096, 8192, 16384, 32768)
AMY_LATENCY_MS = 0
SEQUENCER_PPQ = 48

# If set, inserts func as time for every call to send(). Will not override an explicitly set time
insert_time = None

# If set, calls this instead of amy.send()
override_send = None

mess = []
log = False

show_warnings = True

"""
    A bunch of useful presets
    TODO : move this to patches.c
"""

def preset(which,osc=0, **kwargs):
    # Reset the osc first
    reset(osc=osc)
    if(which==0): # simple note. bp0 applied to amp by default (i.e., amp="0,0,1,1" for vel + bp0)
        send(osc=osc, wave=SINE, bp0="10,1,240,0.7,500,0", **kwargs)
    if(which==1): # filter bass.  bp0 is amplitude (default) and filter.
        send(osc=osc, filter_freq="100,0,0,5", resonance=5, wave=SAW_DOWN, filter_type=FILTER_LPF, bp0="0,1,1000,0,100,0", **kwargs)
    # TODO -- this is a good one to test the whistle on the bps... 
    if(which==2): # long sine pad to test ADSR
        send(osc=osc, wave=SINE, bp0="0,0,500,1,500,0.25,750,0", **kwargs)
    if(which==3): # amp LFO example
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, amp=0.50, freq=1.5, **kwargs)
        send(osc=osc, wave=PULSE, bp0="150,1,1850,0.25,250,0", amp="0,0,1,1,0,1", mod_source=osc+1, **kwargs)
    if(which==4): # pitch LFO going up 
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, amp=0.50, freq=0.25, **kwargs)
        send(osc=osc, wave=PULSE, bp0="150,1,250,0,0,0", freq="261.63,1,0,0,0,1", mod_source=osc+1, **kwargs)
    if(which==5): # bass drum
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, amp=0.50, freq=0.25, phase=0.5, **kwargs)
        send(osc=osc, wave=SINE, bp0="0,1,500,0,0,0", freq="261.63,1,0,0,0,1", mod_source=osc+1, **kwargs)
    if(which==6): # noise snare
        send(osc=osc, wave=NOISE, bp0="0,1,250,0,0,0",  **kwargs)
    if(which==7): # closed hat
        send(osc=osc, wave=NOISE, bp0="25,1,50,0,0,0", **kwargs)
    if(which==8): # closed hat from PCM 
        send(osc=osc, wave=PCM, patch=0, **kwargs)
    if(which==9): # cowbell from PCM
        send(osc=osc, wave=PCM, patch=10, **kwargs)
    if(which==10): # high cowbell from PCM
        send(osc=osc, wave=PCM, patch=10, note=70, **kwargs)
    if(which==11): # snare from PCM
        send(osc=osc, wave=PCM, patch=5, freq=0, **kwargs)
    if(which==12): # FM bass 
        send(osc=osc, wave=ALGO, patch=21, **kwargs)
    if(which==13): # Pcm bass drum
        send(osc=osc, wave=PCM, patch=1, freq=0, **kwargs)
    if(which==14): # filtered algo 
        send(wave=ALGO, patch=62, filter_freq="125,0,0,4", resonance=2.5, filter_type=FILTER_LPF, bp0="1,1,499,0,0,0")


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


# Construct an AMY message
def message(**kwargs):
    # Each keyword maps to two chars, first is the wire protocol prefix, second is an arg type code
    # I=int, F=float, S=str, L=list, C=ctrl_coefs
    kw_map = {'osc': 'vI', 'wave': 'wI', 'note': 'nF', 'vel': 'lF', 'amp': 'aC', 'freq': 'fC', 'duty': 'dC', 'feedback': 'bF', 'time': 'tI',
              'reset': 'SI', 'phase': 'PF', 'pan': 'QC', 'client': 'gI', 'volume': 'VF', 'pitch_bend': 'sF', 'filter_freq': 'FC', 'resonance': 'RF',
              'bp0': 'AL', 'bp1': 'BL', 'eg0_type': 'TI', 'eg1_type': 'XI', 'debug': 'DI', 'chained_osc': 'cI', 'mod_source': 'LI', 
              'eq': 'xL', 'filter_type': 'GI', 'algorithm': 'oI', 'ratio': 'IF', 'latency_ms': 'NI', 'algo_source': 'OL', 'load_sample': 'zL',
              'chorus': 'kL', 'reverb': 'hL', 'echo': 'ML', 'load_patch': 'KI', 'store_patch': 'uS', 'voices': 'rL',
              'external_channel': 'WI', 'portamento': 'mI', 'sequence': 'HL', 'tempo': 'jF',
              'patch': 'pI', 'num_partials': 'pI', # Note alaising.
              }
    arg_handlers = {
        'I': str, 'F': trunc, 'S': str, 'L': str, 'C': parse_ctrl_coefs,
    }
    unrecognized_keywords = set(kwargs).difference(set(kw_map))
    if unrecognized_keywords:
        raise ValueError('Unrecognized keyword(s): %s' % unrecognized_keywords)
    if show_warnings:
        # Check for possible user confusions.
        if 'voices' in kwargs and 'patch' in kwargs and 'osc' not in kwargs:
            print('You specified \'voices\' and \'patch\' but not \'osc\' so your command will apply to the voice\'s osc 0.')
        if 'store_patch' in kwargs and len(kwargs) > 1:
            print('\'store_patch\' should be the only arg in a message.')
            # And yet we plow ahead...
        if 'num_partials' in kwargs:
            if 'patch' in kwargs:
                raise ValueError('You cannot use \'num_partials\' and \'patch\' in the same message.')
            if 'wave' not in kwargs or kwargs['wave'] != BYO_PARTIALS:
                raise ValueError('\'num_partials\' must be used with \'wave\'=BYO_PARTIALS.')

    if(insert_time is not None and 'time' not in kwargs):
        kwargs['time'] = insert_time()

    m = ""
    for key, arg in kwargs.items():
        if arg is None:
            # Just ignore time or sequence=None
            if key != 'time' and key != 'sequence':
                raise ValueError('No arg for key ' + key)
        else:
            wire_code, type_code = kw_map[key]
            m += wire_code + arg_handlers[type_code](arg)
    #print("message:", m)
    return m + 'Z'


def send_raw(m):
    # override_send is used by e.g. Tulip, to send messages in a different way than libamy or UDP
    global mess, log
    global override_send
    if(override_send is not None):
        override_send(m)
    else:
        import libamy
        libamy.send(m)
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

def stop_store_patch(patch_number):
    global saved_override, override_send
    override_send = saved_override

    m = "u"+str(patch_number)+retrieve_patch()
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
    import libamy
    # Output a npy array of samples
    frame_count = int((seconds*AMY_SAMPLE_RATE)/BLOCK_SIZE)
    frames = []
    for f in range(frame_count):
        frames.append( np.array(libamy.render())/32768.0 )
    return np.hstack(frames).reshape((-1, AMY_NCHANS))

# Starts a live mode, with audio playing out default sounddevice
def start():
    live()

def live(audio_playback_device=-1, audio_capture_device=-1):
    import libamy
    libamy.live(audio_playback_device, audio_capture_device)

# Stops live mode
def pause():
    stop()

def stop():
    import libamy
    libamy.pause()

def restart():
    import libamy
    libamy.restart()

def unload_sample(patch=0):
    s= "%d,%d" % (patch, 0)
    send(load_sample=s)
    print("Patch %d unloaded from RAM" % (patch))

def load_sample(wavfilename, patch=0, midinote=0, loopstart=0, loopend=0):
    from math import ceil
    import amy_wave # our version of a wave file reader that looks for sampler metadata
    # tulip has ubinascii, normal has base64
    try:
        import base64
        def b64(b):
            return base64.b64encode(b)
    except ImportError:
        import ubinascii
        def b64(b):
            return ubinascii.b2a_base64(b)[:-1]

    w = amy_wave.open(wavfilename, 'r')
    
    if(w.getnchannels()>1):
        # de-interleave and just choose the first channel
        f = bytes([f[j] for i in range(0,len(f),4) for j in (i,i+1)])
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
    s = "%d,%d,%d,%d,%d,%d" % (patch, w.getnframes(), w.getframerate(), midinote, loopstart, loopend)
    send(load_sample=s)
    # Now generate the base64 encoded segments, 188 bytes / 94 frames at a time
    # why 188? that generates 252 bytes of base64 text. amy's max message size is currently 255.
    for i in range(ceil(w.getnframes()/94)):
        message = b64(w.readframes(94))
        send_raw(message.decode('ascii'))
    print("Loaded sample over wire protocol. Patch #%d. %d bytes, %d frames, midinote %d" % (patch, w.getnframes()*2, w.getnframes(), midinote))


"""
    Convenience functions
"""

def reset(osc=None, **kwargs):
    if(osc is not None):
        send(reset=osc, **kwargs)
    else:
        send(reset=RESET_ALL_OSCS, **kwargs) 


"""
    Run a scale through all the synth's sounds
"""
def test():
    while True:
        for wave in [SINE, SAW_DOWN, PULSE, TRIANGLE, NOISE]:
            for i in range(12):
                send(osc=0, wave=wave, note=40+i, patch=i, vel=1)
                time.sleep(0.5)


"""
    Play all of the patches 
"""
def play_patches(wait=1, patch_total = 256, **kwargs):
    import random
    patch_count = 0
    while True:
        patch = random.randint(0,256) #patch_count % patch_total
        print("Sending patch %d" %(patch))
        send(osc=0, load_patch=patch)
        time.sleep(wait/4.0)            
        patch_count = patch_count + 1
        send(osc=0, note=50, vel=1, **kwargs)
        time.sleep(wait)
        send(osc=0, vel=0)
        reset()
        time.sleep(wait/4.0)


def eq_test():
    reset()
    eqs = [ [0,0,0], [15,0,0], [0,0,15], [0,15,0],[-15,-15,15],[-15,-15,30],[-15,30,-15], [30,-15,-15] ]
    for eq in eqs:
        print("eq_l = %f dB, eq_m = %f dB, eq_h = %f dB" % (eq[0], eq[1], eq[2]))
        send(eq="%.2f,%.2f,%.2f" % (eq[0], eq[1], eq[2]))
        drums(loops=2)
        time.sleep(1)
        reset()
        time.sleep(0.250)

"""
    Sweep the filter
"""
def sweep(speed=0.100, res=0.5, loops = -1):
    end = 2000
    cur = 0
    while(loops != 0):
        for i in [0, 1, 4, 5, 1, 3, 4, 5]:
            cur = (cur + 100) % end
            send(osc=0,filter_type=FILTER_LPF, filter_freq=cur+250, resonance=res, wave=PULSE, note=50+i, duty=0.50, vel=1)
            send(osc=1,filter_type=FILTER_LPF, filter_freq=cur+500, resonance=res, wave=PULSE, note=50+12+i, duty=0.25, vel=1)
            send(osc=2,filter_type=FILTER_LPF, filter_freq=cur, resonance=res, wave=PULSE, note=50+6+i, duty=0.90, vel=1)
            time.sleep(speed)

"""
    An example drum machine using osc+PCM presets
"""
def drums(bpm=120, loops=-1, volume=0.2, **kwargs):
    preset(5, osc=0, **kwargs) # sample bass drum
    preset(8, osc=3, **kwargs) # sample hat
    preset(9, osc=4, pan=1, **kwargs) # sample cow
    preset(10, osc=5, pan=0, **kwargs) # sample hi cow
    preset(11, osc=2, **kwargs) # sample snare
    preset(1, osc=7, **kwargs) # filter bass
    [bass, snare, hat, cow, hicow, silent] = [1, 2, 4, 8, 16, 32]
    pattern = [bass+hat, hat+hicow, bass+hat+snare, hat+cow, hat, hat+bass, snare+hat, hat]
    bassline = [50, 0, 0, 0, 50, 52, 51, 0]
    while (loops != 0):
        loops = loops - 1
        for i,x in enumerate(pattern):
            if(x & bass): 
                send(osc=0, vel=6*volume, note=44, **kwargs)
            if(x & snare):
                send(osc=2, vel=1.5*volume)
            if(x & hat): 
                send(osc=3, vel=1*volume)
            if(x & cow): 
                send(osc=4, vel=1*volume)
            if(x & hicow): 
                send(osc=5, vel=1*volume)
            if(bassline[i]>0):
                send(osc=7, vel=0.5*volume, note=bassline[i]-12, **kwargs)
            else:
                send(vel=0, osc=7, **kwargs)
            time.sleep(1.0/(bpm*2/60))


"""
    C-major chord
"""
def c_major(octave=2,wave=SINE, **kwargs):
    send(osc=0, freq=220.5*octave, wave=wave, vel=1, **kwargs)
    send(osc=1, freq=138.5*octave, wave=wave, vel=1, **kwargs)
    send(osc=2, freq=164.5*octave, wave=wave, vel=1, **kwargs)

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
