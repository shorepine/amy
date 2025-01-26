"""Pared-down fork of Tulip midi.py.

piano_recital.py in the AMY repository is the dependant motivating the fork.

Forking avoids a dependency cycle and eliminates some opportunities for
confusion about the necessity of or assumptions about globals like midi config
and event handler.

`Queue` was removed in favor of the standard `collections.deque`, reflecting
the assumption that piano_recital.py will usually be run under CPython.
"""
import collections
import time

import amy


class VoiceObject:
    """Object to wrap an amy voice."""

    def __init__(self, amy_voice):
        self.amy_voice = amy_voice

    def note_on(self, note, vel, time=None, sequence=None):
        amy.send(time=time,
                 voices=self.amy_voice,
                 note=note,
                 vel=vel,
                 sequence=sequence)

    def note_off(self, time=None, sequence=None):
        amy.send(time=time, voices=self.amy_voice, vel=0, sequence=sequence)


class Synth:
    """Manage a polyphonic synthesizer by rotating among a fixed pool of voices.

    Provides methods:
      synth.note_on(midi_note, velocity, time=None, sequence=None)
      synth.note_off(midi_note, time=None, sequence=None)
      synth.all_notes_off()
      synth.program_change(patch_num) changes preset for all voices.
      synth.control_change(control, value) modifies a parameter for all voices.
    Provides read-back attributes (for voices.py UI):
      synth.amy_voices
      synth.patch_number
      synth.patch_state  - patch-specific data only used by clients e.g. UI state

    Note: The synth internally refers to its voices by indices in
    range(0, num_voices).  These numbers are not related to the actual amy
    voices rendering the note; the amy voice number is internal to the
    VoiceObjects and is opaque to the Synth object.
    """
    # Class-wide record of which voice to allocate next.
    allocated_amy_voices = set()
    next_amy_patch_number = 1024

    @classmethod
    def reset(cls):
        """Resets AMY and Synth's tracking of its state."""
        cls.allocated_amy_voices = set()
        cls.next_amy_patch_number = 1024
        amy.reset()

    def __init__(self, num_voices=6, patch_number=None, patch_string=None):
        self.voice_objs = self._get_new_voices(num_voices)
        self.released_voices = collections.deque(range(num_voices))
        self.active_voices = collections.deque(tuple(), num_voices)
        # Dict to look up active voice from note number, for note-off.
        self.voice_of_note = {}
        self.note_of_voice = [None] * num_voices
        self.sustaining = False
        self.sustained_notes = set()
        # Fields used by UI
        self.patch_number = None
        self.patch_state = None
        if patch_number is not None and patch_string is not None:
            raise ValueError(
                'You cannot specify both patch_number and patch_string.')
        if patch_string is not None:
            patch_number = Synth.next_amy_patch_number
            Synth.next_amy_patch_number = patch_number + 1
            amy.send(store_patch='%d,%s' % (patch_number, patch_string))
        self.program_change(patch_number)

    def _get_new_voices(self, num_voices):
        new_voices = []
        next_amy_voice = 0
        while len(new_voices) < num_voices:
            while next_amy_voice in Synth.allocated_amy_voices:
                next_amy_voice += 1
            new_voices.append(next_amy_voice)
            next_amy_voice += 1
        self.amy_voice_nums = new_voices
        Synth.allocated_amy_voices.update(new_voices)
        voice_objects = []
        for amy_voice_num in self.amy_voice_nums:
            voice_objects.append(VoiceObject(amy_voice_num))
        return voice_objects

    @property
    def amy_voices(self):
        return [o.amy_voice for o in self.voice_objs]

    @property
    def num_voices(self):
        return len(self.voice_objs)

    # send an AMY message to the voices in this synth
    def amy_send(self, **kwargs):
        vstr = ",".join([str(a) for a in self.amy_voice_nums])
        amy.send(voices=vstr, **kwargs)

    def _get_next_voice(self):
        """Return the next voice to use."""
        # First try free/released_voices in order, then steal from active_voices.
        if self.released_voices:
            return self.released_voices.popleft()
        # We have to steal an active voice.
        stolen_voice = self.active_voices.popleft()
        #print('Stealing voice for', self.note_of_voice[stolen_voice])
        self._voice_off(stolen_voice)
        return stolen_voice

    def _voice_off(self, voice, time=None, sequence=None):
        """Terminate voice, update note_of_voice, but don't alter the queues."""
        self.voice_objs[voice].note_off(time=time, sequence=sequence)
        # We no longer have a voice playing this note.
        del self.voice_of_note[self.note_of_voice[voice]]
        self.note_of_voice[voice] = None

    def note_off(self, note, time=None, sequence=None):
        if self.sustaining:
            self.sustained_notes.add(note)
            return
        if note not in self.voice_of_note:
            return
        old_voice = self.voice_of_note[note]
        self._voice_off(old_voice, time=time, sequence=sequence)
        # Return to released.
        self.active_voices.remove(old_voice)
        self.released_voices.append(old_voice)

    def all_notes_off(self):
        self.sustain(False)
        while self.active_voices:
            voice = self.active_voices.popleft()
            self._voice_off(voice)
            self.released_voices.append(voice)

    def note_on(self, note, velocity=1, time=None, sequence=None):
        if not self.amy_voice_nums:
            # Note on after synth.release()?
            raise ValueError(
                'Synth note on with no voices - synth has been released?')
        if velocity == 0:
            self.note_off(note, time=time, sequence=sequence)
        else:
            # Velocity > 0, note on.
            if note in self.sustained_notes:
                self.sustained_notes.remove(note)
            if note in self.voice_of_note:
                # Send another note-on to the voice already playing this note.
                new_voice = self.voice_of_note[note]
            else:
                new_voice = self._get_next_voice()
                self.active_voices.append(new_voice)
                self.voice_of_note[note] = new_voice
                self.note_of_voice[new_voice] = note
            self.voice_objs[new_voice].note_on(note,
                                               velocity,
                                               time=time,
                                               sequence=sequence)

    def sustain(self, state):
        """Turn sustain on/off."""
        if state:
            self.sustaining = True
        else:
            self.sustaining = False
            for midinote in self.sustained_notes:
                self.note_off(midinote)
            self.sustained_notes = set()

    def get_patch_state(self):
        return self.patch_state

    def set_patch_state(self, state):
        self.patch_state = state

    def program_change(self, patch_number):
        if patch_number != self.patch_number:
            self.patch_number = patch_number
            # Reset any modified state due to previous patch modifications.
            self.patch_state = None
            time.sleep(0.1)  # "AMY queue will fill if not slept."
            self.amy_send(load_patch=patch_number)

    def control_change(self, control, value):
        if control == 64:
            if value > 100 and not self.sustaining:
                self.sustain(True)
            if value < 60 and self.sustaining:
                self.sustain(False)

    def release(self):
        """Called to terminate this synth and release its amy_voice resources."""
        # Turn off any active notes
        self.all_notes_off()
        # Return all the amy_voices
        for amy_voice in self.amy_voice_nums:
            Synth.allocated_amy_voices.remove(amy_voice)
        self.amy_voice_nums = []
        del self.voice_objs[:]
