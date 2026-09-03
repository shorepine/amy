# Stored-pattern abstractions and implementation

AMY's root sequencer stores individual events on one global musical timeline.
Stored patterns add one reusable level below that timeline: a root event can
start a finite or looping collection of ordinary AMY events. They do not add a
drum machine, arpeggiator, arrangement model, or recursive sequencer.

For a worked example, see the [stored-pattern how-to](nested-pattern-howto.md).
For the applications that motivated the facility, see [musical use
cases](nested-pattern-musical-use-cases.md).

## The model

There are five distinct objects in the model:

| Object | Purpose | Lifetime |
| --- | --- | --- |
| Root sequencer event | Decides *when* a phrase starts | Existing `H` tick/period/tag semantics |
| Pattern definition | Stores the phrase's events on a local timeline | Until replaced or cleared |
| Staging definition | Receives edits before they become visible | From `begin` until `commit` or `clear` |
| Playback instance | Plays one committed definition as a one-shot or loop | Until it finishes or is stopped |
| Instance tag | Addresses running instances for replacement, stop, or mute | Supplied when an instance is triggered |

The event tag inside a definition, the root sequence tag, and the instance tag
are deliberately separate:

- An **event tag** edits or clears one event within the staging definition.
- A **root sequence tag** edits or clears a future/repeating trigger in the
  existing root sequencer.
- An **instance tag** addresses playback that has already been armed or is
  running.

Keeping these identities separate lets a host replace future arpeggio triggers
without truncating the arpeggio that is currently sounding.

## Definition and playback lifecycle

Definitions are constructed using `begin`, one or more `event` operations, and
`commit`. `begin` creates private staging storage. `commit` publishes that
complete version atomically; playback can never observe a half-written phrase.

A trigger captures the currently committed definition. Replacing or clearing
the slot later affects new triggers only. An instance already using the old
definition retains it until that instance ends. This immutability is what makes
live changes predictable: a host changes the next phrase, not the tail of the
current phrase.

The same definition supports two playback modes:

- `ONE_SHOT` plays local ticks `0` through `length - 1` once.
- `LOOP` wraps the local timeline at `length` until stopped.

Playback may start or stop on a requested multiple of the global sequencer
tick. A trigger fired from a root event can start on that exact root tick, so
the child's local tick-zero events remain sample-clock driven without a host
timer.

## Why the nesting limit is one level

The root sequencer may trigger a stored pattern. A stored pattern may contain
ordinary AMY events and the finite mute leaf operation, but it may not contain
`H` or an operation that starts or schedules another stored pattern.

That gives two musical levels—arrangement and phrase—which cover the motivating
uses. Refusing a third level prevents cycles and makes the number of active
players, their lifetimes, and the work performed on every tick bounded by the
configuration. This is intentionally composition, not recursion.

## Scheduling and mute

`pattern_schedule` / `zQA` installs a normal tagged event in the root
sequencer. Its offset is relative to the next requested quantization boundary.
Giving it a period makes that root trigger repeat; clearing its root sequence
tag with the existing `H0,0,<tag>Z` operation prevents future instances from
starting without cutting off an instance that has already begun.

`pattern_mute` / `zQM` temporarily suppresses events emitted by every running
instance with a matching instance tag. The local clocks continue to advance,
so playback resumes at its original phase after the duration. It is a generic
gate over a sequenced layer, not a synth mute and not a musical priority rule.
Applications choose which events belong to independently controllable layers.

## Wire and API surface

All added wire operations are grouped under the extended `zQ` family:

| Wire operation | API operation | Meaning |
| --- | --- | --- |
| `zQB` | `pattern_begin` | Begin a staging definition |
| `zQE` | `pattern_event` / `pattern_event_wire` | Add an ordinary event with local tick/period/tag metadata |
| `zQC` | `pattern_commit` | Publish the staged version atomically |
| `zQT` | `pattern_trigger` | Start a one-shot or loop |
| `zQA` | `pattern_schedule` | Put a pattern trigger in the root sequencer |
| `zQS` | `pattern_stop` | Stop matching tagged instances, optionally quantized |
| `zQM` | `pattern_mute` | Suppress matching instances for a finite number of ticks |
| `zQR` | `pattern_clear` | Remove staged/current definitions |

`zQE<pattern>,<tick>[,<period>[,<tag>]]<event>Z` intentionally mirrors the
root sequencer's `tick,period,tag` event model. `H` remains unchanged and keeps
its existing special parsing rules.

The public C functions are declared in [`src/sequencer.h`](../src/sequencer.h),
and the Python wrappers are in [`amy/__init__.py`](../amy/__init__.py). The full
argument reference is in [`api.md`](api.md#nested-pattern-c-api).

## Implementation outline

The implementation in [`src/sequencer.c`](../src/sequencer.c) reuses the root
sequencer's internal event record for each local definition. The main pieces
are:

- A fixed array of pattern slots points to staged and committed definitions.
- Each authored definition owns a fixed-capacity tagged event table plus the
  same bounded anonymous-event pool used by the event model.
- A separately bounded player pool holds only active or pending instances.
- Committed definitions are reference-counted, so retired versions remain
  valid while an instance still uses them.
- Occupied-event lists keep per-tick work proportional to active content rather
  than configured tag capacity.
- Root events are processed before child events on each tick, allowing a root
  trigger and the child's local tick zero to occur on the same tick.

The portable defaults are 32 definition slots, 64 local event tags per
definition, and 32 active or pending instances. These are independent limits
in `amy_config_t`; `max_patterns=0` disables the feature. Slots and players are
allocated at initialization, while a definition's event storage is allocated
only when that definition is begun.

## Compatibility contract

The feature is opt-in. Existing `H` messages and C `amy_add_event()` tick
scheduling retain their parsing and execution paths. `RESET_SEQUENCER` clears
root playback and pattern instances but preserves definitions; `RESET_TIMEBASE`
rebases running and pending instances without changing their local phase.

[`tests/test_nested_sequencer.c`](../tests/test_nested_sequencer.c) runs legacy
root-sequencer behavior and the new behavior in the same native test process.
It covers legacy modulo timing, repeat, tag replacement/clear, anonymous-event
coexistence and absolute C-API delivery, as well as one-shot/loop timing,
quantization, immutable replacement, reset, rollover, nesting rejection, mute,
and configured bounds.
