#!/usr/bin/env python3
"""Normalize a folder of break-beat recordings into uniform, loopable WAVs.

Reads everything soundfile can decode from --in (WAV/AIFF, extensionless
AIFF, Sun .snd, MPC2000 .SND, ...), keeps only material with a *verified*
4/4 beat grid, and writes each to --out as:

    exactly ONE BAR (4 beats), cut starting on a detected downbeat,
    mono, 16-bit, 44100 Hz WAV, peak-normalized, zero-crossing ends,
    seam refined by waveform autocorrelation when content allows.

Analysis per file: spectral-flux onset envelope -> beat period by
autocorrelation (parabolic-refined, folded to 75-150 bpm) -> meter check
(the envelope must repeat better at 4 beats than at 3 beats; rejects 3/4,
free time, and unsteady playing) -> beat phase by comb alignment ->
downbeat as the strongest of the 4 bar phases. Anything that fails a step
is skipped and reported, so every surviving break carries a trustworthy
bars=1 / bpm pair and players can lock them all to one grid.

A manifest.json (file, bpm, bars, beats, frames, source) is written
alongside for players like play_cleanbreaks.py.

    python3 clean_breaks.py            # ~/sounds/breaks -> ~/sounds/cleanbreaks
"""
import argparse
import json
import os
import re

import numpy as np
import soundfile as sf

TARGET_SR = 44100
MIN_INPUT_SECONDS = 1.5
BEATS_PER_BAR = 4


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


def norm_autocorr(env):
    e = env - env.mean()
    ac = np.correlate(e, e, 'full')[len(e) - 1:]
    return ac / (ac[0] + 1e-9)


def overlap_ac(flux):
    # Overlap-normalized autocorrelation lookup: without the (n-lag)
    # correction, a perfectly bar-periodic 2-bar file can never score above
    # ~0.5 at its own bar lag, and every threshold lies.
    n = len(flux)
    e = flux - flux.mean()
    ac = np.correlate(e, e, 'full')[n - 1:]
    def r(lag):
        li = int(round(lag))
        if li < 1 or li + 8 >= n:
            return None
        return float((ac[li] / (n - li)) / (ac[0] / n + 1e-12))
    return r


def beat_period(flux, fps, lo_bpm=75, hi_bpm=150):
    # Beat period in (fractional) flux frames, folded into lo..hi bpm,
    # with a parabolic refinement of the autocorrelation peak.
    if len(flux) < 32 or flux.std() == 0:
        return None, 0.0
    ac = norm_autocorr(flux)
    lo_lag = max(2, int(fps * 60 / hi_bpm))
    hi_lag = min(int(fps * 60 / lo_bpm), len(ac) - 2)
    if hi_lag <= lo_lag:
        return None, 0.0
    lag = lo_lag + int(np.argmax(ac[lo_lag:hi_lag]))
    y0, y1, y2 = ac[lag - 1], ac[lag], ac[lag + 1]
    denom = y0 - 2 * y1 + y2
    d = 0.5 * (y0 - y2) / denom if abs(denom) > 1e-12 else 0.0
    d = max(-0.5, min(0.5, d))
    return lag + d, float(ac[lag])


def meter_is_duple(flux, tau):
    # 4/4 check. Long enough files: the onset envelope must repeat at a
    # 4-beat lag (and at least as well as at 3 beats -- rejects 3/4 and
    # unsteady time). Files too short for that lag qualify only if they
    # ARE one 4-beat bar, i.e. a pre-cut single bar.
    # Returns (ok, is_precut_bar, why).
    r = overlap_ac(flux)
    r4 = r(4 * tau)
    if r4 is None:
        beats_total = len(flux) / tau
        if 3.5 <= beats_total <= 4.5:
            return True, True, None
        return False, False, f"short file is {beats_total:.1f} beats, not one 4-beat bar"
    r3 = r(3 * tau)
    if r4 < 0.05:
        return False, False, f"bar-length self-similarity too weak (r4={r4:.2f})"
    if r3 is not None and r4 < r3 - 0.02:
        return False, False, f"repeats at 3 beats, not 4 (r3={r3:.2f} r4={r4:.2f}) - not 4/4?"
    return True, False, None


def bar_has_four_beats(flux, start_frame, tau):
    # The bar we cut must itself contain an onset at every beat slot.
    w = max(1, int(tau * 0.15))
    vals = []
    for k in range(BEATS_PER_BAR):
        i = int(round(start_frame + k * tau))
        if i >= len(flux):
            return False
        vals.append(float(flux[max(0, i - w):i + w + 1].max()))
    return max(vals) > 0.05 and min(vals) >= 0.1 * max(vals)


def beat_grid(flux, tau):
    # Comb-align the beat phase, then pick the strongest of the 4 bar
    # phases as the downbeat. Returns beat positions (frames) and the
    # index of the first downbeat within them.
    n_slots = int(tau)
    best_p, best_s = 0, -1.0
    for p in range(n_slots):
        idx = np.round(np.arange(p, len(flux) - 1, tau)).astype(int)
        s = float(flux[idx].sum() / max(1, len(idx)))
        if s > best_s:
            best_p, best_s = p, s
    beats = np.round(np.arange(best_p, len(flux) - 1, tau)).astype(int)
    if len(beats) < BEATS_PER_BAR:
        return beats, 0
    best_o, best_s = 0, -1.0
    for o in range(BEATS_PER_BAR):
        s = float(flux[beats[o::BEATS_PER_BAR]].mean())
        if s > best_s:
            best_o, best_s = o, s
    return beats, best_o


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


def refine_loop_len(x, start, nominal, slack=0.02, probe=4096):
    # The loop seam wants x[start:start+p] ~ x[start+L:start+L+p]; search L
    # around nominal for the best normalized correlation. Only possible when
    # the source continues past the seam; otherwise keep nominal.
    lo = int(nominal * (1 - slack))
    hi = min(int(nominal * (1 + slack)), len(x) - start - probe)
    if hi <= lo:
        return nominal
    a = x[start:start + probe]
    na = np.linalg.norm(a) + 1e-9
    best, best_l = -2.0, nominal
    for L in range(lo, hi, 8):
        b = x[start + L:start + L + probe]
        c = float(np.dot(a, b) / (na * (np.linalg.norm(b) + 1e-9)))
        if c > best:
            best, best_l = c, L
    return best_l


def clean_one(path, outdir):
    x = load_mono(path)
    if len(x) < MIN_INPUT_SECONDS * TARGET_SR:
        return None, "too short"
    peak = np.abs(x).max()
    if peak < 1e-3:
        return None, "silent"
    x = x / peak * 0.89
    flux, hop = spectral_flux(x)
    fps = TARGET_SR / hop
    tau, conf = beat_period(flux, fps)
    if tau is None or conf < 0.08:
        return None, f"no steady beat (conf {conf:.2f})"
    ok, is_precut_bar, why = meter_is_duple(flux, tau)
    if not ok:
        return None, why

    if is_precut_bar:
        # The file already is one 4-beat bar: trim silence and keep it.
        audible = np.nonzero(np.abs(x) > 0.008)[0]
        start = snap_zero(x, int(audible[0]))
        tail = np.nonzero(np.abs(x) > 0.003)[0]
        end = snap_zero(x, min(len(x) - 1, int(tail[-1]) + 1))
    else:
        beats, o0 = beat_grid(flux, tau)
        bar_len = int(round(BEATS_PER_BAR * tau * hop))  # samples
        # Try each downbeat in order; take the first full bar that also has
        # an onset on every one of its 4 beats.
        start = None
        for b in beats[o0::BEATS_PER_BAR]:
            s = int(b * hop)
            if s + bar_len > len(x) + int(0.03 * bar_len):  # allow a 3% tail shortfall
                break
            if bar_has_four_beats(flux, b, tau):
                start = s
                break
        if start is None:
            return None, "no downbeat bar with onsets on all 4 beats"
        start = snap_zero(x, start)
        L = refine_loop_len(x, start, bar_len)
        end = snap_zero(x, min(len(x) - 1, start + L))
    y = x[start:end]
    bpm = round(BEATS_PER_BAR * 60 * TARGET_SR / len(y), 2)
    if not (70 <= bpm <= 160):
        return None, f"implied tempo implausible ({bpm} bpm)"

    name = re.sub(r'[^A-Za-z0-9]+', '_', os.path.splitext(os.path.basename(path))[0]).strip('_')
    name = re.sub(r'_?dup$', '', name, flags=re.I)
    out = os.path.join(outdir, name + '.wav')
    sf.write(out, (y * 32767).astype(np.int16), TARGET_SR, subtype='PCM_16')
    return {
        'file': name + '.wav',
        'bpm': bpm,
        'bars': 1,
        'beats': BEATS_PER_BAR,
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
            print(f"ok   {fn!r:44s} -> {entry['file']:36s} {entry['bpm']:6.1f} bpm 1 bar {entry['seconds']:5.2f}s")
    with open(os.path.join(args.outdir, 'manifest.json'), 'w') as f:
        json.dump({'samplerate': TARGET_SR, 'breaks': entries}, f, indent=1)
    print(f"\n{len(entries)} converted, {len(skipped)} skipped -> {args.outdir}")
    for fn, why in skipped:
        print(f"skip {fn!r}: {why}")


if __name__ == '__main__':
    main()
