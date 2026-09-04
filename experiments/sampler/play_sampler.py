#!/usr/bin/env python3
"""Play sliced breaks through AMY's new sampler features.

Demos (see slice_breaks.py first -- it writes slices/ and manifests):

  reconstruct NAME      Recreate the original break by scheduling every slice
                        back-to-back with sample-accurate `sample_offset`
                        timing, offline. Verifies the result against the
                        source and writes NAME_recon.wav. Try --no-offset to
                        hear (and measure) what block-quantized scheduling
                        does instead.

  hits NAME             Individual slices one after another on a 16th-note
                        grid at --bpm (in order, or --shuffle to chop). Each
                        hit lands at its exact grid sample via sample_offset;
                        the grid spacing is not a multiple of the 256-sample
                        block, so every hit needs a different sub-block
                        offset. Writes NAME_hits.wav or plays --live.

  loops NAME [NAME..]   90s/00s style: each break is trimmed to whole beats,
                        loaded whole, and looped with `fit=` so it plays at
                        the session --bpm with pitch-invariant time stretch,
                        all locked together by the AMY sequencer. Starts
                        immediately and plays --loops cycles of the longest
                        break. --live or offline render to loops.wav.

  pitch NAME            fit=0: a semitone ladder on one slice -- pitch moves,
                        duration doesn't. Writes NAME_pitch.wav or --live.

All offline renders are deterministic: AMY events fire on 256-sample block
boundaries, so the conductor here sends each event in exactly the block it
belongs to and carries the sub-block remainder in `sample_offset`.
"""
import argparse
import json
import os
import time
import wave

import numpy as np
import amy

SR = amy.AMY_SAMPLE_RATE
BLOCK = amy.AMY_BLOCK_SIZE
PPQ = 48  # AMY_SEQUENCER_PPQ


def render_blocks(n):
    # amy.render() floors seconds*SR/BLOCK; pad half a block so we get exactly n.
    return amy.render((n * BLOCK + BLOCK // 2) / SR)


def read_wav_mono(path):
    w = wave.open(path, 'r')
    data = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float32) / 32768.0
    if w.getnchannels() == 2:
        data = data.reshape(-1, 2).mean(axis=1)
    sr = w.getframerate()
    w.close()
    return data, sr


def write_wav(path, data, nchans=2):
    w = wave.open(path, 'w')
    w.setnchannels(nchans)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((np.clip(data, -1, 1) * 32767.0).astype('<i2').tobytes())
    w.close()
    print("wrote", path)


def load_manifest(slices_dir, name):
    with open(os.path.join(slices_dir, name + '.json')) as f:
        return json.load(f)


_next_preset = [100]

def load_slice_presets(slices_dir, manifest):
    presets = []
    for s in manifest['slices']:
        p = _next_preset[0]; _next_preset[0] += 1
        amy.load_sample(os.path.join(slices_dir, s['file']), preset=p, midinote=60)
        presets.append(p)
    return presets


def load_whole_break(manifest, beats=None):
    # Load the source (trimmed to whole beats at its estimated bpm) as one preset.
    x, sr = read_wav_mono(manifest['source'])
    x = x[:manifest['n_frames']]
    bpm = manifest['bpm_estimate'] or 120.0
    if beats is None:
        beats = max(1, round(len(x) / sr * bpm / 60))
    n = min(len(x), int(round(beats * 60 / bpm * sr)))
    p = _next_preset[0]; _next_preset[0] += 1
    amy.load_sample_bytes((x[:n] * 32767.0).astype('<i2').tobytes(), preset=p, midinote=60, sr=sr)
    return p, beats, n / sr


class Conductor:
    """Offline scheduler: fires each event in its exact block, with
    sample_offset carrying the sub-block remainder."""
    def __init__(self):
        self.events = []  # (abs_sample, kwargs)

    def at(self, abs_sample, **kwargs):
        self.events.append((int(abs_sample), kwargs))

    def render(self, total_samples):
        by_block = {}
        for s, kw in self.events:
            by_block.setdefault(s // BLOCK, []).append((s % BLOCK, kw))
        nblocks = int(np.ceil(total_samples / BLOCK)) + 2
        out = []
        for b in range(nblocks):
            for off, kw in by_block.get(b, []):
                amy.send(sample_offset=off, **kw)
            out.append(render_blocks(1))
        return np.vstack(out)


def setup(live, volume=6):
    if live:
        amy.live()
    else:
        amy.restart()
        _ = render_blocks(4)
    # The default mixdown gain leaves a lone PCM voice ~23 dB down; bring it
    # up so offline renders use the int16 range (and sound right).
    amy.send(volume=volume)


def us_per_tick(bpm):
    return int(60000000 / (bpm * PPQ))


def tick_samples(bpm):
    return us_per_tick(bpm) * SR / 1e6


# ---------------------------------------------------------------- reconstruct

def demo_reconstruct(args):
    m = load_manifest(args.slices, args.name)
    setup(live=False, volume=8)
    presets = load_slice_presets(args.slices, m)
    _ = render_blocks(2)
    cond = Conductor()
    for s, p in zip(m['slices'], presets):
        kw = dict(osc=(presets.index(p) % 24) + 1, wave=amy.PCM, preset=p, vel=1)
        if args.no_offset:
            cond.at((s['start'] // BLOCK) * BLOCK, **kw)   # block-quantized: what you get today
        else:
            cond.at(s['start'], **kw)
    out = cond.render(m['n_frames'] + 4 * BLOCK)
    mono = out[:, 0]

    src, sr = read_wav_mono(m['source'])
    src = src[:m['n_frames']]
    n = min(len(src), len(mono))
    # AMY applies its own gain (pan law etc.); estimate it by regression.
    g = np.dot(mono[:n], src[:n]) / np.dot(src[:n], src[:n])
    resid = mono[:n] - g * src[:n]
    err_db = 20 * np.log10(max(np.max(np.abs(resid)), 1e-10) / np.max(np.abs(g * src[:n])))
    tag = "block-quantized (no sample_offset)" if args.no_offset else "sample-accurate"
    print(f"{args.name}: {len(presets)} slices, {tag}: peak residual vs source = {err_db:.1f} dB")
    out_path = args.out or f"{args.name}_recon{'_nooffset' if args.no_offset else ''}.wav"
    write_wav(out_path, out)


# ----------------------------------------------------------------------- hits

def demo_hits(args):
    m = load_manifest(args.slices, args.name)
    setup(args.live)
    presets = load_slice_presets(args.slices, m)
    order = list(range(len(presets)))
    if args.shuffle:
        rng = np.random.default_rng(args.seed)
        order = list(rng.permutation(order))
    step = 60 / args.bpm / 4  # 16th note
    step_samples = step * SR

    if args.live:
        # Quantized to sequencer ticks live (PPQ/4 ticks per 16th).
        amy.send(tempo=args.bpm)
        for i, k in enumerate(order):
            amy.send(ticks=[int(i * PPQ / 4)], osc=(i % 24) + 1,
                     wave=amy.PCM, preset=presets[k], vel=1)
        time.sleep(len(order) * step + 2)
        return
    cond = Conductor()
    src_to_out = SR / m['samplerate']
    last_end = 0
    for i, k in enumerate(order):
        s = int(round(i * step_samples))
        cond.at(s, osc=(i % 24) + 1, wave=amy.PCM, preset=presets[k], vel=1)
        sl = m['slices'][k]
        last_end = max(last_end, s + (sl['end'] - sl['start']) * src_to_out)
        if args.verbose:
            print(f"hit {i:3d} slice {k:3d} at sample {s} = block {s//BLOCK} + offset {s%BLOCK}")
    # Render until the longest-ringing hit actually finishes, plus a breath.
    total = int(last_end + 0.15 * SR)
    out = cond.render(total)
    write_wav(args.out or f"{args.name}_hits{'_shuffled' if args.shuffle else ''}.wav", out)


# ---------------------------------------------------------------------- loops

def demo_loops(args):
    setup(args.live)
    amy.send(tempo=args.bpm)
    loops = []
    for i, name in enumerate(args.names):
        m = load_manifest(args.slices, name)
        p, beats, dur = load_whole_break(m, beats=args.beats)
        fit = beats * PPQ  # stretch the whole break onto this many ticks
        loops.append((name, p, beats, dur, fit))
        print(f"{name}: {dur:.2f}s ({m['bpm_estimate']} bpm est) -> {beats} beats at {args.bpm} bpm, fit={fit} ticks")
    if not args.live:
        _ = render_blocks(4)

    # Re-zero the clock and sequencer tick count so tick 0 lines up with the
    # note-ons we're about to send (loading above consumed blocks/ticks).
    amy.send(reset=amy.RESET_TIMEBASE)
    for i, (name, p, beats, dur, fit) in enumerate(loops):
        kw = dict(osc=i + 1, wave=amy.PCM, preset=p, fit=fit, vel=1,
                  pan=0.2 + 0.6 * (i / max(1, len(loops) - 1)))
        # First cycle starts now: a periodic sequencer entry with offset 0
        # first fires at tick `period`, not tick 0, so send cycle 1 directly...
        amy.send(**kw)
        # ...and let the sequencer re-trigger every `fit` ticks after that.
        if args.loops > 1:
            amy.send(ticks=[0, fit], **kw)
    # "N loops" = N cycles of the longest break.
    total_ticks = max(l[4] for l in loops) * args.loops
    total = int(total_ticks * tick_samples(args.bpm))
    if args.live:
        print(f"looping {args.loops}x ({total * 1.0 / SR:.1f}s)... ctrl-c to stop early")
        try:
            time.sleep(total / SR + 0.5)
        except KeyboardInterrupt:
            pass
        finally:
            amy.send(reset=amy.RESET_SEQUENCER)
            for i in range(len(loops)):
                amy.send(osc=i + 1, vel=0)
        return
    out = render_blocks(int(np.ceil(total / BLOCK)))
    out = out[:total]  # drop the sliver of cycle N+1 the sequencer fires at the end
    write_wav(args.out or "loops.wav", out)


# ---------------------------------------------------------------------- pitch

def demo_pitch(args):
    m = load_manifest(args.slices, args.name)
    setup(args.live)
    presets = load_slice_presets(args.slices, m)
    k = args.slice if args.slice is not None else 0
    p = presets[k]
    sl = m['slices'][k]
    dur = (sl['end'] - sl['start']) / m['samplerate']
    notes = [60, 62, 63, 65, 67, 63, 60, 55, 60]  # minor-ish ladder
    gap = max(dur * 1.05, 0.3)
    if args.live:
        for i, n in enumerate(notes):
            amy.send(osc=1 + (i % 4), wave=amy.PCM, preset=p, note=n, fit=0, vel=1)
            time.sleep(gap)
        return
    cond = Conductor()
    for i, n in enumerate(notes):
        cond.at(int(i * gap * SR), osc=1 + (i % 4), wave=amy.PCM, preset=p, note=n, fit=0, vel=1)
    out = cond.render(int(len(notes) * gap * SR + SR))
    write_wav(args.out or f"{args.name}_pitch.wav", out)
    print(f"slice {k} ({dur*1000:.0f} ms) at {len(notes)} pitches, duration constant (fit=0)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--slices', default=os.path.join(os.path.dirname(__file__), 'slices'))
    ap.add_argument('--out', default=None, help='output wav (offline modes)')
    ap.add_argument('--live', action='store_true', help='play through speakers instead of rendering')
    sub = ap.add_subparsers(dest='mode', required=True)

    r = sub.add_parser('reconstruct'); r.add_argument('name')
    r.add_argument('--no-offset', action='store_true', help='block-quantize starts (the old behavior) for comparison')

    h = sub.add_parser('hits'); h.add_argument('name')
    h.add_argument('--bpm', type=float, default=120)
    h.add_argument('--shuffle', action='store_true')
    h.add_argument('--seed', type=int, default=7)
    h.add_argument('--verbose', action='store_true')

    l = sub.add_parser('loops'); l.add_argument('names', nargs='+')
    l.add_argument('--bpm', type=float, default=120)
    l.add_argument('--loops', type=int, default=2, help='how many cycles of the longest break to play/render')
    l.add_argument('--beats', type=int, default=None, help='force beats per break (default: estimate)')

    pz = sub.add_parser('pitch'); pz.add_argument('name')
    pz.add_argument('--slice', type=int, default=None)

    args = ap.parse_args()
    {'reconstruct': demo_reconstruct, 'hits': demo_hits,
     'loops': demo_loops, 'pitch': demo_pitch}[args.mode](args)


if __name__ == '__main__':
    main()
