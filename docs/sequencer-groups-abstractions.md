# Sequencer-group abstractions and implementation

AMY's root sequencer stores ordinary events on one global musical timeline.
Sequencer groups add one reusable, bounded phrase level below that timeline: a
root event can start a finite or repeating group of ordinary AMY events. They
do not add a drum machine, arpeggiator, song model, or scheduler hierarchy.

For concrete applications, see the [musical use cases](sequencer-groups-musical-use-cases.md).
For exact messages, see the [step-by-step how-to](sequencer-groups-howto.md).
The concise argument reference is in [Sequencer groups](sequencer-groups.md).

## The model

The model separates stored content, scheduled starts, and active playback:

| Object | Purpose | Lifetime |
| --- | --- | --- |
| Root sequencer event | Decides when a group starts | Existing `H` tick/period/tag semantics |
| Group tag | Selects one reusable definition slot | From 1 through the configured group capacity |
| Staging revision | Receives local event edits privately | Until published or cleared |
| Published revision | Supplies immutable content to future starts | Until replaced or cleared |
| Execution | Plays one captured revision | Until its repeat count completes or it is stopped |
| Execution tag | Optionally addresses live or pending executions | Supplied by the start operation |
| Local event tag | Replaces or clears one event in one group's staging revision | Scoped to that group only |

Root tags, group tags, execution tags, and local event tags are separate
identities. For example, replacing a tagged root event changes which phrase
will start in the future. It does not edit the phrase definition or shorten an
execution that has already started.

## Authoring and publication

The existing `ticks` tuple accepts an optional fourth value:

```text
tick,period,event_tag,group_tag
```

With a nonzero `group_tag`, the `H` message edits that group's private staging
revision instead of the root sequencer. The first edit after publication clones
the current published revision, so a host can replace only the local tags that
changed. A local tag is cleared with `tick=0,period=0`, exactly like a tagged
root event.

Because that pair means clear, an event at local tick zero must use a nonzero
period. Using the group length as its period is usually the clearest choice; a
finite execution still fires it only once per repetition.

Publication uses action 3 of the `sequence_control` family:

```text
zQ<group>,3,<length>Z
```

The length is explicit. AMY validates every staged event against it, then
publishes the complete revision atomically. Playback therefore never observes
a partly rewritten phrase. AMY does not infer a potentially expensive least
common multiple from event periods.

## Execution lifetime

A start captures the currently published revision. Its repeat value is:

- `1` for one performance;
- `N` for exactly N performances;
- `0` for indefinite repetition.

Editing, publishing, or clearing the group afterward affects future starts
only. Every active execution retains a reference to the revision it captured
and can deliver the note-offs or other closing events already stored in that
revision. This is the key guarantee for glitch-free live phrase changes.

Starts and stops can be quantized to the next multiple of a sequencer tick
interval. A zero quantization value means the next sequencer tick for a direct
command. When a root event starts a group, local tick zero is processed on that
same root tick.

An optional execution tag gives live playback a stable control identity. A new
start with the same group and execution tag replaces the matching execution at
the requested boundary. Untagged starts may overlap. Stop and gate operations
can address one execution tag or, when the tag is omitted, all executions of a
group.

## Finite event gates

Gate action 2 suppresses event dispatch for a duration while the execution's
local clock continues advancing. It does not stop already-sounding audio. When
the gate ends, the next event occurs at its original phase rather than at a
restarted phase. A zero duration releases a current gate.

A group may contain a gate control as a leaf event. This lets one finite phrase
temporarily suppress events from another tagged repeating layer. AMY assigns no
musical meaning to either layer; the controller owns that policy.

## Bounded scheduling

The root sequencer may start a group. A group may contain ordinary AMY events
and finite gate controls, but it cannot start, publish, or clear a group. This
provides the two useful musical levels—global arrangement and reusable
phrase—without cycles or variable scheduling depth.

The configured limits independently bound:

- persistent group slots;
- local event tags in each allocated definition;
- active or quantized-pending executions.

The portable defaults are 32 groups, 64 local tags per group, and 32 active or
pending executions. Definition storage is allocated only when a group is
authored. The audio-time tick path scans only the fixed execution pool, not all
stored groups, so an application can choose a larger definition catalogue
without making every inactive definition part of per-tick work.

## Implementation outline

The implementation in [`src/sequencer.c`](../src/sequencer.c) deliberately
reuses the normal event path:

- grouped `H` messages store the same wire payloads AMY already parses;
- staged and published definitions use fixed-capacity local-tag tables;
- published revisions are reference-counted and remain alive while captured by
  an execution;
- an independently bounded execution pool owns start phase, repeat count,
  execution identity, pending stop, and gate state;
- root events are processed before group events, which makes a root launch and
  its local tick-zero payload sample-clock coherent;
- group-to-group lifecycle operations are rejected while a grouped payload is
  firing.

The public configuration fields and constants are declared in
[`src/amy.h`](../src/amy.h). The group engine entry points are in
[`src/sequencer.h`](../src/sequencer.h), and Python uses the existing
`amy.send(ticks=...)` and `amy.send(sequence_control=...)` interface.

## Compatibility contract

An absent or zero fourth `ticks` value follows the existing root-sequencer path.
Existing three-field `H` messages, anonymous root events, tag replacement and
clear behavior, modulo periods, and `amy_add_event()` scheduling are unchanged.

`RESET_SEQUENCER` and `RESET_TIMEBASE` discard active and pending executions
but preserve published group definitions. Full AMY shutdown releases the
definitions.

The native group regression test exercises legacy root behavior and group
behavior in the same process. It covers the unchanged three-value C and wire
formats, root/group namespace isolation, one/N/infinite repetition,
quantization, tagged replacement, selective stop and gate, early ungate,
atomic publication, repair after rejected publication, immutable active
revisions, same-tick root launches, non-recursive lifecycle controls, allowed
leaf controls, resets, 32-bit clock rollover, disabled configuration, and
configured storage and execution bounds. The existing AMY C and audio suites
remain the broader backward-compatibility tests.
