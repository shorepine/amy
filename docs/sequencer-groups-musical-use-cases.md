# Musical use cases for sequencer groups

Sequencer groups are useful when a musical phrase must remain a coherent unit
while a controller changes what will play next. Two representative applications
are an interactive rhythm engine with selectable drum fills and an arpeggiator
whose timing, direction, or notes can change during playback. Both are expressed
as ordinary AMY events on a local timeline; AMY contains no policy specific to
either application.

## Dynamic drum fills

Consider a rhythm engine that combines repeating percussion layers with a
selectable fill and a fill density. It may offer hundreds of short fills, let a
player change the active selection while transport continues, and temporarily
silence some background layers during a fill while allowing others to continue.

A flat root sequence can represent one final arrangement. Live editing is more
complicated: the host must expand every chosen fill into root events, identify
which future events are safe to replace, coordinate the background boundaries,
avoid truncating a fill already in progress, and resend a large schedule whenever
selection or density changes. Combining fills, densities, and independently
controlled background layers multiplies that state even though every individual
phrase is small.

Sequencer groups preserve the useful phrase boundary:

1. The controller preloads each fill once as a finite group.
2. A small tagged root event starts the selected group at a musical boundary.
3. Independently controllable background roles run as tagged repeating group
   executions.
4. A fill can contain finite gate events for background executions that should
   not dispatch events during that fill.
5. Replacing or clearing the root event changes future fills only. A fill that
   already started retains its immutable revision and finishes normally.

The controller still owns every musical choice: fill selection, density,
instrument roles, and which roles continue. AMY only provides reusable phrase
storage, coherent execution, and generic event gating. Live control therefore
changes a small reference instead of rewriting the expanded leaf-event schedule.

Stored definitions and active executions have independent limits. A rhythm
engine can configure enough group slots for a large fill catalogue without
creating hundreds of live players or scanning every stored fill on each tick.

## Arpeggios with clean live changes

An arpeggio can also be expanded into the root sequencer. The difficult part is
changing rate, direction, pitch, or voicing while notes are already in flight.
Deleting old root entries can remove a future note-off and leave a note hanging.
Sending an immediate all-off prevents the hang but shortens a valid note. A
host-side timer can defer the edit, but then the host must mirror AMY's musical
clock and track the lifetimes of overlapping phrases.

Instead, one group revision stores the complete arpeggio phrase, including every
note-on and its matching note-off. Tagged root events determine when that phrase
starts. When a player changes the arpeggio:

- the controller stages and atomically publishes the complete replacement;
- future starts capture the new published revision;
- an execution already sounding retains its previous immutable revision;
- every release in that execution therefore occurs at its original gate;
- quantized root starts preserve the musical boundary;
- untagged executions may overlap when a new phrase starts before an older one
  has finished.

The result avoids both abrupt releases and delayed hanging notes. AMY does not
know that the event collection is an arpeggio; the same lifetime guarantee
applies to any finite musical gesture.

## Independently controlled repeating layers

A drum voice, ostinato, control phrase, or other repeating part can run as an
independently tagged group execution. A controller can stop it at a quantized
boundary or gate future event dispatch without stopping the sequencer, changing
the phase, or affecting unrelated layers.

For example, a foot controller can gate the event stream that triggers one
percussion instrument. Pedal-down suppresses future hits for that tagged
execution, while a sample already sounding ends naturally. Pedal-up releases the
gate and the next hit occurs on the layer's original phase. Reading the pedal and
choosing the execution tag remain responsibilities of the controller application.

## The common abstraction

All three applications share the same structure:

```text
root timeline:       decide when a stored phrase starts
group definition:    store a coherent local event sequence
group execution:     play one immutable revision with a bounded lifetime
execution control:   start, stop, or temporarily gate that playback
```

A flat sequence can ultimately represent the same notes. The group boundary is
valuable because it makes live changes atomic, compact, and independent of host
timing. It moves phrase completion and release ownership into AMY without moving
application-specific musical policy into the synthesizer.

See the [step-by-step arpeggio and percussion-gate example](sequencer-groups-howto.md)
for the corresponding wire commands and Python calls.
