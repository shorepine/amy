#!/usr/bin/env python3

import argparse
import array
import math
import sys
import wave


def read_wave(path):
    with wave.open(path, "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frames = wav.getnframes()
        raw = wav.readframes(frames)

    if sample_width != 2:
        raise ValueError(f"{path}: expected 16-bit PCM, got {sample_width * 8} bits")

    samples = array.array("h")
    samples.frombytes(raw)
    if sys.byteorder != "little":
        samples.byteswap()
    return channels, sample_rate, frames, samples


def levels(samples):
    if not samples:
        return 0, 0.0, -200.0, -200.0, 0

    peak = max(abs(int(sample)) for sample in samples)
    sum_squares = sum(int(sample) * int(sample) for sample in samples)
    rms = math.sqrt(sum_squares / len(samples))
    peak_dbfs = 20.0 * math.log10(peak / 32768.0) if peak else -200.0
    rms_dbfs = 20.0 * math.log10(rms / 32768.0) if rms else -200.0
    clipped = sum(sample in (-32768, 32767) for sample in samples)
    return peak, rms, peak_dbfs, rms_dbfs, clipped


def main():
    parser = argparse.ArgumentParser(
        description="Compare AMY renderer samples with the I16 buffer handed to Oboe"
    )
    parser.add_argument("amy_wave")
    parser.add_argument("oboe_wave")
    parser.add_argument(
        "--min-peak-dbfs",
        type=float,
        default=-6.0,
        help="fail when either capture peak is below this value (default: -6 dBFS)",
    )
    args = parser.parse_args()

    amy_channels, amy_rate, amy_frames, amy = read_wave(args.amy_wave)
    oboe_channels, oboe_rate, oboe_frames, oboe = read_wave(args.oboe_wave)

    expected = (2, 48000)
    if (amy_channels, amy_rate) != expected:
        raise SystemExit(
            f"AMY capture format mismatch: {amy_channels} channels @ {amy_rate} Hz"
        )
    if (oboe_channels, oboe_rate) != expected:
        raise SystemExit(
            f"Oboe capture format mismatch: {oboe_channels} channels @ {oboe_rate} Hz"
        )
    if amy_frames != oboe_frames or len(amy) != len(oboe):
        raise SystemExit(
            f"capture length mismatch: AMY={amy_frames} frames Oboe={oboe_frames} frames"
        )

    mismatch_samples = 0
    max_abs_diff = 0
    for source, output in zip(amy, oboe):
        difference = abs(int(source) - int(output))
        if difference:
            mismatch_samples += 1
            max_abs_diff = max(max_abs_diff, difference)

    amy_peak, amy_rms, amy_peak_dbfs, amy_rms_dbfs, amy_clipped = levels(amy)
    oboe_peak, oboe_rms, oboe_peak_dbfs, oboe_rms_dbfs, oboe_clipped = levels(oboe)

    print(f"frames={amy_frames} channels={amy_channels} sample_rate={amy_rate}")
    print(
        f"AMY : peak={amy_peak:5d} {amy_peak_dbfs:7.2f} dBFS  "
        f"RMS={amy_rms:9.2f} {amy_rms_dbfs:7.2f} dBFS  clipped={amy_clipped}"
    )
    print(
        f"Oboe: peak={oboe_peak:5d} {oboe_peak_dbfs:7.2f} dBFS  "
        f"RMS={oboe_rms:9.2f} {oboe_rms_dbfs:7.2f} dBFS  clipped={oboe_clipped}"
    )
    print(f"sample mismatches={mismatch_samples} max_abs_diff={max_abs_diff}")

    if mismatch_samples != 0:
        raise SystemExit(
            "Oboe callback buffer is not byte-for-byte identical to the AMY render stream"
        )
    if amy_peak_dbfs < args.min_peak_dbfs:
        raise SystemExit(
            f"AMY peak {amy_peak_dbfs:.2f} dBFS is below minimum {args.min_peak_dbfs:.2f} dBFS"
        )
    if oboe_peak_dbfs < args.min_peak_dbfs:
        raise SystemExit(
            f"Oboe peak {oboe_peak_dbfs:.2f} dBFS is below minimum {args.min_peak_dbfs:.2f} dBFS"
        )
    if amy_clipped or oboe_clipped:
        raise SystemExit(
            f"full-scale clipping detected: AMY={amy_clipped} Oboe={oboe_clipped} samples"
        )


if __name__ == "__main__":
    main()
