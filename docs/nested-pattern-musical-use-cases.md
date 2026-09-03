# Musical use cases for stored patterns

Stored patterns are useful when a musical phrase should remain a coherent unit
while a controller changes *which* phrase will play next. Two representative
applications are an interactive rhythm engine with selectable drum fills and
an arpeggiator whose timing or direction can change during playback. Both are
expressed as ordinary AMY events on a local timeline; AMY contains no policy
specific to either application.

## Dynamic drum fills

Consider a rhythm engine that combines a repeating rhythm with a selectable
fill and a fill density. Each rhythm offers multiple fills, and the player may
change the selection or density while playback continues. During a fill,
selected percussion layers can be silent while other layers continue.

A flat sequencer can render any one final arrangement, but live editing makes
the host responsible for considerably more state. It must expand every chosen
fill into root events, determine which future events may safely be replaced,
coordinate the mute boundaries, avoid truncating the fill already in progress,
and resend a large schedule whenever the selection changes. Combining rhythms,
fill choices, densities, and independently gated layers multiplies those edge
cases even though each individual phrase is short.

Stored patterns move the phrase boundary into AMY:

1. The host preloads each fill once as a short `ONE_SHOT` definition.
2. A small repeating root event triggers the selected definition at a musical
   boundary.
3. The fill may contain a finite `zQM` leaf event for each background layer
   that should be suppressed during that fill.
4. Changing the root trigger changes only future fills. A fill that already
   started retains its committed definition and finishes normally.

The host still owns every musical choice—selection, density, instrument roles,
and which layers continue. AMY only provides coherent phrase playback. This
reduces live control from rewriting many individual sequencer events to
replacing or clearing one tagged root trigger.

A large rhythm engine may preload hundreds of fills and configure more
definition slots than AMY's conservative portable default. That does not imply
the same number of simultaneous players: stored definitions and active
instances have separate limits.

## Arpeggios with clean live changes

An arpeggio can also be written directly into the flat root sequencer. The
difficult part is changing rate, direction, or pitch while notes are already in
flight. Deleting the old root events can remove a scheduled note-off and leave
a note hanging. Sending an all-off avoids the hang but cuts a valid note short.
A host-side timer can defer the edit, but then the host must reproduce AMY's
musical clock and account for every overlapping note gate.

The arpeggiator can instead represent each sounding note as a short one-shot
containing its note-on and matching note-off. Root events decide which
one-shots will start in the future. When the player changes the arpeggio:

- future root triggers are replaced or cleared by tag;
- an already-started one-shot keeps its immutable definition;
- its matching note-off therefore remains present and occurs at the original
  gate time;
- the replacement starts on a quantized boundary.

The result is neither an abrupt stop nor a late, hanging note. Overlapping notes
remain possible because each trigger creates a separate playback instance.
Again, AMY does not know what an arpeggio is; the same lifetime rule applies to
any finite musical gesture.

## The common abstraction

The two applications share one structure:

```
root timeline:  choose when phrase A or phrase B starts
stored phrase:  play a coherent set of ordinary events once (or loop it)
```

Without this boundary, the controlling application must track and mutate the
expanded leaf events. With it, the application edits references to immutable
phrases and lets AMY's sequencer own their timing and completion. The gain is
therefore not that a flat sequence is unable to represent the music; it is that
the nested form keeps live musical changes atomic, compact, and independent of
host timing.

See the [step-by-step arpeggio and live-mute example](nested-pattern-howto.md)
for the corresponding wire commands.
