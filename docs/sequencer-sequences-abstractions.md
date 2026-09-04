# Reusable sequence abstractions and implementation

## Public model

The public model has two ways to use the existing sequencer tag identity:

1. `ticks=(tick, period, tag)` keeps the established single-event behavior;
2. `define_sequence(tag, events)` explicitly gives that tag multiple local
   events which can be started and stopped as a reusable sequence.

There is no second public group ID, no local event-tag namespace, no fourth
`ticks` field, no explicit length, and no publish/revision command.

`sequence_control` supplies the three generic runtime operations:

- start, optionally aligned to an AMY sequencer period;
- stop every active execution of the tag at an optional alignment boundary;
- gate ordinary events for a finite duration without resetting local phase.

Sequences may start or stop other sequences. A finite controller sequence can
therefore express a fixed repeat count, and a parent can stop launching new
note-pair children while children already in progress deliver their note-offs.

## Why executions still exist internally

A stored definition and an active execution have different lifetimes even
though that distinction is not a second public API. An execution needs a local
start tick and must retain the event data it began with. Without that internal
separation, changing a future phrase could remove a note-off or alter a fill
which is already sounding.

AMY therefore uses a small bounded execution pool and reference-counted,
copy-on-write definitions. Appending to a definition which an execution still
uses first clones it. The active execution keeps the old snapshot; later starts
see the updated contents. No revision number is exposed to callers.

Multiple finite executions of one tag may overlap. This is important for
ordinary musical phrases whose gate time is longer than the interval between
starts. The execution pool, rather than a caller-managed ID scheme, is the
bound.

## Lifetime inference

The component events define lifetime:

- if every event has `period=0`, the execution retires after its greatest local
  tick has been processed;
- if any event has a nonzero period, the execution remains active and evaluates
  that event against elapsed local time until stopped.

This avoids an independent length that could disagree with the ordinary
sequencer periods. A fixed number of repeats is composition: a finite parent
starts a periodic child and stops it at the required local tick.

## Tick processing

Only active root entries and active sequence executions are visited per tick.
Stored but inactive definitions have no per-tick cost.

Sequence controls are processed before ordinary events for a tick. Consequently
a stop scheduled at a period boundary prevents the event on that boundary, and
a parent launch can make a child's local tick-zero event run on the launch tick.

Temporary gating suppresses ordinary payload dispatch but advances elapsed
local time normally. Control events are not gated; otherwise a controller could
mute its own future stop or recovery operation.

## Bounds and recovery

All storage is configured at startup:

- `max_sequencer_tags`: shared public identities;
- `max_sequence_events`: maximum events in one stored definition;
- `max_sequence_executions`: active and pending executions.

Definitions allocate event storage only when first used. The render path does
not perform unbounded allocation. A recursive or cyclic control graph can fill
the execution pool, but cannot grow past it; further starts fail and the caller
can stop a tag or reset the sequencer.

## Compatibility boundary

The legacy parser, C event layout, anonymous-event pool, modulo timing,
same-tag replacement, MIDI/external-clock behavior, and root active-list order
are unchanged. Reusable accumulation only occurs through the explicit sequence
API. Tests cover both the old path and the interaction between legacy and
reusable forms.
