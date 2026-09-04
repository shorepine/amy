# Reusable sequence abstractions and implementation

## Public model

The existing sequencer tag is the sequence identity. Every ordinary
`ticks=(tick, period, tag)` message appends an event to that tag. The tag is
reset explicitly and controlled with one start/stop operation. There is no
second group namespace, separate append command, fourth `ticks` field,
explicit length, or publish/revision operation.

At the Python API, `amy.send(sequence=tag, vel=...)` makes start and stop look
like note-on and note-off. Internally its compact `sequence_control` operation
provides:

- start, optionally aligned to an AMY sequencer period;
- stop all active executions of a tag at an optional boundary;
- gate ordinary events for a finite duration without resetting local phase.

Sequences may start or stop other sequences. A finite controller can therefore
express a fixed repeat count, and a parent can stop launching new note-pair
children while children already in progress deliver their note-offs.

## Why executions exist internally

A stored definition and an active execution have different lifetimes without
being different public abstractions. An execution needs a local start tick and
must retain the event data it began with. Otherwise editing a future phrase
could remove a note-off or alter a fill already sounding.

AMY therefore uses a bounded execution pool and reference-counted copy-on-write
definitions. Editing a definition used by an execution clones it. The active
execution keeps its old snapshot; later starts see the new contents. No
revision number or execution ID is exposed.

The copy is constructed while the old definition is pinned, but outside the
queue lock also used by rendering. Publication is a short checked pointer swap.
When the last execution releases an obsolete definition, the render path links
it onto an intrusive retirement list; a later non-rendering control call
detaches that list and performs the variable-time string and heap frees. The
audio path therefore neither copies nor frees a definition. Internally fired
wire payloads bypass the public wire-ingest boundary, while that public boundary
drains the retirement list after parsing. This makes reclamation a structural
control-path property rather than a best-effort test of concurrent render state.

This is reference-counted deferred reclamation, not a tracing garbage
collector. A fixed two-buffer ping-pong is insufficient because overlapping or
repeating executions can retain more than two generations at once. Allocating
versions only when an active definition is edited keeps the normal preload path
linear and bounds retained generations through the configured execution pool.
This matters in particular on embedded targets, where allocator and external-
memory/cache latency must not extend a render-thread critical section.

Finite executions of one tag may overlap. This supports phrases whose note
gate exceeds their trigger interval without transferring note state to the
caller.

## Lifetime inference and tick processing

If every event has `period=0`, the execution retires after its greatest local
tick. If any event has a nonzero period, it remains active until stopped.

Only untagged scheduled entries and active sequence executions are visited per
tick. Stored inactive definitions have no per-tick cost. Sequence controls are
processed before ordinary events, so a boundary stop prevents an event on that
boundary and a child start can include local tick zero on the same tick.

Gating suppresses ordinary payload dispatch while elapsed local time advances.
Control events are not gated, preventing a controller from muting its own
recovery operation.

## Bounds and recovery

Startup configuration bounds tags, events per definition, and simultaneous
executions. A cyclic control graph may fill the execution pool, but cannot grow
beyond it; later starts fail clearly and the caller can stop a tag or reset the
sequencer.

Aligned stop and gate commands capture the executions active when the command
is sent. An execution started later does not inherit previously pending control
state merely because its tag matches. This keeps control ownership on explicit
executions rather than creating a hidden per-tag automation timeline.

The ordinary three-field C event layout remains unchanged. Untagged one-off
and periodic scheduling, MIDI/external-clock behavior, and global reset retain
their existing behavior. The intentional API change is that a supplied tag now
creates a stopped reusable sequence and repeated writes cumulate instead of
replacing one scheduled event.
