"""Plays a collection of MIDI files using the AMY piano voice."""

import os
import time
from typing import Iterable

import amy
from experiments.mido_piano import instrument


def init_amy() -> None:
    amy.live(audio_playback_device=0)
    # Volume value was determined by making 8-finger ff chords on a Kawai CA-67
    # come just short of the soft-clipping threshold.
    amy.send(volume=0.75)
    amy.reverb(0.1)
    amy.chorus(0)
    amy.echo(0)


def set_list() -> Iterable[str]:
    """Yields the sorted MIDI filenames in this directory."""
    directory = 'experiments/mido_piano'
    for filename in sorted(os.listdir(directory)):
        if filename.lower().endswith('.mid'):
            yield os.path.join(directory, filename)


def run() -> None:
    init_amy()
    try:
        piano = instrument.Piano()
        iter_filenames = iter(set_list())
        filename = next(iter_filenames)
        while True:
            try:
                piano.play_file(filename)
                filename = next(iter_filenames)
                time.sleep(2.0)
            except (StopIteration, KeyboardInterrupt):
                break
    finally:
        amy.pause()


if __name__ == '__main__':
    run()
