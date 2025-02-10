"""Piano MIDI sound module based on AMY and `mido`.

Example usage:

```
piano = instrument.Piano()
piano.play_file('test.mid')
```

Assumptions:
    * `amy.live()` and other setup are done external to this module.
    * Patch 256 is piano, and we will use 32 voices.
    * The `libamy` and `mido` packages are installed and supported by your
      platform. `python-rtmidi` is also useful for `play_input`.
    * You edited `#define AMY_OSCS` in amy/src/amy_config.h to have enough.
      (Enough is (32 * 21 == 672), which is the number of voices in `Piano`
      below times the number of oscs for the dpwe piano patch, except that
      there are chorus oscs and 999 and stuff, so why not go big?)
"""

import mido
import numpy as np

import amy
from experiments.mido_piano import midi


class MidoSynth(midi.Synth):
    """Bridge from `mido` to a Tulip `midi.Synth`."""

    def __del__(self):
        super().release()

    def play_message(self,
                     message: mido.Message,
                     time: float | None = None) -> None:
        """Plays a single MIDI message.

        All input values ranges are assumed to be as in standard MIDI format,
        which is in some cases different from what `midi.Synth` assumes.

        Args:
          message: The message to play.
          time: Optional time to forward to amy_send.
        """
        if message.type == 'note_on':
            self.note_on(message.note,
                         velocity=message.velocity / 127,
                         time=time)
        elif message.type == 'note_off':
            self.note_off(message.note, time=time)
        elif message.is_cc():
            self.control_change(message.control, message.value, time=time)

    def play_file(self,
                  filename: str,
                  default_velocity: int = 64,
                  blocking: bool = True,
                  start_millis: float = 0.0) -> float:
        """Plays a MIDI file.

        Args:
          filename: Path to a MIDI file to play.
          default_velocity: If this is set, and if all positive velocities of
            note on events have the same value, then this value will replace the
            constant velocity from the file. Without this, files with constant
            velocity 127 sound bad.
          blocking: Whether to use `mido.MidiFile.play`. The canonical use case
            for setting this to `False` is `amy.render`.
          start_millis: AMY time for the start of the file. Only matters when
            `blocking == False`.

        Returns:
          The duration of the MIDI file, in seconds.
        """
        midi_file = mido.MidiFile(filename)
        duration = sum((m.time for m in midi_file))
        velocities = [
            m.velocity for m in midi_file
            if m.type == 'note_on' and m.velocity > 0
        ]
        if (not velocities) or min(velocities) != max(velocities):
            default_velocity = None

        def filter_fn(m: mido.Message) -> mido.Message:
            if m.type == 'note_on' and m.velocity > 0 and default_velocity:
                return m.copy(velocity=default_velocity)
            return m

        if blocking:
            for m in midi_file.play():
                self.play_message(filter_fn(m))
        else:
            millis = start_millis
            for m in midi_file:
                m = filter_fn(m)
                millis += m.time * 1000.0
                self.play_message(m, time=millis)
        return duration

    def render(self,
               filename: str,
               volume_db: float = 0.0) -> tuple[np.ndarray, float]:
        start_millis = 0.0
        amy.send(volume=np.pow(10.0, volume_db / 20.0), time=start_millis)
        samples = amy.render(self.play_file(filename, blocking=False))
        return samples, amy.AMY_SAMPLE_RATE

    def play_input(self, name: str | None = None) -> None:
        """Plays MIDI messages from a `mido` input.

        This is useful if you are connected to a USB instrument and have also
        installed the `python-rtmidi` package.

        name: `mido` input name from which to consume messages.
          `mido.get_input_names()` shows your options. USB replugging can cause
          the name to change. If none, this attempts to find the first input
          with "usb" in its lowercased name.
        """
        if not name:
            for candidate in mido.get_input_names():
                if 'usb' in candidate.lower():
                    name = candidate
                    break
        for message in mido.open_input(name):
            self.play_message(message)


class Piano(MidoSynth):

    def __init__(self, patch_time: float | None = None):
        super().__init__(num_voices=16,
                         patch_number=256,
                         patch_time=patch_time)
