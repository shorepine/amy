# Control outputs: a synth whose notes are CV/gate or MIDI

**Status: proposal.** Nothing here is implemented yet. It is written up
as a PR so the shape can be argued over before any of it is built.

## The problem

AMY plays notes on oscillators. On a board with a CV DAC, a gate pin or
a MIDI port, you frequently want a note to go *somewhere else* instead:
out to a modular rack as 1V/oct plus a gate, or out of the MIDI port as
a note-on with velocity. Today AMY can do a fraction of that, in a way
that cannot be configured, and the rest has to be reimplemented in every
host.

What this proposes is one idea: **a synth can have its note events sent
to a control output instead of to oscillators.** Which output, and what
kind, is a property of the synth.

```python
amy.send(synth=1, synth_control=amy.CV_GATE, synth_control_channel=(0, 2))
amy.send(synth=1, note=60, vel=1)     # 3.0 V on CV out 0, gate out 2 high
amy.send(synth=1, note=60, vel=0)     # gate low, CV held at 3.0 V

amy.send(synth=2, synth_control=amy.MIDI_OUT, synth_control_channel=6)
amy.send(synth=2, note=60, vel=0.8)   # 0x95 0x3C 0x66 out the MIDI port
```

Everything else about those synths is unchanged: they are sequenced with
`ticks=`, they respond to `to_synth`, they take a `synth_delay_ms`, they
can be muted. A pattern that plays an internal instrument plays an
external one by changing one line.

## What AMY has today, and why none of it is this

**`wave=AMY_MIDI` (wave type 16).** An oscillator with this wave sends a
note-on out the MIDI port when it is played (`amy_send_midi_note_on`,
`amy.c:1504`) and a note-on-with-velocity-0 when it is released
(`amy.c:1990`). It works, and it has three problems that are all the
same problem:

- **The channel is hardcoded.** `bytes[0] = 0x90` — channel 1, always.
  There is nowhere for a channel to live, because the thing carrying the
  setting is an *oscillator*, and an oscillator has no channel.
- **It costs an oscillator to make no sound.** The osc is allocated,
  rendered around, and counted against the pool, to emit three bytes.
- **It is the wrong granularity.** "Where do this synth's notes go" is a
  question about a synth. Answering it per-osc means a polyphonic
  instrument has to answer it once per voice per osc, identically, and
  nothing stops the answers disagreeing.

**`ig` / `cv_trigger.c`** is the same idea pointed the other way: a CV
*input* crossing a threshold fires an arbitrary wire message, with
optional 1V/oct pitch from a second CV input. It is good and this
proposal is deliberately its mirror image — see "Symmetry with `ig`"
below. It does not do the out direction and is not trying to.

**`W` / `external_channel`** is a reserved wire letter with a comment and
no implementation (`parse.c:877`), set aside for a host driving a CV
output from an oscillator's *signal* — an LFO to a CV jack. That is a
real and different feature: continuous modulation, not note events. It
should use the same output hook and the same channel space as this
proposal, and the two compose (one synth's notes drive pitch and gate,
one osc's output drives a modulation jack). This proposal does not claim
`W`.

**`SYNTH_FLAGS_NOTES_VIA_MIDI`** is the closest existing precedent and is
worth reading before reviewing this. A synth with that flag has its
note-ons routed back *into* AMY's own MIDI handler so per-note command
mappings fire (`patches.c:1256`) — this is how drum kits work. It is the
opposite direction, but it establishes the thing that matters here:
**AMY already has synths whose notes do not reach oscillators, and
already has the branch point where that is decided.** This proposal adds
one more arm to that branch rather than new machinery.

## The proposal

### The model

A synth gains one setting with its parameters:

| | |
|---|---|
| `synth_control` | `OFF` (0), `CV_GATE` (1), `MIDI_OUT` (2) |
| `synth_control_channel` | which host output(s); meaning depends on the mode |

`OFF` is the default and is every synth that exists today.

A synth with a non-`OFF` `synth_control` **consumes no voices and no
oscillators.** Its note events are intercepted in
`patches_event_has_voices`, at the same place `NOTES_VIA_MIDI` is
intercepted, *before* `patches_voices_for_event` runs. So there is
nothing to enforce: `num_voices` and `oscs_per_voice` are never
consulted, rather than being clamped to 1.

### AMY has no opinion about what a control output *is*

`synth_control_channel` for `CV_GATE` is an index into a flat list of
**control outputs** the host provides. AMY passes the index and a value
in volts to a hook and stops caring. Whether output 2 is a second DAC
channel, a GPIO driven to 0 or 5 V, or an opto-isolated trigger is the
host's business entirely.

That is what makes "use a CV output as a gate" free rather than a
feature: a gate *is* a control output driven to a voltage. A host with
two DAC channels and one gate pin publishes three control outputs; a
host with four DAC channels and no gate pin publishes four, and you can
put the gate on any of them.

### Wire

The synth-layer namespace (`i` plus a second letter) is where synth
properties already live — `if` flags, `iv` voices, `iV` level, `ig` the
CV trigger. This adds one letter to it:

```
i<synth>iG<mode>[,<ch0>[,<ch1>[,<ch2>]]][,<opts>...]
```

```
i1iG1,0,2            CV_GATE: pitch on control out 0, gate on control out 2
i1iG1,0,2,3          ...and velocity as a voltage on control out 3
i1iG2,6              MIDI_OUT on channel 6
i1iG0                off; the synth goes back to being an ordinary synth
```

**`iG` deliberately pairs with `ig`.** Lower case is CV in, upper case is
control out. AMY already uses case this way in the same namespace (`iv`
/ `iV`, `im` / `iM`), so this reads as the convention rather than as a
coincidence.

**One command, not two**, because the mode and its channels have to
arrive together. Split across two commands there is a window in which a
synth is `MIDI_OUT` holding CV channel numbers, and a wire protocol
should not have a state like that in it.

The optional tail carries the tuning, defaulted so the common case never
writes it:

```
i1iG1,0,2,,12,24,5.0
                 │  │  └── gate high volts (default 5.0)
                 │  └───── MIDI note at 0 V (default 24)
                 └──────── semitones per volt (default 12)
```

### Python

```python
amy.send(synth=1, synth_control=amy.CV_GATE, synth_control_channel=(0, 2))
amy.send(synth=1, synth_control=amy.MIDI_OUT, synth_control_channel=6)
amy.send(synth=1, synth_control=amy.OFF)
```

A tuple maps most directly onto the wire's comma list. It is also the
least readable part of this proposal — `(0, 2)` does not say which is
pitch and which is the gate — so the alternative is worth arguing:

```python
amy.send(synth=1, synth_control=amy.CV_GATE, cv_pitch_out=0, cv_gate_out=2)
```

more kwargs, but nobody has to remember an order. See open questions.

### One new hook

```c
// A control synth is putting `value` (VOLTS) on host control output
// `channel`. Called from the render thread when the note event is
// applied, so it lands at block resolution rather than at whatever
// rate the host polls. Keep it to a register write or a mailbox post.
void (*amy_external_cv_output_hook)(uint8_t channel, float value);
```

That is the whole C-side addition. **`MIDI_OUT` needs no new hook at
all** — `midi_out()` and `amy_external_midi_output_hook` already exist
and already reach every port a host has wired up.

Volts rather than a normalised value, for two reasons: 1V/oct is defined
in volts, and `cv_inputs[]` is already in volts, so a host's scaling code
is the same in both directions.

`AMY_MAX_CV_OUT` (proposed default 4) range-checks the channel and
complains rather than clamping silently. Hosts with more outputs raise
it, the way `AMY_MAX_CV_IN` works today.

### Note semantics

**Note-on, `CV_GATE`:** pitch voltage first, then gate high. In that
order and not the other way round, so nothing downstream is told to look
at a voltage that is still moving.

**Note-off, `CV_GATE`:** gate low, **pitch held**. Holding it is what a
hardware sequencer does and what makes a slide into the next note
possible; zeroing it would put a click on every release.

**Monophonic, last-note priority, legato.** One pitch output can carry
one note. A note-on while another is held moves the pitch and **leaves
the gate high** — which is exactly how a mono synth gives you slides for
free. The gate falls only when the last held note is released. A
note-off for a note that is no longer the sounding one changes nothing.
This is classic mono-synth behaviour and is specified here because it is
precisely the sort of thing that is ambiguous until somebody hits it.

**`MIDI_OUT` is polyphonic** and needs none of the above: MIDI carries
several notes on one channel, so notes pass through one for one.

**Velocity.** `MIDI_OUT` sends it as the velocity byte, as
`amy_send_midi_note_on` already does. `CV_GATE` sends it as a voltage on
the optional third channel, scaled 0..1 to 0..gate-high volts, and
sends nothing if no third channel was given.

**Notes that arrived over MIDI are not forwarded by default.** The
existing code guards on `s_note_source_channel` being unset so a note
coming in one port does not immediately go back out another. That guard
is kept, and made a flag, because MIDI-thru with a transpose in the
middle is a real thing somebody will want.

### 1V/oct, and symmetry with `ig`

`cv_trigger` turns an input voltage into a note with

```
note = volts * pitch_scale + pitch_offset
```

so the out direction is exactly its inverse:

```
volts = (note - pitch_offset) / pitch_scale
```

with the same two numbers spelled the same way. **A host that patches a
control output into a CV input and gives both ends the same scale and
offset gets the note back unchanged**, which is a property worth having
deliberately: it makes the whole feature testable on one board with one
patch cable, and it is an assertion a test suite can make.

Defaults `pitch_scale = 12` (semitones per volt) and `pitch_offset = 24`
(MIDI note 24, C1, at 0 V). Eurorack has no universal convention for the
offset, so that number is a guess at the commonest one and is the first
thing to argue about.

### Raw MIDI

For a `MIDI_OUT` synth, a way to put arbitrary bytes on the same port:

```python
amy.send(synth=2, raw_midi=(176, 74, 64))    # CC 74 = 64
```

**Verbatim, with no rewriting.** The obvious convenience — replace the
status byte's low nibble with the synth's channel, so the app does not
repeat itself — is deliberately not proposed: a `raw` escape hatch that
silently edits a byte is not an escape hatch, and the caller set the
channel and therefore knows it. Sysex and realtime messages then pass
through for free, which a nibble rewrite would have had to special-case.

This is the one part of the proposal that is arguably not about synths
at all — "put these bytes on the MIDI port" needs no synth — and could
equally be a top-level command. Binding it to a synth is what gives it a
port and a place to live. See open questions.

## Why the synth layer

Because the question is "where do this synth's notes go", and a synth is
the only object in AMY that can answer it. The wave-type approach put
the answer on an oscillator, which is why it could not hold a channel,
and why it spent an oscillator to make no sound.

It also composes with everything AMY already does with synths for free:
the sequencer addresses synths, `to_synth` routes between them,
`synth_delay_ms` delays them, patches define them, `amy_dump_state`
serialises them. A control output that is a synth property is reachable
by all of that on the day it lands.

## What it replaces

`wave=AMY_MIDI` is superseded completely: `synth_control=MIDI_OUT` does
everything it did, on a channel you choose, without an oscillator.

Removing it is a breaking change for anyone using wave 16 — believed to
be nobody, since the hardcoded channel makes it hard to use for
anything real — so the proposal is to delete the two call sites and keep
**constant 16 reserved and unused**, rather than recycling it into some
future wave. A recycled wave number is a silent wrong sound in old
stored patches and wire strings, which is the worst failure a patch
format has.

`amy_send_midi_note_on` / `_off` stay as the implementation of
`MIDI_OUT`, gaining a channel argument.

## Open questions

1. **Channel numbering: 0-based.** The sketch this came from used
   1-based (`cv1=1, cv2=2, gate=3`). `ig`'s `trigger_cv` indexes
   `cv_inputs[]` from 0, and having the in and out directions disagree
   about the first channel's number is exactly the off-by-one that costs
   somebody an afternoon. Proposed 0-based for both; worth confirming.
2. **The Python surface**: one `synth_control_channel` tuple, or named
   `cv_pitch_out` / `cv_gate_out` / `cv_vel_out` kwargs?
3. **`pitch_offset` default** — is 24 the right convention?
4. **Polyphonic CV.** Left out of this proposal. The natural extension is
   voice *i* taking pitch channel `pitch + i` and gate channel
   `gate + i`, which is how a polyphonic CV interface is normally wired,
   and the arithmetic is worth agreeing now even if it is not built.
5. **Does `iG` on a synth that already holds voices free them?** It
   should, but that interacts with patch loading in ways worth checking.
6. **Does `raw_midi` belong on a synth**, or is it a top-level "send
   these bytes" command with the synth binding as sugar over it?
7. **Naming.** `synth_control` / `CV_GATE` / `MIDI_OUT` are the sketch's
   names and they read well. Note that `amy.set_cv_from_osc()` already
   exists and means CV *input* from a mod osc (test support), so
   anything new should avoid that phrasing entirely.

## Implementation sketch

1. `synth_control`, `synth_control_channel[3]` and the tuning triple on
   the instrument struct; `synth_control` in `amy_event`.
2. `iG` in `amy_parse_synth_layer_message`, beside `ig`.
3. The interception in `patches_event_has_voices`, as a third arm of the
   branch that already picks out `SYNTH_FLAGS_NOTES_VIA_MIDI`.
4. Mono note-stack state per control synth for last-note priority.
5. `amy_external_cv_output_hook` in `amy_config_t`, `AMY_MAX_CV_OUT`.
6. A channel argument on `amy_send_midi_note_on` / `_off`; delete the
   two `case AMY_MIDI:` call sites.
7. `amy_dump_state` / `yield_synth_commands` emit `iG`, so a control
   synth survives a state round trip.
8. Tests: the loopback identity above, and that a control synth
   allocates no oscillators.
