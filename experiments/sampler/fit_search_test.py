"""Measure what `fit_search` ('pS') buys the stretcher.

Stretch a pure tone 2x with `fit` and ask how much of the output is still at
the input frequency.  The WSOLA search can only find the aligned splice if it
can reach half a period, so the answer is a step function of `fit_search`:
below the tone's half-period the aligner locks onto a line of the comb spaced
at the hop rate instead of the note, and the output is at the wrong pitch.

    python3 experiments/sampler/fit_search_test.py

Deterministic (offline render), asserts the claims in docs/api.md, prints the
table.  No golden files.
"""
import numpy as np
import amy

SR = amy.AMY_SAMPLE_RATE
BLOCK = amy.AMY_BLOCK_SIZE
PPQ = 48  # AMY_SEQUENCER_PPQ
HOP = 512  # PCM_STRETCH_HOP
BPM = 120.0
SRC_SECS = 1.0
STRETCH = 2.0


def render_blocks(n):
    # amy.render() floors seconds*SR/BLOCK; pad half a block so we get exactly n.
    return amy.render((n * BLOCK + BLOCK // 2) / SR)


def spectrum(y):
    S = np.abs(np.fft.rfft(y * np.hanning(len(y)))) ** 2
    return S, np.fft.rfftfreq(len(y), 1.0 / SR)


def purity_db(y, freq):
    """Energy within +/-5 Hz of freq against everything else above 20 Hz."""
    S, f = spectrum(y)
    band = (f > freq - 5) & (f < freq + 5)
    rest = (f > 20) & ~band
    return 10 * np.log10(S[band].sum() / max(S[rest].sum(), 1e-20))


def peak_hz(y, freq):
    """Loudest bin within an octave either side of the expected tone."""
    S, f = spectrum(y)
    sel = (f > freq * 0.5) & (f < freq * 2)
    return f[sel][np.argmax(S[sel])]


def stretch_tone(freq, search):
    """Render `freq` Hz stretched by STRETCH with fit_search=`search`.

    `search=None` leaves it unset, i.e. the PCM_STRETCH_SEARCH default.
    """
    amy.restart()
    _ = render_blocks(4)
    amy.send(volume=8)  # a lone PCM voice sits ~23 dB down at default mixdown
    n = int(SRC_SECS * SR)
    x = 0.7 * np.sin(2 * np.pi * freq * np.arange(n) / SR)
    amy.load_sample_bytes((x * 32767.0).astype('<i2').tobytes(),
                          preset=101, midinote=60, sr=SR)
    _ = render_blocks(2)
    amy.send(tempo=BPM)
    kw = dict(osc=0, wave=amy.PCM, preset=101, vel=1,
              fit=STRETCH * SRC_SECS * BPM * PPQ / 60.0)
    if search is not None:
        kw['fit_search'] = search
    amy.send(**kw)
    out = render_blocks(int(np.ceil(STRETCH * SRC_SECS * SR / BLOCK)))[:, 0]
    # Analyze the steady middle, clear of note-on and the windowed tail.
    return out[int(0.4 * SR):int(1.8 * SR)].astype(np.float64)


def main():
    half_period = {f: SR / (2 * f) for f in (110.0, 440.0)}
    print(f"hop {HOP} frames = {SR / HOP:.1f} Hz;  "
          f"default search 64 reaches periods down to {SR / 128:.0f} Hz")
    print(f"{'tone':>7} {'half-period':>12} {'pS':>8} {'purity':>10} {'peak Hz':>9}")
    results = {}
    for freq in (110.0, 440.0):
        for search in (0, None, 64, 128, 256, 512, 4000):
            y = stretch_tone(freq, search)
            p, pk = purity_db(y, freq), peak_hz(y, freq)
            results[(freq, search)] = (p, pk)
            label = 'unset' if search is None else str(search)
            print(f"{freq:7.0f} {half_period[freq]:11.0f}f {label:>8} "
                  f"{p:7.1f} dB {pk:9.1f}")

    def r(freq, search):
        return results[(freq, search)]

    # Unset must be exactly the compiled-in default.
    assert r(110.0, None) == r(110.0, 64), (r(110.0, None), r(110.0, 64))
    # Over the ceiling clamps rather than doing something else.
    assert r(110.0, 4000) == r(110.0, 512), (r(110.0, 4000), r(110.0, 512))
    # pS=0 is fixed-grid OLA: the aligner is gone, the tone is destroyed.
    assert r(110.0, 0)[0] < -50, r(110.0, 0)
    assert r(440.0, 0)[0] < -50, r(440.0, 0)
    # 110 Hz needs 218 frames of reach; the default 64 misses it and lands on
    # a comb line ~1.5x too high, and 256 recovers the true pitch.
    assert abs(r(110.0, 64)[1] - 110.0) > 20, r(110.0, 64)
    assert abs(r(110.0, 256)[1] - 110.0) < 2, r(110.0, 256)
    assert r(110.0, 256)[0] > r(110.0, 64)[0] + 20, (r(110.0, 256), r(110.0, 64))
    # 440 Hz is inside the default's reach, so it is already at pitch -- but a
    # wider comparison window still cleans it up.
    assert abs(r(440.0, 64)[1] - 440.0) < 2, r(440.0, 64)
    assert r(440.0, 256)[0] > r(440.0, 64)[0] + 10, (r(440.0, 256), r(440.0, 64))
    print("ok")


if __name__ == '__main__':
    main()
