# Reusable sequence how-to

This example preloads two simple arpeggios, launches them from the root
sequencer, and changes which one will launch without cutting short a note which
already started.

## 1. Define note-pair sequences

Each finite child owns its note-on and note-off:

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

The parents contain only starts of their note-pair children:

```python
amy.define_sequence(30, [
    dict(ticks=(0, 48),
         sequence_control=(20, amy.SEQUENCE_CONTROL_START, 1)),
    dict(ticks=(24, 48),
         sequence_control=(21, amy.SEQUENCE_CONTROL_START, 1)),
])

amy.define_sequence(31, [
    dict(ticks=(0, 24),
         sequence_control=(20, amy.SEQUENCE_CONTROL_START, 1)),
    dict(ticks=(12, 24),
         sequence_control=(21, amy.SEQUENCE_CONTROL_START, 1)),
])
```

Because these parents contain periodic events, they run until stopped.

## 3. Start the first arpeggio

```python
amy.send(sequence_control=(30, amy.SEQUENCE_CONTROL_START, 48))
```

The start is aligned to the next 48-tick boundary.

## 4. Switch parents

```python
amy.send(sequence_control=(30, amy.SEQUENCE_CONTROL_STOP, 48))
amy.send(sequence_control=(31, amy.SEQUENCE_CONTROL_START, 48))
```

Both controls select the same next boundary. The old parent starts no more
children there. A note-pair child which started earlier remains independent and
still sends its tick-18 note-off.

<details>
<summary>Equivalent low-level wire messages</summary>

The Python API above emits these sequence-authoring messages:

```text
HR20Z
HA20,0,0n60l1i1Z
HA20,18,0n60l0i1Z
HR21Z
HA21,0,0n64l1i1Z
HA21,18,0n64l0i1Z
HR30Z
HA30,0,48HC20,1,1Z
HA30,24,48HC21,1,1Z
HR31Z
HA31,0,24HC20,1,1Z
HA31,12,24HC21,1,1Z
HC30,1,48Z
HC30,0,48Z
HC31,1,48Z
```

`HA` is the explicit cumulative event form, `HR` resets the future contents of
one tag, and `HC` controls a tagged sequence. They are all part of the
sequencer-oriented `H` family. Existing `Htick,period,tag...` messages retain
their original replace-by-tag behavior.

</details>

## Temporarily gate one percussion layer

Suppose tag `50` is already running a periodic percussion sequence. A caller
can suppress its events for one quarter note at 48 PPQ without stopping its
clock:

```python
amy.send(sequence_control=(50, amy.SEQUENCE_CONTROL_GATE, 48, 1))
```

After 48 ticks the gate expires automatically and events resume on their
original phase. Duration zero removes a current gate explicitly:

```python
amy.send(sequence_control=(50, amy.SEQUENCE_CONTROL_GATE, 0, 1))
```

<details>
<summary>Equivalent low-level wire messages</summary>

```text
HC50,2,48,1Z
HC50,2,0,1Z
```

</details>

The source of these commands could be a foot pedal, UI, network controller, or
another sequence. AMY only sees generic tagged sequence control.
