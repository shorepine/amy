# Musical use cases for reusable sequences

Reusable sequences let a caller define a collection of ordinary AMY events
once and launch that collection as one musical unit. AMY gives no musical
meaning to a sequence tag: a sequence may contain notes, parameter changes, or
controls for other sequences.

## Preloaded fills and phrases

A rhythm engine can preload each fill or phrase as a finite sequence. Its live
schedule then needs only a sequence start instead of another copy of every
event in the phrase. This keeps controller traffic and controller code small
even when the phrase catalogue is large.

An execution retains the definition with which it started. Rebuilding the
stored definition affects later starts but does not alter a phrase already in
progress. The caller therefore does not need to stream the phrase repeatedly,
calculate when it ends, or track which definition version is sounding.

## Arpeggios with complete note ownership

A short finite sequence can hold a note-on together with its matching
note-off. A periodic parent sequence can start these note-pair sequences in an
arpeggio pattern.

Stopping or replacing the parent prevents later child starts. Children which
already started remain independent and deliver their original note-offs. A
live change of rate, direction, voicing, or harmony can therefore be expressed
without mirroring AMY's clock or maintaining pending-note state in the caller.

Starting the same child again while an older execution is active is valid.
This permits note gates to overlap their trigger interval. If a caller instead
wants to truncate every active instance of the child, it can explicitly stop
the child's tag.

## Temporarily thinning a rhythm

A repeating percussion layer can be stored as a periodic sequence. The `gate`
action suppresses its ordinary event dispatch for a chosen number of ticks
while local phase continues. When the gate expires, the layer resumes where it
would otherwise have been.

This action does not silence audio which is already ringing. It controls
future event dispatch and continues to process sequence-control events, so a
controller sequence cannot gate away its own recovery. The caller decides
which tags represent musical layers; AMY implements only generic action,
duration, and phase behavior.

## A fixed number of repeats

An event with a nonzero period repeats until its execution is stopped. To play
it exactly `N` times, a finite controller sequence can start the periodic
sequence at local tick zero and stop it at `N * period`.

Sequence controls are processed before ordinary events on the same tick, so
the event at the stop boundary is not dispatched. This composes finite and
periodic sequences without adding a separate repeat-counter state.

## Parameter automation and compound gestures

Stored events are not limited to notes. A finite sequence can apply filter,
amplitude, pan, effects, patch, or other AMY changes at local ticks. This can
represent a reusable automation curve or a compound control gesture. AMY does
not invent inverse events when such an execution is stopped; the definition
must contain any restoration required by the caller's musical intent.

## Live definition changes

A controller can stop future launches, reset a tag, append a replacement
definition, and start it at a selected alignment. Executions which began before
the change keep their immutable snapshots; later starts use the replacement.

The controller continues to own musical policy and the ordering of the edit.
It does not need to own definition versions, phrase completion, sequence phase,
or note-release bookkeeping.
