# Reusable sequence status and compatibility

This document records the implemented interface, the compatibility boundary,
and the validation which still depends on a particular target or downstream
application. It describes the reusable-sequence model in this source tree.

## Implemented interface

Python callers normally use named actions:

```python
amy.send(sequence=40, action='start', alignment_period=48)
amy.send(sequence=40, action='stop', alignment_period=48)
amy.send(sequence=40, action='gate', duration=24, alignment_period=1)
```

`amy.define_sequence(tag, events)` is the validated replace-as-a-list helper.
The corresponding lower-level fields are `sequence_reset` and
`sequence_control`. JavaScript and Godot bindings expose those lower-level
fields through the generated API.

The wire protocol uses:

| Operation | Wire shape | Meaning |
| --- | --- | --- |
| append | `Htick,period,tag<payload>Z` | Add an ordinary event to a definition |
| reset | `HRtagZ` | Clear the definition used by future starts |
| stop | `HCtag,0,alignmentZ` | Stop the selected executions |
| start | `HCtag,1,alignmentZ` | Create an execution |
| gate | `HCtag,2,duration,alignmentZ` | Temporarily suppress ordinary events |

The numeric action is deliberately a three-value action rather than a boolean
or a note velocity. Fractional values are rejected for every sequence tag,
tick, period, duration and alignment field. Tags, ticks and periods use uint32;
duration and alignment are capped at 2,147,483,647 ticks for wrap-safe pending
boundaries.

## Compatibility summary

| Existing use | Status | Required action |
| --- | --- | --- |
| Untagged `ticks=(tick,)` | Compatible | None |
| Untagged `ticks=(tick, period)` | Compatible | None |
| Repeated tagged writes used to replace one event | Changed | Reset and rebuild the definition, or omit the tag for direct scheduling |
| A tagged event expected to become active immediately | Changed | Start its sequence explicitly |
| Empty `H0,0,tagZ` used as cancellation | Compatible reset spelling | It still resets the future definition; stop an active execution separately |
| C code using `amy_config_t` | Source compatible after rebuild | Initialize with `amy_default_config()` and override named fields |
| Generated JavaScript or Godot bindings | Regeneration required | Rebuild the bindings with this AMY source |

The intentional breaking change is limited to tagged scheduling. A tag now
identifies a stopped, cumulative definition: repeated tagged writes append,
and playback begins only after an explicit start. This replaces two properties
of the earlier tagged-event behavior, where a later write replaced the event
and the tagged event was active immediately.

## Migrating a replaceable tagged event

If the tag was only being used as a replace/remove handle, the smallest
migration is to omit it and keep using direct one-off or periodic scheduling.

If the contents need to remain addressable as a reusable sequence, replace
them explicitly:

```python
amy.send(sequence=tag, action='stop', alignment_period=period)
amy.define_sequence(tag, events)
amy.send(sequence=tag, action='start', alignment_period=period)
```

The low-level wire equivalent is:

```text
HC<tag>,0,<period>Z
HR<tag>Z
H<tick>,<event-period>,<tag><payload>Z
...
HC<tag>,1,<period>Z
```

An aligned stop captures the executions which exist when the command is sent.
Replacing the definition changes future starts, while an execution which
already began retains its immutable snapshot. This lets a wrapper migrate
without tracking AMY's current tick, active note state, or definition version.
The wrapper must still choose its musical update boundary: replacing on the
next full period is simple and phase-stable, but may have more latency than an
application-specific mid-cycle update.

One known first-party consumer of the replace-on-tag behavior is Tulip's
`AMYSequenceEvent` wrapper. Its `update()` and `remove()` operations need the
explicit stop/reset/append/start lifecycle above. That migration is localized,
but its live-edit boundary is a product choice and should be tested together
with the consumers of that wrapper.

## Other source-compatibility details

`amy_config_t` appends `max_sequence_events` and
`max_sequence_executions`. Appending preserves the offsets of existing
members, but changing the size of a public C structure is not a binary ABI
promise. Applications should be recompiled against the matching header and
library. As with other AMY configuration, begin with `amy_default_config()` so
new fields receive supported defaults.

Limits are explicit. `max_sequencer_tags` bounds identities,
`max_sequence_events` bounds one definition, and
`max_sequence_executions` bounds active or alignment-pending executions.
Exhaustion, invalid tags, malformed actions, publication allocation failure,
and cyclic start graphs reject the affected operation without corrupting the
previously published generation. Callers which deliberately choose small
limits should treat a rejected operation as a normal bounded-resource failure.

A multi-message upload is not a wire-level transaction. `define_sequence()`
validates every Python event before sending its reset, but a target-side
capacity or transport failure during the subsequent messages can leave the
successfully accepted prefix as the new definition. A protocol which needs
acknowledged all-or-nothing remote upload must add that acknowledgement above
AMY's one-way wire command stream; after a detected failure, reset the tag
before retrying.

Resetting a definition does not stop an execution which already holds a
snapshot. `RESET_TIMEBASE` removes active and pending executions while
retaining definitions. `RESET_SEQUENCER` clears direct events, definitions,
and executions.

## Automated validation

The host test suite covers:

- unchanged one- and two-value direct scheduling;
- cumulative definitions, explicit reset, finite and repeating executions;
- overlapping executions and more than two simultaneously retained snapshot
  generations;
- same-tick control ordering, alignment, tick rollover, gate phase, and global
  reset behavior;
- current-execution capture for aligned stop and gate;
- arbitrary payloads, sequence composition, bounded cycles, and exhausted
  execution pools;
- allocation failure during pool initialization, new-definition creation and
  candidate cloning, with recovery and no partial single-event publication;
- two competing writers, including checked publication and retry;
- Python validation and exact wire serialization;
- executable JavaScript serialization and generated binding freshness.

The reusable-sequence C tests run as part of `make ctest`. Python API coverage
is in `tests/test_sequence_api.py`, and generated API checks are included in
`make check-c-api` and `make js-api-test`.

## Target-dependent validation still required

The ownership design keeps definition allocation, cloning, string copying,
and destruction off the render path and outside the shared render-lock
critical section. That is a source-level real-time property, not a substitute
for measuring a complete device.

On an ESP32 target, validate the intended sample rate, block and DMA sizes,
memory capabilities, effects load, and authoring traffic. Record maximum
render time, missed DMA deadlines, publication critical-section time, heap
low-water mark, largest free block, and maximum retired-list depth. At 48 kHz
and 128 samples, the block deadline is approximately 2.67 ms.

Generated Godot source is checked for freshness and syntax when the parser is
available. An executable Godot runtime behavior test remains target-dependent;
the sequence behavior itself is implemented in the common C core.

See [Abstractions and implementation](sequencer-sequences-abstractions.md) for
the snapshot publication and deferred-reclamation design.
