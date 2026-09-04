# Reusable sequencer sequences

AMY's existing sequencer tags can also identify reusable sequences. A reusable
sequence is a collection of ordinary AMY events with local `tick` and `period`
values. It can be started from Python, from the wire protocol, or from another
sequenced event.

The ordinary three-value `ticks=(tick, period, tag)` API remains unchanged. A
legacy tagged write replaces the event at that tag. Multi-event accumulation is
always explicit.

## Defining a sequence

The Python convenience API replaces all future contents at a tag:

```python
amy.define_sequence(40, [
    dict(ticks=(0,), synth=2, note=60, vel=1),
    dict(ticks=(12,), synth=2, note=60, vel=0),
])
```

Each event uses the normal AMY keyword arguments. Its `ticks` value is local to
the start of the sequence and contains `tick` plus an optional `period`.

`define_sequence()` validates every event before sending anything. It then
performs a per-tag reset followed by explicit cumulative writes. If a sequence
may be launched while it is being rewritten, first remove or stop those future
launches. An execution which already started is safe: it retains the immutable
definition it started with, including later note-offs.

Low-level callers can use `sequence_reset` and `sequence_event` directly:

```python
amy.send(sequence_reset=40)
amy.send(sequence_event=(40, 0, 0), synth=2, note=60, vel=1)
amy.send(sequence_event=(40, 12, 0), synth=2, note=60, vel=0)
```

The sequence tag and legacy root tag are one identity space. Writing a legacy
tagged `ticks` event replaces the future reusable definition at that tag;
explicitly appending a reusable event removes the future legacy root event at
that tag. Applications should assign distinct tags to stored phrases and root
launch events.

## Starting and stopping

```python
amy.send(sequence_control=(40, amy.SEQUENCE_CONTROL_START, 1))
amy.send(sequence_control=(40, amy.SEQUENCE_CONTROL_STOP, 48))
```

The optional final value is `alignment_period`. `0` or `1` acts at the next
available sequencer tick for a direct command. A larger value selects the next
tick divisible by that period. When a root sequencer event fires a start on a
tick, the child sequence's local tick zero participates in that same tick.

A start creates a bounded execution. More than one execution of a finite
sequence may overlap; no caller-generated execution ID is required. Stop
targets every active execution of the tag. Stopping a parent prevents its
future child starts but does not stop child sequences which already started.
This lets a note-on/note-off child own its complete lifetime.

## Finite and repeating lifetime

No explicit sequence length or publish action is needed:

- a definition containing only `period=0` events is finite and retires after
  its last event;
- an event with nonzero `period` repeats on its local period, and keeps that
  execution alive until it is stopped;
- a controlling finite sequence can start a periodic child at local tick zero
  and stop it after a chosen number of periods.

## Temporary event gating

```python
amy.send(sequence_control=(40, amy.SEQUENCE_CONTROL_GATE, 24, 1))
```

This suppresses ordinary event dispatch from active executions of tag `40` for
24 ticks. Their local phase continues and dispatch resumes on the original
phase. Audio which is already ringing is not cut off. Nested sequence controls
remain active while ordinary payload events are gated, so controller sequences
can still complete their lifecycle.

Gate duration `0` removes a gate at the selected alignment boundary.

## Reset behavior

- `amy.send(sequence_reset=tag)` removes the future legacy/root event and the
  future reusable definition for that tag. Active immutable executions finish.
- `RESET_TIMEBASE` discards active/pending executions because their absolute
  activation ticks cannot be rebased, but retains stored definitions.
- `RESET_SEQUENCER` retains its global meaning: it clears root events, reusable
  definitions, and active/pending executions.

## Capacity and realtime behavior

`max_sequencer_tags` bounds the shared tag space. `max_sequence_events` bounds
the number of events in one reusable definition, and
`max_sequence_executions` independently bounds active or alignment-pending
executions. Definitions are allocated only for tags which use them, and
inactive definitions are not scanned on each tick.

Starts fail clearly when the execution pool is full. Cyclic sequence launches
cannot allocate beyond that fixed pool and can be recovered with targeted stop
commands or `RESET_SEQUENCER`.

See the [implementation model](sequencer-sequences-abstractions.md),
[musical use cases](sequencer-sequences-musical-use-cases.md), and
[step-by-step examples](sequencer-sequences-howto.md).
