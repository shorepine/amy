# Reusable sequence how-to

This example preloads two arpeggios, starts one, and switches to the other on a
musical boundary. Python is the primary interface; the equivalent wire
messages are collected afterward.

AMY's sequencer uses 48 ticks per quarter note. The example gives every note
an 18-tick gate and uses 48 ticks as its switching boundary.

## 1. Define complete note gestures

Store each note-on together with its note-off in a finite sequence:

```python
import amy

amy.define_sequence(20, [
    dict(ticks=(0,), synth=1, note=60, vel=1),
    dict(ticks=(18,), synth=1, note=60, vel=0),
])

amy.define_sequence(21, [
    dict(ticks=(0,), synth=1, note=64, vel=1),
    dict(ticks=(18,), synth=1, note=64, vel=0),
])
```

Both definitions contain only period-zero events. Each start therefore creates
a finite execution which retires after its tick-18 note-off.

## 2. Define two arpeggios

The slower arpeggio starts the two note gestures half a quarter note apart.
The faster one starts them an eighth note apart:

```python
amy.define_sequence(30, [
    dict(ticks=(0, 48), sequence=20,
         action='start', alignment_period=1),
    dict(ticks=(24, 48), sequence=21,
         action='start', alignment_period=1),
])

amy.define_sequence(31, [
    dict(ticks=(0, 24), sequence=20,
         action='start', alignment_period=1),
    dict(ticks=(12, 24), sequence=21,
         action='start', alignment_period=1),
])
```

The nonzero periods make these parent executions repeat until explicitly
stopped. A stored sequence may contain ordinary AMY events or controls for
other sequences.

## 3. Start and switch

```python
amy.send(sequence=30, action='start', alignment_period=48)

# Later: stop the old parent and start the new one at the same boundary.
amy.send(sequence=30, action='stop', alignment_period=48)
amy.send(sequence=31, action='start', alignment_period=48)
```

The stop prevents sequence 30 from launching another child at the selected
boundary. A note gesture launched before that boundary is an independent
execution, so it still sends its original note-off. The caller does not need
to mirror AMY's tick count or remember pending releases.

Start may be sent again while an earlier finite execution of the same tag is
active. Each execution has its own local start tick and immutable definition
snapshot.

## 4. Stop playback

```python
amy.send(sequence=31, action='stop', alignment_period=48)
```

Stopping a parent cancels its future child launches. Stopping a leaf such as
sequence 20 instead deliberately cancels the future events of every selected
active leaf execution, including any pending note-off. This lets the caller
choose between a graceful parent stop and explicit truncation.

<details>
<summary>Equivalent wire messages</summary>

`H<tick>,<period>,<tag><payload>Z` appends a normal event to a reusable
definition. `HR<tag>Z` resets future contents. `HC` uses action `0` for stop,
`1` for start, and `2` for gate.

```text
HR20Z
H0,0,20n60l1i1Z
H18,0,20n60l0i1Z

HR21Z
H0,0,21n64l1i1Z
H18,0,21n64l0i1Z

HR30Z
H0,48,30HC20,1,1Z
H24,48,30HC21,1,1Z

HR31Z
H0,24,31HC20,1,1Z
H12,24,31HC21,1,1Z

HC30,1,48Z
HC30,0,48Z
HC31,1,48Z
HC31,0,48Z
```

The final field of each `HC` message is the alignment period. Direct controls
with alignment `0` or `1` act on the next available sequencer tick; a larger
value selects the next global tick divisible by that value.

</details>

## Temporarily gate one layer

Suppose sequence 50 is a running periodic percussion layer. Suppress its
ordinary events for one quarter note without stopping its local clock:

```python
amy.send(
    sequence=50,
    action='gate',
    duration=48,
    alignment_period=1,
)
```

After 48 ticks, ordinary event dispatch resumes on the original phase. Audio
which was already ringing is not cut off. A zero-duration gate removes the
current gate at the selected boundary:

```python
amy.send(
    sequence=50,
    action='gate',
    duration=0,
    alignment_period=1,
)
```

<details>
<summary>Equivalent gate wire messages</summary>

```text
HC50,2,48,1Z
HC50,2,0,1Z
```

</details>

For the complete lifecycle and reset rules, see
[Reusable sequences](sequencer-sequences.md).
