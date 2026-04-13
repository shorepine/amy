import amy, math, datetime

try:
  amy.live(playback_device_id=1)
  running_amyboard = False
except:
  running_amyboard = True

# Reserve 3 oscs x 3 "hands"
amy.send(synth=16, num_voices=1, oscs_per_voice=9)

def setup_triple(
    synth=16, base_osc=0, base_freq=200.0, freq_dev=5.0, period_sec=60, phase=0, vel=1
):
  # Osc+2 is the mod osc
  amy.send(synth=synth, osc=base_osc + 2, freq=1.0/period_sec,
           phase=phase - 0.25)  # Want it at lowest value when phase = now.
  # Osc+1 is the moving sine
  mid_freq = base_freq + freq_dev / 2
  mod_depth = math.log2(mid_freq / base_freq)  # -1 x this deviation brings down to base_freq
  amy.send(synth=synth, osc=base_osc + 1, mod_source=base_osc + 2,
           freq={'const': mid_freq, 'mod': mod_depth})
  # Osc+0 is the fixed sine
  amy.send(synth=synth, osc=base_osc, freq=base_freq)
  # Start the two sounding oscs
  amy.send(synth=synth, osc=base_osc, phase=0, vel=vel)
  amy.send(synth=synth, osc=base_osc + 1, phase=0, vel=vel)

def start_sine_clock(hour, minute, second, vel=0.1):
  # One-minute cycle
  setup_triple(base_osc=0, vel=vel, base_freq=200,
               period_sec=60, phase=second / 60)
  # One-hour cycle
  setup_triple(base_osc=3, vel=vel, base_freq=300,
               period_sec=60 * 60,
               phase=(60 * minute + second) / (60 * 60))
  # One day cycle
  setup_triple(base_osc=6, vel=vel, base_freq=450,
               period_sec=60 * 60 * 24,
               phase=(3600 * hour + 60 * minute + second) / (60 * 60 * 24))

try:
  import datetime
  vel = 0.1
  now = datetime.datetime.now()
  print(now)
  start_sine_clock(hour=now.hour, minute=now.minute, second=now.second, vel=vel)
except ImportError:
  pass

                   
