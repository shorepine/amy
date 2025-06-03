#!/usr/bin/env python3
# examples.py
# sound examples

import amy
from time import sleep

def example_reset(start):
    amy.send(osc=0, reset=amy.RESET_ALL_OSCS, time=start)

def example_voice_alloc():
    # alloc 2 juno voices, then try to alloc a dx7 voice on voice 0
    amy.send(patch_number=1, voices=[0, 1])
    sleep(0.25)

    amy.send(patch_number=131, voices=[0])
    sleep(0.25)

    # play the same note on both
    amy.send(vel=1, note=60, voices=[0])
    sleep(2)

    amy.send(vel=1, note=60, voices=[1])
    sleep(2)

    # now try to alloc voice 0 with a juno, should use oscs 0-4 again
    amy.send(patch_number=2, voices=[0])
    sleep(0.25)

def example_voice_chord(start, patch):
    amy.send(time=start, patch_number=patch, voices=[0, 1, 2])
    start += 250

    amy.send(time=start, vel=0.5, voices=[0], note=50)
    start += 1000

    amy.send(time=start, vel=0.5, voices=[1], note=54)
    start += 1000

    amy.send(time=start, vel=0.5, voices=[2], note=56)
    start += 2000

    amy.send(time=start, vel=0, voices=[0, 1, 2])

def example_synth_chord(start, patch):
    # Like example_voice_chord, but use 'synth' to avoid having to keep track of voices
    amy.send(time=start, patch_number=patch, num_voices=3, synth=0)
    start += 250

    amy.send(time=start, vel=0.5, synth=0, note=50)
    start += 1000

    amy.send(time=start, vel=0.5, synth=0, note=54)
    start += 1000

    amy.send(time=start, vel=0.5, synth=0, note=56)
    start += 2000

    # Voices are referenced only by their note, so have to turn them off individually
    amy.send(time=start, vel=0, synth=0, note=50)
    amy.send(time=start, vel=0, synth=0, note=54)
    amy.send(time=start, vel=0, synth=0, note=56)

def example_sustain_pedal(start, patch):
    # Reset all oscillators first
    amy.send(time=start, reset=amy.RESET_ALL_OSCS)

    amy.send(time=start, synth=1, num_voices=4, patch_number=patch)

    start += 50
    amy.send(time=start, synth=1, note=76, vel=1.0)

    start += 50
    amy.send(time=start, synth=1, note=76, vel=0)

    start += 50
    amy.send(time=start, synth=1, pedal=127)

    start += 50
    amy.send(time=start, synth=1, note=63, vel=1.0)

    start += 50
    amy.send(time=start, synth=1, note=63, vel=0)

    start += 50
    amy.send(time=start, synth=1, note=67, vel=1.0)

    start += 50
    amy.send(time=start, synth=1, note=67, vel=0)

    start += 50
    amy.send(time=start, synth=1, note=72, vel=1.0)

    start += 50
    amy.send(time=start, synth=1, pedal=0)

    start += 50
    amy.send(time=start, synth=1, note=72, vel=0)

def example_patches():
    for i in range(256):
        amy.send(patch_number=i, voices=[0])
        print(f"sending patch {i}")
        sleep(0.25)

        amy.send(voices=[0], osc=0, note=50, vel=0.5)
        sleep(1)

        amy.send(voices=[0], vel=0)
        sleep(0.25)

        amy.resets()

def example_reverb():
    if amy.HAS_REVERB:
        amy.config_reverb(2, amy.REVERB_DEFAULT_LIVENESS, amy.REVERB_DEFAULT_DAMPING, amy.REVERB_DEFAULT_XOVER_HZ)

def example_chorus():
    if amy.HAS_CHORUS:
        amy.config_chorus(0.8, amy.CHORUS_DEFAULT_MAX_DELAY, amy.CHORUS_DEFAULT_LFO_FREQ, amy.CHORUS_DEFAULT_MOD_DEPTH)

def example_ks(start):
    amy.send(time=start, vel=1, wave=amy.KS, feedback=0.996, preset=15, osc=0, note=60)

def example_sine(start):
    amy.send(time=start, freq_coefs=[440], wave=amy.SINE, vel=1)

def example_multimbral_fm():
    notes = [60, 70, 64, 68, 72, 82]
    for i, note in enumerate(notes):
        amy.send(vel=0.5, patch_number=128 + i, note=note,
                pan_coefs=[i % 2], voices=[i])
        sleep(1)

def example_drums(start, loops):
    # bd, snare, hat, cow, hicow
    oscs = [0, 1, 2, 3, 4]
    presets = [1, 5, 0, 10, 10]

    # Reset all used oscs first
    for osc in oscs:
        amy.send(time=start, osc=osc, reset=osc)

    # Initialize oscillators
    for osc, preset in zip(oscs, presets):
        amy.send(time=start + 1, osc=osc, wave=amy.PCM, vel=0, preset=preset)

    # Update high cowbell
    amy.send(time=start + 1, osc=4, note=70)

    # osc 5: bass
    amy.send(time=start + 1, osc=5, wave=amy.SAW_DOWN,
            filter_freq_coefs=[650.0, 0, 2.0],  # [CONST, VEL, EG0]
            resonance=5.0,
            filter_type=amy.FILTER_LPF,
            bp0="0,1,500,0.2,25,0")

    bd = 1 << 0
    snare = 1 << 1
    hat = 1 << 2
    cow = 1 << 3
    hicow = 1 << 4

    pattern = [bd+hat, hat+hicow, bd+hat+snare, hat+cow, hat, bd+hat, snare+hat, hat]
    bassline = [50, 0, 0, 0, 50, 52, 51, 0]
    volume = 0.5

    time = start + 1
    while loops > 0:
        for i, (x, bass) in enumerate(zip(pattern, bassline)):
            time += 250

            if x & bd:
                amy.send(time=time, osc=0, vel=4.0 * volume)
            if x & snare:
                amy.send(time=time, osc=1, vel=1.5 * volume)
            if x & hat:
                amy.send(time=time, osc=2, vel=1.0 * volume)
            if x & cow:
                amy.send(time=time, osc=3, vel=1.0 * volume)
            if x & hicow:
                amy.send(time=time, osc=4, vel=1.0 * volume)

            if bass > 0:
                amy.send(time=time, osc=5, vel=0.5 * volume, note=bass - 12)
            else:
                amy.send(time=time, osc=5, vel=0)
        loops -= 1

def example_sequencer_drums(start):
    # Reset all oscillators
    amy.send(time=start, reset=amy.RESET_ALL_OSCS)

    amy.send(tempo=120.0)

    # Setup oscs for bd, snare, hat, cow, hicow
    oscs = [0, 1, 2, 3, 4]
    presets = [1, 5, 0, 10, 10]

    for osc, preset in zip(oscs, presets):
        amy.send(time=start + 1, osc=osc, wave=amy.PCM, preset=preset)

    # Update high cowbell
    amy.send(time=start + 1, osc=4, note=70)

    # Add patterns
    # Hi hat every 1/8th note
    amy.send(sequence=[0, 24, 0], osc=2, vel=2.0)

    # Bass drum every quarter note
    amy.send(sequence=[0, 96, 1], osc=0, vel=1.0)

    # Snare every quarter note, counterphase to BD
    amy.send(sequence=[24, 96, 2], osc=1, vel=1.0)

    # Cow once every other cycle
    amy.send(sequence=[0, 192, 3], osc=3, vel=1.0)

def example_sequencer_drums_synth(start):
    # Hi hat every 8 ticks
    amy.send(sequence=[0, 24, 0], synth=10, note=42, vel=1.0)  # Closed Hat

    # Bass drum every 32 ticks
    amy.send(sequence=[1, 96, 0], synth=10, note=35, vel=1.0)  # Std kick

    # Snare every 32 ticks, counterphase to BD
    amy.send(sequence=[2, 96, 48], synth=10, note=38, vel=1.0)  # Snare

    # Cow once every other cycle
    amy.send(sequence=[3, 192, 180], synth=10, note=56, vel=1.0)  # Cowbell

def example_fm(start):
    # Modulating oscillator (op 2)
    amy.send(time=start, osc=9, wave=amy.SINE, ratio=1.0,
            amp_coefs=[1.0, 0, 0])  # [CONST, VEL, EG0]

    # Output oscillator (op 1)
    amy.send(time=start, osc=8, wave=amy.SINE, ratio=0.2,
            amp_coefs=[1.0, 0, 1.0],  # [CONST, VEL, EG0]
            bp0="0,1,1000,0,0,0")

    # ALGO control oscillator
    amy.send(time=start, osc=7, wave=amy.ALGO, algorithm=1,
            algo_source=[0, 0, 0, 0, 9, 8])  # Only indices 4 and 5 matter here

    # Add a note on event
    amy.send(time=start + 100, osc=7, note=60, vel=1.0)

def example_patch_from_events():
    time = amy.sysclock()
    number = 1039

    amy.send(time=time, patch_number=number, reset=amy.RESET_PATCH)

    amy.send(time=time, patch_number=number, osc=0, wave=amy.SAW_DOWN,
            chained_osc=1, bp0="0,1,1000,0.1,200,0")

    amy.send(time=time, patch_number=number, osc=1, wave=amy.SINE,
            freq_coefs=[131.0], bp0="0,1,500,0,200,0")

    amy.send(time=time, synth=0, num_voices=4, patch_number=number)

    amy.send(time=time + 100, synth=0, note=60, vel=1.0)
    amy.send(time=time + 300, synth=0, note=64, vel=1.0)
    amy.send(time=time + 500, synth=0, note=67, vel=1.0)
    amy.send(time=time + 800, synth=0, vel=0.0) 