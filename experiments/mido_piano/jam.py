"""Plays your USB MIDI input in the piano voice."""
import amy
from experiments.mido_piano import instrument


def run() -> None:
    amy.send(volume=2.0)
    amy.live()
    piano = instrument.Piano()
    piano.play_input()


if __name__ == '__main__':
    run()
