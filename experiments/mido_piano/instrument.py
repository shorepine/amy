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
    * You edited amy/src/amy_config.h to have `#define AMY_OSCS 1024` before
      installing `libamy`.
"""

import amy
import mido
from experiments.mido_piano import midi


class MidoSynth(midi.Synth):
    """Bridge from `mido` to a Tulip `midi.Synth`."""

    def __del__(self):
        super().release()

    def play_message(self, message: mido.Message) -> None:
        """Plays a single MIDI message.

        All input values ranges are assumed to be as in standard MIDI format,
        which is in some cases different from what `midi.Synth` assumes.

        Args:
          message: The message to play.
        """
        if message.type == 'note_on':
            self.note_on(message.note, velocity=message.velocity / 127)
        elif message.type == 'note_off':
            self.note_off(message.note)
        elif message.is_cc():
            self.control_change(message.control, message.value)

    def play_file(self, filename: str, default_velocity: int = 64) -> None:
        """Plays a MIDI file.

        Args:
          filename: Path to a MIDI file to play.
          default_velocity: If this is set, and if all positive velocities of
            note on events have the same value, then this value will replace the
            constant velocity from the file. Without this, files with constant
            velocity 127 sound bad.
        """
        midi_file = mido.MidiFile(filename)
        velocities = [
            m.velocity for m in midi_file
            if m.type == 'note_on' and m.velocity > 0
        ]
        if (not velocities) or min(velocities) != max(velocities):
            default_velocity = None
        for m in midi_file.play():
            if m.type == 'note_on' and m.velocity > 0 and default_velocity:
                m = m.copy(velocity=default_velocity)
            self.play_message(m)

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

    def __init__(self):
        super().__init__(num_voices=32, patch_number=256)
