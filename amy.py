import time

BLOCK_SIZE = 256
AMY_SAMPLE_RATE = 44100.0
AMY_NCHANS = 2
AMY_OSCS = 120
MAX_QUEUE = 400
[SINE, PULSE, SAW_DOWN, SAW_UP, TRIANGLE, NOISE, KS, PCM, ALGO, PARTIAL, PARTIALS, OFF] = range(12)
TARGET_AMP, TARGET_DUTY, TARGET_FREQ, TARGET_FILTER_FREQ, TARGET_RESONANCE, TARGET_FEEDBACK, TARGET_LINEAR, TARGET_TRUE_EXPONENTIAL, TARGET_DX7_EXPONENTIAL, TARGET_PAN = (1, 2, 4, 8, 16, 32, 64, 128, 256, 512)
FILTER_NONE, FILTER_LPF, FILTER_BPF, FILTER_HPF, FILTER_LPF24 = range(5)
AMY_LATENCY_MS = 0
AMY_MAX_DRIFT_MS = 20000

override_send = None
mess = []
log = False

"""
    A bunch of useful presets
    TODO : move this to patches.c
"""

def preset(which,osc=0, **kwargs):
    # Reset the osc first
    reset(osc=osc)
    if(which==0): # simple note
        send(osc=osc, wave=SINE, bp0="10,1,250,0.7,500,0", bp0_target=TARGET_AMP, **kwargs)
    if(which==1): # filter bass
        send(osc=osc, filter_freq=2500, resonance=5, wave=SAW_DOWN, filter_type=FILTER_LPF, bp0="0,1,500,0.2,25,0", bp0_target=TARGET_AMP+TARGET_FILTER_FREQ, **kwargs)

    # TODO -- this is a good one to test the whistle on the bps... 
    if(which==2): # long sine pad to test ADSR
        send(osc=osc, wave=SINE, bp0="0,0,500,1,1000,0.25,750,0", bp0_target=TARGET_AMP, **kwargs)

    if(which==3): # amp LFO example
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=1.5, **kwargs)
        send(osc=osc, wave=PULSE, bp0="150,1,250,0.25,250,0", bp0_target=TARGET_AMP, mod_target=TARGET_AMP, mod_source=osc+1, **kwargs)
    if(which==4): # pitch LFO going up 
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=0.25, **kwargs)
        send(osc=osc, wave=PULSE, bp0="150,1,400,0,0,0", bp0_target=TARGET_AMP, mod_target=TARGET_FREQ, mod_source=osc+1, **kwargs)
    if(which==5): # bass drum
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=0.25, phase=0.5, **kwargs)
        send(osc=osc, wave=SINE, vel=0, bp0="500,0,0,0", bp0_target=TARGET_AMP, mod_target=TARGET_FREQ, mod_source=osc+1, **kwargs)
    if(which==6): # noise snare
        send(osc=osc, wave=NOISE, vel=0, bp0="250,0,0,0", bp0_target=TARGET_AMP, **kwargs)
    if(which==7): # closed hat
        send(osc=osc, wave=NOISE, vel=0, bp0="25,1,75,0,0,0", bp0_target=TARGET_AMP, **kwargs)
    if(which==8): # closed hat from PCM 
        send(osc=osc, wave=PCM, vel=0, patch=0, freq=0, **kwargs)
    if(which==9): # cowbell from PCM
        send(osc=osc, wave=PCM, vel=0, patch=10, freq=0, **kwargs)
    if(which==10): # high cowbell from PCM
        send(osc=osc, wave=PCM, vel=0, patch=10, note=70, **kwargs)
    if(which==11): # snare from PCM
        send(osc=osc, wave=PCM, vel=0, patch=5, freq=0, **kwargs)
    if(which==12): # FM bass 
        send(osc=osc, wave=ALGO, vel=0, patch=21, **kwargs)
    if(which==13): # Pcm bass drum
        send(osc=osc, wave=PCM, vel=0, patch=1, freq=0, **kwargs)
    if(which==14): # filtered algo 
        send(wave=ALGO, vel=0, patch=62,filter_freq=2000,resonance=2.5,filter_type=FILTER_LPF, bp0_target=TARGET_FILTER_FREQ,bp0="1,1,500,0,0,0")


# Removes trailing 0s and x.0000s from floating point numbers to trim wire message size
# Fun historical trivia: this function caused a bug so bad that Dan had to file a week-long PR for micropython
# https://github.com/micropython/micropython/pull/8905
def trunc(number):
    if(type(number)==float):
        return ('%.6f' % number).rstrip('0').rstrip('.')
    return str(number)

def trunc3(number):
    if(type(number)==float):
        return ('%.3f' % number).rstrip('0').rstrip('.')
    return str(number)


# Construct an AMY message
def message(osc=0, wave=None, patch=None, note=None, vel=None, amp=None, freq=None, duty=None, feedback=None, time=None, reset=None, phase=None, pan=None,
            client=None, retries=None, volume=None, filter_freq = None, resonance = None, bp0=None, bp1=None, bp0_target=None, bp1_target=None, mod_target=None,
            debug=None, chained_osc=None, mod_source=None, clone_osc=None, eq_l = None, eq_m = None, eq_h = None, filter_type= None, algorithm=None, ratio = None, latency_ms = None, algo_source=None,
            chorus_level=None, chorus_delay=None, chorus_freq=None, chorus_depth=None, reverb_level=None, reverb_liveness=None, reverb_damping=None, reverb_xover=None, load_patch=None, voices=None):

    m = ""
    if(time is not None): m = m + "t" + str(time)
    if(osc is not None): m = m + "v" + str(osc)
    if(wave is not None): m = m + "w" + str(wave)
    if(duty is not None): m = m + "d%s" % duty
    if(feedback is not None): m = m + "b" + trunc(feedback)
    if(freq is not None): m = m + "f%s" % freq
    if(note is not None): m = m + "n" + str(note)
    if(patch is not None): m = m + "p" + str(patch)
    if(phase is not None): m = m + "P" + trunc(phase)
    if(pan is not None): m = m + "Q%s" % pan
    if(client is not None): m = m + "c" + str(client)
    if(amp is not None): m = m + "a%s" % amp
    if(vel is not None): m = m + "l" + trunc(vel)
    if(volume is not None): m = m + "V" + trunc(volume)
    if(latency_ms is not None): m = m + "N" + str(latency_ms)
    if(resonance is not None): m = m + "R" + trunc(resonance)
    if(filter_freq is not None): m = m + "F%s" % filter_freq
    if(ratio is not None): m = m + "I" + trunc(ratio)
    if(algorithm is not None): m = m + "o" + str(algorithm)
    if(bp0 is not None): m = m +"A%s" % (bp0)
    if(bp1 is not None): m = m +"B%s" % (bp1)
    if(algo_source is not None): m = m +"O%s" % (algo_source)
    if(bp0_target is not None): m = m + "T" +str(bp0_target)
    if(bp1_target is not None): m = m + "W" +str(bp1_target)
    if(chained_osc is not None): m = m + "c" + str(chained_osc)
    if(clone_osc is not None): m = m + "C" + str(clone_osc)
    if(mod_target is not None): m = m + "g" + str(mod_target)
    if(mod_source is not None): m = m + "L" + str(mod_source)
    if(reset is not None): m = m + "S" + str(reset)
    if(debug is not None): m = m + "D" + str(debug)
    if(eq_l is not None): m = m + "x" + trunc(eq_l)
    if(eq_m is not None): m = m + "y" + trunc(eq_m)
    if(eq_h is not None): m = m + "z" + trunc(eq_h)
    if(filter_type is not None): m = m + "G" + str(filter_type)
    if(chorus_level is not None): m = m + "k" + str(chorus_level)
    if(chorus_delay is not None): m = m + "m" + str(chorus_delay)
    if(chorus_depth is not None): m = m + 'q' + trunc(chorus_depth)
    if(chorus_freq is not None): m =m + 'M' + trunc(chorus_freq)
    if(reverb_level is not None): m = m + "h" + str(reverb_level)
    if(reverb_liveness is not None): m = m + "H" + str(reverb_liveness)
    if(reverb_damping is not None): m = m + "j" + str(reverb_damping)
    if(reverb_xover is not None): m = m + "J" + str(reverb_xover)
    if(load_patch is not None): m = m + 'K' + str(load_patch)
    if(voices is not None): m = m + 'r' + str(voices)
    #print("message " + m)
    return m+'Z'


def send_raw(m):
    import libamy
    libamy.send(m)

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

# Send an AMY message to amy
def send(**kwargs):
    global override_send
    global mess, log
    m = message(**kwargs)
    if(log): mess.append(m)

    if(override_send is not None):
        override_send(**kwargs)
    else:
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

def live(audio_device=-1):
    import libamy
    libamy.live(audio_device)

# Stops live mode
def pause():
    stop()

def stop():
    import libamy
    libamy.pause()

def restart():
    import libamy
    libamy.restart()
    
"""
    Convenience functions
"""

def reset(osc=None, **kwargs):
    if(osc is not None):
        send(reset=osc, **kwargs)
    else:
        send(reset=10000, **kwargs) # reset > AMY_OSCS resets all oscs

def volume(volume, client = None):
    send(client=client, volume=volume)

def latency_ms(latency, client=None):
    send(client=client, latency_ms =latency)

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

"""
    Play up to AMY_OSCS patches at once
"""
def polyphony(max_voices=AMY_OSCS,**kwargs):
    note = 0
    oscs = []
    for i in range(int(max_voices/2)):
        oscs.append(int(i))
        oscs.append(int(i+(AMY_OSCS/2)))
    print(str(oscs))
    while(1):
        osc = oscs[note % max_voices]
        print("osc %d note %d filter %f " % (osc, 30+note, note*50))
        send(osc=osc, **kwargs, patch=note, filter_type=FILTER_NONE, filter_freq=note*50, note=30+(note), client = None, vel=1)
        time.sleep(0.5)
        note =(note + 1) % 64

def eq_test():
    reset()
    eqs = [ [0,0,0], [15,0,0], [0,0,15], [0,15,0],[-15,-15,15],[-15,-15,30],[-15,30,-15], [30,-15,-15] ]
    for eq in eqs:
        print("eq_l = %ddB eq_m = %ddB eq_h = %ddB" % (eq[0], eq[1], eq[2]))
        send(eq_l=eq[0], eq_m=eq[1], eq_h=eq[2])
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
    preset(13, osc=0, **kwargs) # sample bass drum
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
                send(osc=0, vel=4*volume, **kwargs)
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
    args = {}
    if (freq >= 0):
        args['chorus_freq'] = freq
    if (amp >= 0):
        args['chorus_depth'] = amp
    if (level >= 0):
        args['chorus_level'] = level
    if (max_delay >= 0):
        args['chorus_delay'] = max_delay
    send(**args)

"""
    Reverb control
"""
def reverb(level=-1, liveness=-1, damping=-1, xover_hz=-1):
    args = {}
    if (level >= 0):
        args['reverb_level'] = level
    if (liveness >= 0):
        args['reverb_liveness'] = liveness
    if (damping >= 0):
        args['reverb_damping'] = damping
    if (xover_hz >= 0):
        args['reverb_xover'] = xover_hz
    send(**args)






