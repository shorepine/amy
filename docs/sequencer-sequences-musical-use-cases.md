# Musical use cases for reusable sequences

Reusable sequences reduce controller complexity when a musical phrase contains
several events but should be launched as one unit. The examples below describe
generic rhythm-engine behavior; AMY assigns no musical meaning to a tag.

## Preloaded fills

A rhythm engine can preload each fill once as a finite tagged sequence. Its
root schedule then stores only sequence starts. Selecting or deselecting a fill
changes future root launches, not the complete fill body.

An already-started fill holds its immutable definition and finishes even if its
future launches are removed. The controller does not calculate an end time,
stream the phrase repeatedly, or maintain an active-fill state machine.

## Arpeggios and note lifetime

A short child sequence can contain one note-on and its matching note-off. A
parent sequence starts these children in an arpeggio pattern. Stopping or
replacing the parent prevents future child starts; children which already
started keep their scheduled release.

This makes live rate, direction, voicing, or chord changes predictable without
requiring the controller to mirror AMY's clock or remember which note-offs are
still pending. Starting the same finite child again may overlap with an older
execution; each execution retains its own event snapshot.

An explicit stop of the child tag has the different, generic meaning of
terminating every active execution of that child. A caller can therefore choose
between stopping future launches at a parent and deliberately truncating the
leaf itself.

## Temporarily reducing a rhythm

A repeating percussion layer can be represented by a periodic sequence. A
finite gate suppresses its ordinary events for a chosen number of ticks while
its local phase keeps advancing. Once the gate expires, it resumes at the point
it would otherwise have reached; already-ringing audio is unaffected.

The controller decides which musical layer a tag represents and which layers
to gate. AMY implements only generic event dispatch, duration, and phase.

## Fixed repeat counts

Component periods define looping. When a phrase should repeat exactly `N`
times, a finite controller sequence can start the periodic phrase at tick zero
and stop it at `N * period`. Control processing precedes ordinary events, so the
event on the stop boundary is not dispatched.

This composes existing concepts instead of adding a separate repeat-mode or
published-length state.

## Live definition changes

A controller can remove future launches, reset and append the replacement
definition, then install new launches. Executions which started before the
change keep the old snapshot. Future starts use the new contents.

The controller still owns musical policy and transaction ordering, but it does
not own active execution revisions, note lifetime, phrase completion, or the
sequencer clock.
