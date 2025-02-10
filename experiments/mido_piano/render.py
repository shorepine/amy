"""Renders a MIDI file using AMY piano and soundfile.

Usage:

```
python3 -m experiments.mido_piano.render input.mid output.wav 10.0
```
"""

import os
import sys

import soundfile

import amy
from experiments.mido_piano import instrument


def run(input_filename: str,
        output_filename: str,
        volume_db: float = 0.0) -> None:
    duration = None
    try:
        piano = instrument.Piano(patch_time=0.0)
        samples, sample_rate = piano.render(input_filename,
                                            volume_db=volume_db)
        duration = samples.shape[0] / sample_rate
        soundfile.write(output_filename, samples, int(round(sample_rate)))
    finally:
        piano.release(time=(duration * 1000.0))


if __name__ == '__main__':
    run(sys.argv[1], sys.argv[2], float(sys.argv[3]))
