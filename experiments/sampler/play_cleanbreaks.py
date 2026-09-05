#!/usr/bin/env python3
"""DJ a folder of cleaned breaks at one fixed tempo.

Picks random breaks from ~/sounds/cleanbreaks (see clean_breaks.py), loads
each into an AMY memory preset, and plays them one after another: every
break is `fit=` time-stretched onto its own bar count at the session --bpm,
so a 90 bpm funk break and a 140 bpm jungle break lock to the same grid,
pitch untouched. Transitions land on bar boundaries via the AMY sequencer.

    python3 play_cleanbreaks.py --live               # play through speakers
    python3 play_cleanbreaks.py --bpm 140 --count 12 # render cleanbreaks_mix.wav
"""
import argparse
import json
import os
import random
import time

import numpy as np
import amy

SR = amy.AMY_SAMPLE_RATE
BLOCK = amy.AMY_BLOCK_SIZE
PPQ = 48
BAR_TICKS = 4 * PPQ


def render_blocks(n):
    return amy.render((n * BLOCK + BLOCK // 2) / SR)


def write_wav(path, data):
    import wave
    w = wave.open(path, 'w')
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((np.clip(data, -1, 1) * 32767.0).astype('<i2').tobytes())
    w.close()
    print("wrote", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dir', default=os.path.expanduser('~/sounds/cleanbreaks'))
    ap.add_argument('--bpm', type=float, default=96, help='session tempo every break is fit to')
    ap.add_argument('--count', type=int, default=8, help='how many random breaks to play')
    ap.add_argument('--cycles', type=int, default=1, help='times each break loops before the next')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--live', action='store_true', help='play through speakers instead of rendering')
    ap.add_argument('--out', default='cleanbreaks_mix.wav')
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.dir, 'manifest.json')))
    breaks = manifest['breaks']
    rng = random.Random(args.seed)
    picks = rng.sample(breaks, min(args.count, len(breaks)))

    if args.live:
        amy.live()
    else:
        amy.restart()
        _ = render_blocks(4)
    amy.send(volume=7)
    amy.send(tempo=args.bpm)

    print(f"loading {len(picks)} breaks...")
    for i, e in enumerate(picks):
        amy.load_sample(os.path.join(args.dir, e['file']), preset=100 + i, midinote=60)
    if not args.live:
        _ = render_blocks(4)

    # Line tick 0 up with the first note-on (loading above consumed time).
    amy.send(reset=amy.RESET_TIMEBASE)
    t = 0  # ticks
    print(f"\n  when  bars  native  break")
    for i, e in enumerate(picks):
        fit = e['bars'] * BAR_TICKS
        print(f"{t/BAR_TICKS:6.0f}  {e['bars']:4d}  {e['bpm']:6.1f}  {e['file']}")
        for c in range(args.cycles):
            kw = dict(osc=i + 1, wave=amy.PCM, preset=100 + i, fit=fit, vel=1)
            if t == 0:
                # A sequencer event at tick 0 never fires (tick 0 is already
                # past by the first render); play the opener directly.
                amy.send(**kw)
            else:
                amy.send(ticks=[t], **kw)
            t += fit
    us_per_tick = int(60000000.0 / (args.bpm * PPQ))  # matches sequencer.c
    total = int(t * us_per_tick / 1e6 * SR)

    if args.live:
        print(f"\nplaying {total / SR:.0f}s at {args.bpm:g} bpm... ctrl-c to stop early")
        try:
            time.sleep(total / SR + 0.5)
        except KeyboardInterrupt:
            pass
        finally:
            amy.send(reset=amy.RESET_SEQUENCER)
            for i in range(len(picks)):
                amy.send(osc=i + 1, vel=0)
        return
    out = render_blocks(int(np.ceil(total / BLOCK)))[:total]
    write_wav(args.out, out)


if __name__ == '__main__':
    main()
