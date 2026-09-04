# Reusable sequence how-to

This example preloads two arpeggios and switches between them without cutting
short a note which already started.

## 1. Define note-pair sequences

```python
amy.define_sequence(20, [
    dict(ticks=(0,), synth=1, note=60, vel=1),
    dict(ticks=(18,), synth=1, note=60, vel=0),
])
amy.define_sequence(21, [
    dict(ticks=(0,), synth=1, note=64, vel=1),
    dict(ticks=(18,), synth=1, note=64, vel=0),
])
```

## 2. Define two arpeggio parents

```python
amy.define_sequence(30, [
    dict(ticks=(0, 48), sequence=20, vel=1, alignment_period=1),
    dict(ticks=(24, 48), sequence=21, vel=1, alignment_period=1),
])

amy.define_sequence(31, [
    dict(ticks=(0, 24), sequence=20, vel=1, alignment_period=1),
    dict(ticks=(12, 24), sequence=21, vel=1, alignment_period=1),
])
```

The parents contain periodic events and run until stopped.

## 3. Start and switch

```python
amy.send(sequence=30, vel=1, alignment_period=48)

# Later, switch both parents at the same boundary.
amy.send(sequence=30, vel=0, alignment_period=48)
amy.send(sequence=31, vel=1, alignment_period=48)
```

The old parent starts no more children at that boundary. A note-pair child
started earlier remains independent and still sends its tick-18 note-off.

<details>
<summary>Equivalent low-level wire messages</summary>

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
```

Ordinary `Htick,period,tag...` messages cumulate behind the tag. `HR` resets
one definition and `HC` controls its executions.

</details>

## Temporarily gate one percussion layer

Suppose tag `50` is already running a periodic percussion sequence. A caller
can suppress its events for one quarter note at 48 PPQ without stopping its
clock:

```python
amy.send(sequence_control=(50, amy.SEQUENCE_CONTROL_GATE, 48, 1))
```

After 48 ticks the gate expires and events resume on their original phase.
Duration zero removes a current gate explicitly:

```python
amy.send(sequence_control=(50, amy.SEQUENCE_CONTROL_GATE, 0, 1))
```

The equivalent wire messages are `HC50,2,48,1Z` and `HC50,2,0,1Z`. Their
source may be a foot pedal, UI, network controller, or another sequence; AMY
only sees generic tagged sequence control.
