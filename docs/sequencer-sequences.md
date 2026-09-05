# Reusable sequences

A sequencer tag identifies a reusable sequence of ordinary AMY events. Sending
more than one event with the same tag accumulates those events, in the same way
that repeated `synth=` messages configure one synth. Tagged events use local
ticks and remain inactive until the sequence is started.

Untagged `ticks` events keep their direct scheduling behavior on the global
sequencer clock.

## Defining a sequence

The Python convenience API replaces all future contents at a tag:

```python
amy.define_sequence(40, [
    dict(ticks=(0,), synth=2, note=60, vel=1),
    dict(ticks=(12,), synth=2, note=60, vel=0),
])
```

Each event uses normal AMY keyword arguments. Its `ticks` value is local to the
start of the sequence and contains `tick` plus an optional `period`.
`define_sequence()` validates every event, resets the tag, then sends ordinary
tagged `ticks` messages:

```python
amy.send(sequence_reset=40)
amy.send(ticks=(0, 0, 40), synth=2, note=60, vel=1)
amy.send(ticks=(12, 0, 40), synth=2, note=60, vel=0)
```

Repeating tag `40` accumulates both events. `sequence_reset=40` explicitly
replaces the definition; the empty wire form `H0,0,40Z` is an equivalent reset.
With an event payload, `ticks=(0, 0, 40)` is a valid local tick-zero event.

## Starting and stopping

```python
amy.send(sequence=40, action='start', alignment_period=1)
amy.send(sequence=40, action='stop', alignment_period=48)
amy.send(sequence=40, action='gate', duration=24, alignment_period=1)
```

The named actions expose the complete control model: `start` creates an
execution, `stop` terminates the selected executions, and `gate` temporarily
suppresses their ordinary events for the required `duration`. `vel` keeps its
usual meaning of note velocity. At the lower-level `sequence_control` API and
on the wire, actions use integers: stop `0`, start `1`, and gate `2`.
Fractional values are invalid. The optional `alignment_period` is the alignment
quantum. `0` or `1` acts at the next available sequencer tick for a direct
command. A larger value selects the next global tick divisible by that period.
When a sequenced parent starts a child, the child's local tick zero participates
in the same tick.

A start creates a bounded execution. Finite executions of one tag may overlap,
so callers do not need execution IDs or note-lifetime bookkeeping. Stop targets
all executions of that tag which are active when the command is sent. If the
stop is aligned to a future boundary, a separate execution started after that
command does not inherit its pending stop. This avoids hidden per-tag control
state. Stopping a parent prevents future child starts, while children already
started retain their own event pairs.

## Finite and repeating lifetime

Lifetime follows directly from the periods of the stored events:

- a definition containing only `period=0` events is finite and retires after
  its last event;
- an event with nonzero `period` repeats on its local period until stopped;
- a finite controller sequence can start a periodic child and stop it after a
  chosen number of periods.

## Temporary event gating

```python
amy.send(sequence=40, action='gate', duration=24, alignment_period=1)
```

This suppresses ordinary event dispatch from active executions of tag `40` for
24 ticks. Local phase continues, and dispatch resumes on the original phase.
Audio already ringing is not cut off. Sequence-control payloads remain active,
so a controller sequence can still complete its lifecycle. Duration zero
removes a gate at the selected boundary.

Gated ordinary events are skipped and are not replayed. That rule also applies
to note-offs and parameter-restoration events. Keep required cleanup outside
the interval or in a separately started finite gesture. Duration and alignment
must not exceed 2,147,483,647 ticks so their boundaries remain unambiguous
across the wrapping 32-bit tick clock.

## Reset behavior

- `amy.send(sequence_reset=tag)` removes the future definition. Active
  executions retain the snapshot they started with and may finish.
- `RESET_TIMEBASE` discards active or pending executions because their absolute
  activation ticks cannot be rebased, but retains stored definitions.
- `RESET_SEQUENCER` clears untagged events, tagged definitions, and executions.

## Capacity and realtime behavior

`max_sequencer_tags` bounds public tag identities. `max_sequence_events` bounds
the number of events in one definition, and `max_sequence_executions` bounds
active or alignment-pending executions. Definitions allocate only when used;
inactive definitions are not scanned on each tick.

See the [implementation model](sequencer-sequences-abstractions.md),
[musical use cases](sequencer-sequences-musical-use-cases.md), and
[step-by-step examples](sequencer-sequences-howto.md). The
[status and compatibility guide](sequencer-sequences-status.md) records the
intentional tagged-scheduling change, migration path, test coverage, and
target-dependent validation boundary.
