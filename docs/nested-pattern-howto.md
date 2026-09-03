# Stored-pattern how-to: two arpeggios and a live percussion mute

This example uses exact AMY wire messages. Send each line as one complete
message, including its final `Z`. AMY's sequencer runs at 48 ticks per quarter
note, so this example uses 24 ticks per eighth note and a 96-tick, four-note
phrase.

The arpeggio uses oscillator 0. Configure it first; a sine wave keeps the
example independent of a stored patch bank:

```text
v0w0Z
```

## 1. Preload an ascending arpeggio

Pattern 10 plays C4, E4, G4, and C5. Each note starts 24 ticks after the
previous one and has an 18-tick gate.

```text
zQB10,96Z
zQE10,0,96,0v0n60l1Z
zQE10,18,96,1v0l0Z
zQE10,24,96,2v0n64l1Z
zQE10,42,96,3v0l0Z
zQE10,48,96,4v0n67l1Z
zQE10,66,96,5v0l0Z
zQE10,72,96,6v0n72l1Z
zQE10,90,96,7v0l0Z
zQC10Z
```

`zQB` begins a private staging definition, each `zQE` adds one event on its
local timeline, and `zQC` publishes the complete definition atomically.

<details>
<summary>Python API equivalent</summary>

```python
import amy

amy.send(osc=0, wave=amy.SINE)
amy.pattern_begin(10, length_ticks=96)
amy.pattern_event(10, 0,  period=96, tag=0, osc=0, note=60, vel=1)
amy.pattern_event(10, 18, period=96, tag=1, osc=0, vel=0)
amy.pattern_event(10, 24, period=96, tag=2, osc=0, note=64, vel=1)
amy.pattern_event(10, 42, period=96, tag=3, osc=0, vel=0)
amy.pattern_event(10, 48, period=96, tag=4, osc=0, note=67, vel=1)
amy.pattern_event(10, 66, period=96, tag=5, osc=0, vel=0)
amy.pattern_event(10, 72, period=96, tag=6, osc=0, note=72, vel=1)
amy.pattern_event(10, 90, period=96, tag=7, osc=0, vel=0)
amy.pattern_commit(10)
```

</details>

## 2. Preload a descending arpeggio

Pattern 11 uses the same timing but reverses the pitches:

```text
zQB11,96Z
zQE11,0,96,0v0n72l1Z
zQE11,18,96,1v0l0Z
zQE11,24,96,2v0n67l1Z
zQE11,42,96,3v0l0Z
zQE11,48,96,4v0n64l1Z
zQE11,66,96,5v0l0Z
zQE11,72,96,6v0n60l1Z
zQE11,90,96,7v0l0Z
zQC11Z
```

<details>
<summary>Python API equivalent</summary>

```python
amy.pattern_begin(11, length_ticks=96)
amy.pattern_event(11, 0,  period=96, tag=0, osc=0, note=72, vel=1)
amy.pattern_event(11, 18, period=96, tag=1, osc=0, vel=0)
amy.pattern_event(11, 24, period=96, tag=2, osc=0, note=67, vel=1)
amy.pattern_event(11, 42, period=96, tag=3, osc=0, vel=0)
amy.pattern_event(11, 48, period=96, tag=4, osc=0, note=64, vel=1)
amy.pattern_event(11, 66, period=96, tag=5, osc=0, vel=0)
amy.pattern_event(11, 72, period=96, tag=6, osc=0, note=60, vel=1)
amy.pattern_event(11, 90, period=96, tag=7, osc=0, vel=0)
amy.pattern_commit(11)
```

</details>

## 3. Turn the ascending arpeggio on

Schedule pattern 10 as a one-shot every 96 ticks. The start is quantized to the
next 96-tick boundary, and root sequence tag 200 makes this future schedule
replaceable:

```text
zQA10,0,0,96,96,200Z
```

The arguments are:

```text
zQA pattern,mode,offset,period,quantize,sequence_tag Z
     10     0      0      96       96          200
```

Mode `0` is `ONE_SHOT`. The repeating object is the small root trigger; each
96-tick child instance is finite.

<details>
<summary>Python API equivalent</summary>

```python
amy.pattern_schedule(
    10,
    sequence_tag=200,
    mode=amy.AMY_PATTERN_ONE_SHOT,
    offset_ticks=0,
    period_ticks=96,
    quantize_ticks=96,
)
```

</details>

## 4. Switch to the descending arpeggio

Use the same root sequence tag. This replaces the future repeating trigger on
the next musical boundary; an ascending one-shot that already started retains
its note-offs and finishes its 96-tick lifetime.

```text
zQA11,0,0,96,96,200Z
```

<details>
<summary>Python API equivalent</summary>

```python
amy.pattern_schedule(
    11,
    sequence_tag=200,
    mode=amy.AMY_PATTERN_ONE_SHOT,
    offset_ticks=0,
    period_ticks=96,
    quantize_ticks=96,
)
```

</details>

## 5. Turn the arpeggio off

Clear root sequence tag 200 with the existing root-sequencer operation:

```text
H0,0,200Z
```

This removes future triggers. It does not stop a child that has already begun,
so the current note gate and phrase end remain intact. Sending the `zQA` command
from step 3 or 4 again turns the chosen arpeggio back on.

<details>
<summary>Python API equivalent</summary>

```python
amy.send(ticks="0,0,200")
```

</details>

For a single performance instead of a repeating arpeggio, trigger a committed
definition directly. This plays pattern 10 once at the next 96-tick boundary:

```text
zQT10,0,96Z
```

<details>
<summary>Python API equivalent</summary>

```python
amy.pattern_trigger(10, amy.AMY_PATTERN_ONE_SHOT, quantize_ticks=96)
```

</details>

## Live-mute one percussion instrument

Mute addresses a pattern **instance tag**, not a synth or note. Put an
independently controllable percussion instrument in its own small pattern. In
this example synth 10 is assumed to be configured as the desired percussion
kit, and MIDI note 42 is its closed hi-hat. Pattern 20 emits that hit every 24
ticks:

```text
zQB20,24Z
zQE20,0,24,0n42l1i10Z
zQC20Z
zQT20,1,24,300Z
```

Mode `1` starts a `LOOP`; instance tag 300 is the live control address. Other
percussion instruments should use other definitions and instance tags if they
must remain audible independently.

Suppose a MIDI footpedal handler has already converted pedal-down and pedal-up
into outgoing AMY commands. It need only send the following. On pedal-down,
start a deliberately long finite mute (over 100 days even at 300 BPM):

```text
zQM300,2147483647Z
```

On pedal-up, a zero duration clears that mute immediately:

```text
zQM300,0Z
```

The hi-hat pattern keeps advancing while muted and resumes on its original
24-tick phase. These are only AMY commands; reading and mapping the MIDI pedal
belongs to the controller application.

<details>
<summary>Python API equivalent</summary>

```python
# Preload and start the independently controllable hi-hat layer.
amy.pattern_begin(20, length_ticks=24)
amy.pattern_event(20, 0, period=24, tag=0,
                  synth=10, note=42, vel=1)
amy.pattern_commit(20)
amy.pattern_trigger(
    20,
    amy.AMY_PATTERN_LOOP,
    quantize_ticks=24,
    instance_tag=300,
)

# Pedal down, then pedal up.
amy.pattern_mute(300, 2147483647)
amy.pattern_mute(300, 0)
```

</details>

If the silence has a known musical duration, send that duration directly—for
example, `zQM300,192Z` suppresses the tagged layer for exactly four quarter
notes at 48 PPQ and then lets it resume automatically.
