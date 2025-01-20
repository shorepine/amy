"""Stress test for AMY piano voice, curated by an amateur pianist.

```
python3 -m experiments.piano_recital.runner
```

Will play all the .mid files in experiments/piano_recital, sorted by filename.

Assumptions:
    * `audio_playback_device=0`. If not, edit this file.
    * You execute the command from top-level amy/.
    * The `libamy` and `mido` packages are installed.
    * python3 is CPython, probably on a laptop.
    * You edited amy/src/amy_config.h to have `#define AMY_OSCS 1024` before
      installing `amy`.

Findings:
    * Voice stealing can be noticed, even at 32 voices, especially in the Ocean
      Etude op_25_no_12. It would be nice not only to make voice stealing aware
      of velocities but also to support turning MAX_VOICES up to more than 88.
      (As of now, this seems to trigger memory addressing bugs.)
    * Output can crackle on Linux due to buffer underrun. See
      https://github.com/mackron/miniaudio/issues/427
      Editing amy/src/libminiaudio-audio.c to add
      `deviceConfig.periodSizeInFrames = AMY_BLOCK_SIZE * 8;` reduces the
      severity while preserving the use of PulseAudio. Switching to JACK using
      both `#define MA_NO_PULSEAUDIO` and `#define MA_NO_ALSA` achieves 0
      dropped samples.
"""

import os
import time
from typing import Iterable

import amy
import mido
from experiments.piano_recital import midi


def _velocity_is_binarized(midi_file: mido.MidiFile) -> bool:
    """Returns whether all note ons have a constant positive velocity."""
    velocities = [
        m.velocity for m in midi_file if m.type == 'note_on' and m.velocity > 0
    ]
    return velocities and min(velocities) == max(velocities)


def play_piece(synth: midi.Synth,
               filename: str,
               default_velocity: int = 64) -> None:
    """Plays a piano MIDI file to a Synth.

    This strongly assumes we are playing a *piano* in that it ignores channels
    as well all messages other than note on, note off, and the control changes
    for the three pedals.

    Args:
      synth: The synth that will play the notes. Patch and effects must have
          already been set.
      filename: Path to a MIDI file to play.
      default_velocity: Some files, like the Bach above, binarize velocity to
          {0, N}, either because they were programmed rather than played. This
          is neither pianistic nor realistic, and if N is large, it causes
          distortion. For such files, the constant nonzero velocity will be
          replaced by this value.
    """
    try:
        midi_file = mido.MidiFile(filename)
        use_default_velocity = _velocity_is_binarized(midi_file)
        for m in midi_file.play():
            if m.type == 'note_on':
                velocity = m.velocity
                if velocity > 0 and use_default_velocity:
                    velocity = default_velocity
                synth.note_on(m.note, velocity=velocity / 127)
            elif m.type == 'note_off':
                synth.note_off(m.note)
            elif m.is_cc() and m.control in (64, 66, 67):
                synth.control_change(m.control, m.value)
    finally:
        synth.all_notes_off()


def give_recital(filenames: Iterable[str]) -> None:
    """Plays each MIDI file on the AMY piano."""
    amy.live(audio_playback_device=0)
    try:
        # The volume that ff 4-finger chords from a Kawai CA-67 peak just below
        # the soft-clipping threshold is 0.75.
        amy.send(volume=0.75)
        amy.reverb(0.1)
        amy.chorus(0)
        amy.echo(0)
        synth = midi.Synth(num_voices=32, patch_number=256)
        try:
            iter_filenames = iter(filenames)
            filename = next(iter_filenames)
            while True:
                try:
                    play_piece(synth, filename)
                    filename = next(iter_filenames)
                    time.sleep(1.0)
                except (StopIteration, KeyboardInterrupt):
                    break
        finally:
            synth.release()
    finally:
        amy.pause()


def set_list() -> Iterable[str]:
    """Yields the sorted MIDI filenames in this directory."""
    directory = 'experiments/piano_recital'
    for filename in sorted(os.listdir(directory)):
        if filename.lower().endswith('.mid'):
            yield os.path.join(directory, filename)


if __name__ == '__main__':
    give_recital(set_list())
