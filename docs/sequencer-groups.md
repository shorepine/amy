# Sequencer groups

Sequencer groups are reusable collections of ordinary AMY sequencer events.
They add one bounded level below the existing root sequencer: a root event may
start a group, but a group cannot start another group.

This is useful when a musical controller needs to trigger a complete phrase
as one operation. Examples include a drum fill, a short arpeggio with its own
note-on and note-off, or a repeating percussion layer. The controller can
preload these phrases and later send one small, quantized control message. It
does not need to reproduce AMY's clock or resend every event at performance
time.

## Defining and publishing a group

The normal `ticks` tuple accepts an optional fourth value:

```text
tick,period,event_tag,group_tag
```

`group_tag` values start at 1. An absent or zero group tag uses the existing
root sequencer without changing any of its semantics.

This wire sequence stages a four-beat phrase in group 1 and then publishes it
atomically with a length of 192 ticks:

```text
H0,192,0,1i2n60l1Z
H24,192,1,1i2n60l0Z
H48,192,2,1i2n64l1Z
H72,192,3,1i2n64l0Z
zQ1,3,192Z
```

The equivalent Python calls are:

```python
amy.send(ticks="0,192,0,1", synth=2, note=60, vel=1)
amy.send(ticks="24,192,1,1", synth=2, note=60, vel=0)
amy.send(ticks="48,192,2,1", synth=2, note=64, vel=1)
amy.send(ticks="72,192,3,1", synth=2, note=64, vel=0)
amy.send(sequence_control=[1, amy.SEQUENCE_CONTROL_PUBLISH, 192])
```

Grouped `ticks` commands update a private staging revision. Publishing is one
action in the generic control family rather than a separate begin/add/commit
API. It makes all staged local-tag replacements visible together, so a launch
can never observe a half-updated phrase. As at the root, `tick=0,period=0`
clears the specified event tag. Use a nonzero period for an event at local tick
zero.

The published length is explicit and bounded; AMY does not derive it using an
LCM of event periods. Within each phrase, a nonzero event period repeats by
local modulo and a zero period fires once at its local tick.

## Controlling executions

The control layout is fixed:

```text
group,action,value,quantize[,execution_tag]
```

| Action | Number | Meaning of `value` |
|---|---:|---|
| stop | 0 | reserved; use 0 |
| start | 1 | repeat count: 1 once, N exactly N times, 0 indefinitely |
| gate | 2 | suppress group-event firings for this many ticks; 0 releases a gate |
| publish | 3 | explicit group length in ticks |
| clear | 4 | reserved; use 0 |

`quantize=0` means the next sequencer tick for a direct command. Otherwise the
control takes effect at the next multiple of that many ticks. When a root
sequencer event issues the control on the boundary itself, it takes effect on
that same tick, including the group's local tick-zero events.

For example, start group 1 indefinitely at the next 192-tick boundary, assign
execution tag 100, and later stop that execution at a boundary:

```text
zQ1,1,0,192,100Z
zQ1,0,0,192,100Z
```

```python
amy.send(sequence_control=[1, amy.SEQUENCE_CONTROL_START, 0, 192, 100])
amy.send(sequence_control=[1, amy.SEQUENCE_CONTROL_STOP, 0, 192, 100])
```

Omit `execution_tag` to address every active execution of the group for stop
or gate operations. Supplying a tag to start makes a later start with the same
group and execution tag replace it on the requested boundary. Untagged starts
may overlap, which is useful for one-shot note phrases whose releases must be
allowed to finish independently.

A finite gate advances the execution's local clock but suppresses its event
firings. Audio already sounding is not stopped, and the first event after the
gate occurs at its original phase. A gate can itself be placed in another
group as a leaf control; start, publish and clear are rejected while a group
payload is firing, preventing recursive nesting.

## Scheduling a launch at the root

Because `sequence_control` is an ordinary wire command, it can be the payload
of a normal root `ticks` event. This starts group 1 once at absolute tick 960:

```text
H960,0,40zQ1,1,1,0Z
```

A repeating root entry can launch the same group sparsely without copying its
events. Clear that future launch with the unchanged root operation
`H0,0,40Z`; an execution already started from it keeps running.

## Lifetime and memory guarantees

An active execution retains the immutable published revision it started with.
Editing, publishing or clearing the group affects future starts only. This is
important for phrases containing releases: an old note-off cannot disappear
because a new definition was loaded while it was sounding.

`RESET_SEQUENCER` and `RESET_TIMEBASE` discard active and quantized-pending
executions but preserve published group definitions. Full AMY shutdown frees
them.

Storage and work are bounded by `max_sequence_groups`,
`max_sequence_group_tags` and `max_sequence_group_executions` in
`amy_config_t`. Group event arrays and wire payloads are allocated only for
definitions that are authored. The tick path scans only the fixed active
execution pool; inactive stored groups are not visited, and starting an
execution does not allocate memory.
