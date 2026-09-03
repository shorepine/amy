# Sequencer-group how-to: switchable arpeggios and a percussion gate

This example sends complete AMY wire messages, including the final `Z`. AMY's
sequencer uses 48 ticks per quarter note, so the arpeggios use 24 ticks per
eighth note and a 96-tick phrase length.

The examples use `amy.send()` as the Python API. Each expandable section emits
the same wire message shown directly above it.

## 1. Configure a simple sound

Use oscillator 0 with a sine wave so the example does not depend on a stored
patch bank:

```text
v0w0Z
```

<details>
<summary>Python API equivalent</summary>

```python
import amy

amy.send(osc=0, wave=amy.SINE)
```

</details>

## 2. Preload an ascending arpeggio

Group 10 plays C4, E4, G4, and C5. Each note begins 24 ticks after the previous
one and has an 18-tick gate:

```text
H0,96,0,10v0n60l1Z
H18,96,1,10v0l0Z
H24,96,2,10v0n64l1Z
H42,96,3,10v0l0Z
H48,96,4,10v0n67l1Z
H66,96,5,10v0l0Z
H72,96,6,10v0n72l1Z
H90,96,7,10v0l0Z
zQ10,3,96Z
```

The fourth `H` value selects group 10. The third value is a local event tag,
not a root tag. These messages update private staging storage; publish action 3
makes the complete 96-tick revision visible atomically.

<details>
<summary>Python API equivalent</summary>

```python
amy.send(ticks=[0, 96, 0, 10], osc=0, note=60, vel=1)
amy.send(ticks=[18, 96, 1, 10], osc=0, vel=0)
amy.send(ticks=[24, 96, 2, 10], osc=0, note=64, vel=1)
amy.send(ticks=[42, 96, 3, 10], osc=0, vel=0)
amy.send(ticks=[48, 96, 4, 10], osc=0, note=67, vel=1)
amy.send(ticks=[66, 96, 5, 10], osc=0, vel=0)
amy.send(ticks=[72, 96, 6, 10], osc=0, note=72, vel=1)
amy.send(ticks=[90, 96, 7, 10], osc=0, vel=0)
amy.send(sequence_control=[10, amy.SEQUENCE_CONTROL_PUBLISH, 96])
```

</details>

## 3. Preload a descending arpeggio

Group 11 uses the same timing and reverses the pitches:

```text
H0,96,0,11v0n72l1Z
H18,96,1,11v0l0Z
H24,96,2,11v0n67l1Z
H42,96,3,11v0l0Z
H48,96,4,11v0n64l1Z
H66,96,5,11v0l0Z
H72,96,6,11v0n60l1Z
H90,96,7,11v0l0Z
zQ11,3,96Z
```

<details>
<summary>Python API equivalent</summary>

```python
amy.send(ticks=[0, 96, 0, 11], osc=0, note=72, vel=1)
amy.send(ticks=[18, 96, 1, 11], osc=0, vel=0)
amy.send(ticks=[24, 96, 2, 11], osc=0, note=67, vel=1)
amy.send(ticks=[42, 96, 3, 11], osc=0, vel=0)
amy.send(ticks=[48, 96, 4, 11], osc=0, note=64, vel=1)
amy.send(ticks=[66, 96, 5, 11], osc=0, vel=0)
amy.send(ticks=[72, 96, 6, 11], osc=0, note=60, vel=1)
amy.send(ticks=[90, 96, 7, 11], osc=0, vel=0)
amy.send(sequence_control=[11, amy.SEQUENCE_CONTROL_PUBLISH, 96])
```

</details>

## 4. Turn on the ascending arpeggio

Install a normal repeating root event. Every 96 ticks it starts group 10 once.
Root tag 200 gives that future schedule a replaceable identity:

```text
H0,96,200zQ10,1,1,0Z
zY1Z
```

The embedded control arguments are:

```text
zQ group,action,repeats,quantize Z
   10      1       1        0
```

Action 1 means start, and repeat value 1 makes each execution finite. The root
event supplies the repetition. Quantization is zero because the root event
already fires on the exact musical boundary; the group's local tick-zero event
is delivered on that same tick.

<details>
<summary>Python API equivalent</summary>

```python
amy.send(
    ticks=[0, 96, 200],
    sequence_control=[10, amy.SEQUENCE_CONTROL_START, 1, 0],
)
amy.send(sequencer_run=1)
```

</details>

## 5. Switch to the descending arpeggio

Replace root tag 200 with a start for group 11:

```text
H0,96,200zQ11,1,1,0Z
```

The next matching root boundary starts the descending revision. An ascending
execution that already began keeps its captured revision and reaches every
original note-off normally.

<details>
<summary>Python API equivalent</summary>

```python
amy.send(
    ticks=[0, 96, 200],
    sequence_control=[11, amy.SEQUENCE_CONTROL_START, 1, 0],
)
```

</details>

## 6. Turn the arpeggio off and on

Clear root tag 200 with the unchanged root-sequencer operation:

```text
H0,0,200Z
```

This prevents future starts. It does not stop an execution that has already
begun, so the current phrase finishes with its normal note gates. Re-send the
root message from step 4 or 5 to turn the selected arpeggio on again.

<details>
<summary>Python API equivalent</summary>

```python
amy.send(ticks=[0, 0, 200])
```

</details>

To play group 10 only once instead of installing a root schedule, start one
execution at the next 96-tick boundary:

```text
zQ10,1,1,96Z
```

<details>
<summary>Python API equivalent</summary>

```python
amy.send(
    sequence_control=[10, amy.SEQUENCE_CONTROL_START, 1, 96]
)
```

</details>

## 7. Gate one percussion instrument from a controller

An independently controllable percussion role needs its own group execution.
Assume synth 10 is already configured as a percussion instrument and MIDI note
42 produces the desired closed hi-hat. Group 20 triggers that hit every 24
ticks, and execution tag 300 is its live control address:

```text
H0,24,0,20i10n42l1Z
zQ20,3,24Z
zQ20,1,0,24,300Z
```

The start repeat value is zero, so the execution repeats indefinitely. Other
percussion roles should use separate groups and execution tags when they need
independent control.

Suppose a MIDI foot controller, switch, or other input has already been mapped
by the sending application. On press, it can apply a long finite event gate:

```text
zQ20,2,2147483647,0,300Z
```

On release, duration zero removes the gate immediately:

```text
zQ20,2,0,0,300Z
```

The gate suppresses future events from execution 300. It does not cut off a
sample that is already sounding, and the execution's clock continues. When the
gate is released, the hi-hat resumes on its original 24-tick phase. Reading the
controller and mapping it to these messages remain outside AMY.

<details>
<summary>Python API equivalent</summary>

```python
# Define and start the independently controllable hi-hat layer.
amy.send(ticks=[0, 24, 0, 20], synth=10, note=42, vel=1)
amy.send(sequence_control=[20, amy.SEQUENCE_CONTROL_PUBLISH, 24])
amy.send(
    sequence_control=[20, amy.SEQUENCE_CONTROL_START, 0, 24, 300]
)

# Controller press, then controller release.
amy.send(
    sequence_control=[20, amy.SEQUENCE_CONTROL_GATE, 2147483647, 0, 300]
)
amy.send(
    sequence_control=[20, amy.SEQUENCE_CONTROL_GATE, 0, 0, 300]
)
```

</details>

When the silence has a known musical duration, send that duration directly.
For example, `zQ20,2,192,0,300Z` suppresses four quarter notes at 48 PPQ and
then releases automatically without another controller message.
