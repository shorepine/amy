#!/usr/bin/env python3
"""Offline break slicer for the AMY sampler experiments.

Carves drum breaks into slices at onsets, snapped to zero crossings, and
writes the slices plus a JSON manifest that play_sampler.py consumes.

Slices tile the source exactly: slice k runs [onset_k, onset_k+1), so playing
them back-to-back (see play_sampler.py reconstruct) recreates the original.

Only numpy + stdlib. Usage:

    python3 slice_breaks.py                          # slices everything in
                                                     # ~/.local/share/tulipcc2/breaks -> ./slices
    python3 slice_breaks.py --in DIR --out DIR --max-seconds 30
"""
import argparse
import json
import os
import wave

import numpy as np


def read_wav_mono(path, max_seconds=None):
    w = wave.open(path, 'r')
    assert w.getsampwidth() == 2, f"{path}: 16-bit PCM only"
    sr = w.getframerate()
    n = w.getnframes()
    if max_seconds is not None:
        n = min(n, int(max_seconds * sr))
    data = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float32) / 32768.0
    if w.getnchannels() == 2:
        data = data.reshape(-1, 2).mean(axis=1)
    w.close()
    return data, sr


def write_wav_mono(path, data_f32, sr):
    w = wave.open(path, 'w')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(sr)
    w.writeframes((np.clip(data_f32, -1, 1) * 32767.0).astype('<i2').tobytes())
    w.close()


def spectral_flux(x, nfft=1024, hop=256):
    # Half-wave-rectified spectral flux onset envelope.
    nframes = max(0, (len(x) - nfft) // hop)
    win = np.hanning(nfft)
    prev = None
    flux = np.zeros(nframes)
    for i in range(nframes):
        mag = np.abs(np.fft.rfft(x[i * hop:i * hop + nfft] * win))
        if prev is not None:
            d = mag - prev
            flux[i] = np.sum(d[d > 0])
        prev = mag
    if flux.max() > 0:
        flux /= flux.max()
    return flux, hop


def pick_onsets(flux, hop, sr, min_sep_ms=60, sensitivity=1.5):
    # Peaks above a local-median adaptive threshold, with a refractory period.
    med_w = 17
    pad = np.pad(flux, med_w // 2, mode='edge')
    med = np.array([np.median(pad[i:i + med_w]) for i in range(len(flux))])
    thresh = med * sensitivity + 0.02
    min_sep = int(min_sep_ms / 1000 * sr / hop)
    onsets = []
    for i in range(1, len(flux) - 1):
        if flux[i] > thresh[i] and flux[i] >= flux[i - 1] and flux[i] >= flux[i + 1]:
            if not onsets or i - onsets[-1] >= min_sep:
                onsets.append(i)
    return [o * hop for o in onsets]


def snap_to_zero_crossing(x, pos, search=256):
    # Nearest sign change (or minimum |x|) within +/- search samples.
    lo = max(1, pos - search)
    hi = min(len(x) - 1, pos + search)
    if lo >= hi:
        return pos
    seg = x[lo:hi]
    sign_change = np.nonzero(np.signbit(seg[1:]) != np.signbit(seg[:-1]))[0]
    if len(sign_change):
        cands = sign_change + 1 + lo
        return int(cands[np.argmin(np.abs(cands - pos))])
    return int(lo + np.argmin(np.abs(seg)))


def estimate_bpm(flux, hop, sr, lo_bpm=60, hi_bpm=180):
    # Autocorrelation of the onset envelope over plausible beat periods.
    env = flux - flux.mean()
    ac = np.correlate(env, env, 'full')[len(env) - 1:]
    fps = sr / hop
    lo_lag = int(fps * 60 / hi_bpm)
    hi_lag = min(int(fps * 60 / lo_bpm), len(ac) - 1)
    if hi_lag <= lo_lag:
        return None
    lag = lo_lag + int(np.argmax(ac[lo_lag:hi_lag]))
    bpm = 60 * fps / lag
    # Fold octave errors into a 75-150 window.
    while bpm < 75:
        bpm *= 2
    while bpm > 150:
        bpm /= 2
    return round(bpm, 2)


def slice_break(path, outdir, max_seconds=None, max_slices=64):
    name = os.path.splitext(os.path.basename(path))[0]
    x, sr = read_wav_mono(path, max_seconds)
    flux, hop = spectral_flux(x)
    onsets = pick_onsets(flux, hop, sr)
    onsets = [snap_to_zero_crossing(x, o) for o in onsets]
    # Slices must tile the file: force a boundary at 0, dedupe, sort.
    bounds = sorted(set([0] + onsets + [len(x)]))
    if len(bounds) - 1 > max_slices:
        bounds = bounds[:max_slices] + [len(x)]
    slices = []
    os.makedirs(outdir, exist_ok=True)
    for k in range(len(bounds) - 1):
        s, e = bounds[k], bounds[k + 1]
        fn = f"{name}_{k:03d}.wav"
        write_wav_mono(os.path.join(outdir, fn), x[s:e], sr)
        slices.append({'file': fn, 'start': int(s), 'end': int(e)})
    bpm = estimate_bpm(flux, hop, sr)
    manifest = {
        'source': os.path.abspath(path),
        'name': name,
        'samplerate': sr,
        'n_frames': int(len(x)),
        'bpm_estimate': bpm,
        'slices': slices,
    }
    mpath = os.path.join(outdir, f"{name}.json")
    with open(mpath, 'w') as f:
        json.dump(manifest, f, indent=1)
    print(f"{name}: {len(slices)} slices, {len(x)/sr:.2f}s, bpm~{bpm} -> {mpath}")
    return mpath


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--in', dest='indir', default=os.path.expanduser('~/.local/share/tulipcc2/breaks'))
    ap.add_argument('--out', dest='outdir', default=os.path.join(os.path.dirname(__file__), 'slices'))
    ap.add_argument('--max-seconds', type=float, default=30.0, help='truncate long sources (0 = no limit)')
    ap.add_argument('--max-slices', type=int, default=64)
    args = ap.parse_args()
    max_seconds = args.max_seconds if args.max_seconds > 0 else None
    wavs = sorted(f for f in os.listdir(args.indir) if f.lower().endswith('.wav'))
    if not wavs:
        raise SystemExit(f"no wavs in {args.indir}")
    for f in wavs:
        slice_break(os.path.join(args.indir, f), args.outdir, max_seconds, args.max_slices)


if __name__ == '__main__':
    main()
