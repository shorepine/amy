# Reusable sequence abstractions and implementation

## Public abstractions

### Definition

A three-value `ticks=(tick, period, tag)` event contributes one ordinary AMY
event to the reusable definition identified by `tag`. Repeating the tag
accumulates events. Ticks in a definition are local to each execution.

`amy.define_sequence(tag, events)` is a Python replace-as-a-list convenience:
it validates every event, resets the future definition, and then sends the
tagged events. `sequence_reset=tag` resets only the definition used by future
starts. It does not rewrite an execution which already started.

### Execution

The action `start` creates an execution with its own local start tick. Several
finite executions of the same definition may overlap. The action `stop`
selects all executions of the tag which are active when the action is issued.
If the stop is aligned to a later boundary, an execution started after the
stop request is not implicitly captured by it.

An execution containing only period-zero events is finite and retires after
its greatest local tick. If any event has a nonzero period, the execution
repeats until stopped.

### Gate

The action `gate` suppresses ordinary event dispatch for a duration while
local phase advances. It does not stop audio which is already ringing.
Sequence-control events continue to run while gated, allowing a finite
controller sequence to restore or change another sequence without being
blocked by its own gate.

### Composition

A stored payload may be an ordinary AMY event or a control for another
sequence. A finite sequence can therefore launch note gestures, control a
periodic sequence for a fixed number of repeats, or coordinate several
independent phrases. Cycles are not recursively expanded through C call
frames: each successful start occupies a slot in the bounded execution pool,
so a cyclic graph fails further starts once that pool is full and remains
recoverable through stop or reset.

## Event ordering and ownership

For a given tick, sequence controls are processed before ordinary events. A
stop on a boundary therefore prevents the ordinary event on that boundary,
and a child start can include the child's local tick-zero event on the same
tick.

Stopping an execution cancels its future payloads. AMY cannot synthesize a
generic inverse for arbitrary events: a payload may change a filter, load a
patch, start another sequence, or send a note. If a phrase must complete a
release, store that release in a finite child and stop the parent which creates
future children. If the caller intentionally stops the child itself, its
remaining payloads are intentionally cancelled.

## Immutable snapshots

A definition and an execution have different lifetimes. Once an execution
starts, it holds a reference to the exact definition version it observed.
Changing the tag publishes a new version for future starts; existing
executions continue to read their old versions. This prevents a live edit from
removing a pending note-off or changing another payload halfway through a
phrase.

The implementation uses copy-on-write snapshot semantics. A definition owned
only by its tag can be appended in place. If an execution or competing writer
also holds it, an editor pins that source and constructs a complete candidate
copy. This is the data-versioning rule; it is not by itself sufficient for a
real-time audio thread because copying and freeing are variable-time work.

## RCU-like publication and deferred reclamation

Candidate construction happens outside `amy_queue_lock`. After cloning the
events and their wire strings, the editor briefly reacquires the lock and
publishes the candidate only if the tag still points to the source it cloned.
Publication is therefore a checked pointer swap. If another writer won the
race, the losing writer discards its private candidate outside the lock and
retries from the newly published definition. Concurrent cumulative writers do
not silently lose one another's events.

Executions act as readers by retaining references to their immutable versions.
When the render path releases the last reference, it does not free the event
array or its strings. It links the definition onto an intrusive retired list,
which requires no allocation. A later non-rendering command boundary detaches
that list under the lock and performs destruction after releasing the lock.
Internally fired sequence payloads bypass the public command boundary so they
cannot accidentally reclaim memory from the render path.

This is an RCU-like publication and reclamation scheme with explicit reference
counts, not a tracing garbage collector. Copy-on-write still describes how a
new immutable version is created; RCU-like publication describes how readers
continue safely and how old versions are retired without waiting or freeing on
the audio path.

Two fixed ping-pong buffers are insufficient. Multiple overlapping or
indefinitely repeating executions may retain more than two historical
generations while additional edits are published. Explicit references allow
exactly the generations which remain in use to survive. A general garbage
collector would add machinery without improving that already-known ownership.

## Why this matters on ESP32

At 48 kHz with 128-sample render blocks, one block represents approximately
2.67 ms. Heap allocation, copying many variable-length wire strings, heap
coalescing, PSRAM/cache latency, and destruction of an entire definition are
not usefully bounded operations within that deadline. Performing them while
holding the lock shared with sequence rendering can turn an infrequent live
edit into an audio dropout.

The current design limits the shared-lock publication step to reference
updates, validation, and a pointer swap. The render path releases references
and links retired objects without allocating or freeing. This removes the
known variable-time definition work from the render critical section.

That architecture reduces and bounds the source-level risk; it is not a claim
that every ESP32 configuration is proven hard real-time. Final assurance still
requires measurement on the target board with the intended sample rate, block
size, memory capabilities, effects load, concurrent authoring traffic, heap
low-water mark, and worst observed render deadline.

## Capacity and per-tick cost

`max_sequencer_tags` bounds definition identities. `max_sequence_events`
bounds events in one definition, and `max_sequence_executions` bounds active
or alignment-pending executions. Definitions allocate lazily. The tick loop
visits active executions and directly scheduled entries, not every inactive
definition.

Allocation failure, a full definition, an unavailable execution slot, an
invalid tag, and malformed action shapes fail with diagnostics. A failed
publication leaves the previously published definition intact.

See [Status and compatibility](sequencer-sequences-status.md) for validated
behavior, platform limits, and migration guidance.
