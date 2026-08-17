#!/usr/bin/env python3
"""Normalize a folder of break-beat recordings into uniform, loopable WAVs.

Reads everything soundfile can decode from --in (WAV/AIFF, extensionless
AIFF, Sun .snd, MPC2000 .SND, ...), keeps the "break-like" ones (audible,
>= ~2s, with a detectable steady tempo), and writes each to --out as:

    mono, 16-bit, 44100 Hz WAV, peak-normalized,
    trimmed to a whole number of bars (2-10 s), both ends snapped to zero
    crossings, loop length refined by waveform autocorrelation so the seam
    actually loops.

Long files (full songs) get the most rhythmically dense whole-bar window --
usually the break. A manifest.json (file, bpm, bars, frames, source) is
written alongside for players like play_cleanbreaks.py.

    python3 clean_breaks.py            # ~/sounds/breaks -> ~/sounds/cleanbreaks
"""
import argparse
import json
import os
import re

import numpy as np
import soundfile as sf

TARGET_SR = 44100
MIN_SECONDS = 1.8
MAX_SECONDS = 10.0


def load_mono(path):
    x, sr = sf.read(path, dtype='float32', always_2d=True)
    x = x.mean(axis=1)
    if sr != TARGET_SR:
        n = int(round(len(x) * TARGET_SR / sr))
        x = np.interp(np.linspace(0, len(x), n, endpoint=False), np.arange(len(x)), x)
    return x.astype(np.float32)


def spectral_flux(x, nfft=1024, hop=256):
    nframes = max(0, (len(x) - nfft) // hop)
    win = np.hanning(nfft)
    prev = None
    flux = np.zeros(nframes, dtype=np.float32)
    for i in range(nframes):
        mag = np.abs(np.fft.rfft(x[i * hop:i * hop + nfft] * win))
        if prev is not None:
            d = mag - prev
            flux[i] = np.sum(d[d > 0])
        prev = mag
    if flux.max() > 0:
        flux /= flux.max()
    return flux, hop


def estimate_bpm(flux, hop, lo_bpm=75, hi_bpm=150):
    # Autocorrelation of the onset envelope; returns (bpm, confidence).
    env = flux - flux.mean()
    if len(env) < 32 or env.std() == 0:
        return None, 0.0
    ac = np.correlate(env, env, 'full')[len(env) - 1:]
    fps = TARGET_SR / hop
    lo_lag = max(1, int(fps * 60 / hi_bpm))
    hi_lag = min(int(fps * 60 / lo_bpm), len(ac) - 1)
    if hi_lag <= lo_lag:
        return None, 0.0
    seg = ac[lo_lag:hi_lag]
    lag = lo_lag + int(np.argmax(seg))
    conf = float(ac[lag] / ac[0]) if ac[0] > 0 else 0.0
    bpm = 60 * fps / lag
    while bpm < lo_bpm:
        bpm *= 2
    while bpm > hi_bpm:
        bpm /= 2
    return round(bpm, 2), conf


def snap_zero(x, pos, search=256):
    lo, hi = max(1, pos - search), min(len(x) - 1, pos + search)
    if lo >= hi:
        return pos
    seg = x[lo:hi]
    sc = np.nonzero(np.signbit(seg[1:]) != np.signbit(seg[:-1]))[0]
    if len(sc):
        cands = sc + 1 + lo
        return int(cands[np.argmin(np.abs(cands - pos))])
    return int(lo + np.argmin(np.abs(seg)))


def refine_loop_len(x, start, nominal, slack=0.06, probe=4096):
    # The loop seam wants x[start:start+p] ~ x[start+L:start+L+p]; search L
    # around nominal for the best normalized correlation.
    lo = int(nominal * (1 - slack))
    hi = min(int(nominal * (1 + slack)), len(x) - start - probe)
    if hi <= lo or start + lo < probe:
        return nominal
    a = x[start:start + probe]
    na = np.linalg.norm(a) + 1e-9
    best, best_l = -2.0, nominal
    for L in range(lo, hi, 16):
        b = x[start + L:start + L + probe]
        c = float(np.dot(a, b) / (na * (np.linalg.norm(b) + 1e-9)))
        if c > best:
            best, best_l = c, L
    return best_l


def best_window(flux, hop, win_frames):
    # Rhythmic density score: total onset strength over a sliding window.
    if len(flux) <= win_frames:
        return 0
    csum = np.cumsum(flux)
    scores = csum[win_frames:] - csum[:-win_frames]
    return int(np.argmax(scores)) * hop


def clean_one(path, outdir):
    x = load_mono(path)
    if len(x) < MIN_SECONDS * TARGET_SR:
        return None, "too short"
    peak = np.abs(x).max()
    if peak < 1e-3:
        return None, "silent"
    x = x / peak * 0.89
    flux, hop = spectral_flux(x)
    bpm, conf = estimate_bpm(flux, hop)
    if bpm is None or conf < 0.08:
        return None, f"no steady tempo (conf {conf:.2f})"
    bar = 4 * 60 / bpm * TARGET_SR   # samples per 4/4 bar
    if len(x) <= MAX_SECONDS * TARGET_SR * 1.02:
        # Already a cut break: trust the cut. Trim leading/trailing digital
        # silence, snap the ends to zero crossings, and call the result a
        # whole number of bars; the bpm estimate only picks *which* number.
        audible = np.nonzero(np.abs(x) > 0.008)[0]
        start = snap_zero(x, int(audible[0]))
        tail = np.nonzero(np.abs(x) > 0.003)[0]
        end = snap_zero(x, min(len(x) - 1, int(tail[-1]) + 1))
        if end - start < MIN_SECONDS * TARGET_SR:
            return None, "too short after silence trim"
        bars = max(1, round((end - start) / bar))
    else:
        # Full song: cut whole bars from its most rhythmically dense stretch.
        for bars in (8, 4, 2, 1):
            if MIN_SECONDS * TARGET_SR <= bars * bar <= MAX_SECONDS * TARGET_SR:
                break
        else:
            return None, f"no whole-bar cut fits ({bpm} bpm)"
        nominal = int(round(bars * bar))
        start = best_window(flux, hop, int(nominal / hop))
        start = min(start, len(x) - nominal - 1)
        # Nudge the start onto the first strong onset at/after it.
        f0 = start // hop
        seg = flux[f0:f0 + int(TARGET_SR / hop)]  # look ahead up to 1s
        if len(seg) and seg.max() > 0:
            start = (f0 + int(np.argmax(seg > 0.5 * seg.max()))) * hop
        start = snap_zero(x, start)
        L = refine_loop_len(x, start, nominal)
        end = snap_zero(x, start + L)
        if end - start < MIN_SECONDS * TARGET_SR:
            return None, "refined loop too short"
    y = x[start:end]
    # The authoritative bpm is the one the cut implies.
    bpm = round(bars * 4 * 60 / (len(y) / TARGET_SR), 2)
    if not (55 <= bpm <= 200):
        return None, f"implied tempo implausible ({bpm} bpm, {bars} bars)"
    name = re.sub(r'[^A-Za-z0-9]+', '_', os.path.splitext(os.path.basename(path))[0]).strip('_')
    name = re.sub(r'_?dup$', '', name, flags=re.I)
    out = os.path.join(outdir, name + '.wav')
    sf.write(out, (y * 32767).astype(np.int16), TARGET_SR, subtype='PCM_16')
    return {
        'file': name + '.wav',
        'bpm': bpm,
        'bars': bars,
        'frames': int(len(y)),
        'seconds': round(len(y) / TARGET_SR, 3),
        'tempo_confidence': round(conf, 3),
        'source': os.path.abspath(path),
    }, None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--in', dest='indir', default=os.path.expanduser('~/sounds/breaks'))
    ap.add_argument('--out', dest='outdir', default=os.path.expanduser('~/sounds/cleanbreaks'))
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    entries, skipped, seen = [], [], set()
    for fn in sorted(os.listdir(args.indir), key=str.lower):
        path = os.path.join(args.indir, fn)
        if not os.path.isfile(path) or fn.startswith('.'):
            continue
        stem = re.sub(r'[^A-Za-z0-9]+', '_', os.path.splitext(fn)[0]).strip('_').lower()
        stem = re.sub(r'_?dup$', '', stem)
        if stem in seen:
            skipped.append((fn, "duplicate stem"))
            continue
        try:
            sf.info(path)
        except Exception:
            skipped.append((fn, "not decodable audio"))
            continue
        try:
            entry, why = clean_one(path, args.outdir)
        except Exception as e:
            entry, why = None, f"error: {str(e)[:60]}"
        if entry is None:
            skipped.append((fn, why))
        else:
            seen.add(stem)
            entries.append(entry)
            print(f"ok   {fn!r:44s} -> {entry['file']:36s} {entry['bpm']:6.1f} bpm {entry['bars']} bars {entry['seconds']:5.2f}s")
    with open(os.path.join(args.outdir, 'manifest.json'), 'w') as f:
        json.dump({'samplerate': TARGET_SR, 'breaks': entries}, f, indent=1)
    print(f"\n{len(entries)} converted, {len(skipped)} skipped -> {args.outdir}")
    for fn, why in skipped:
        print(f"skip {fn!r}: {why}")


if __name__ == '__main__':
    main()
