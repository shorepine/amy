# AMY as a "real" sampler

An experiment (branch `sampler-features`) adding the two missing pieces
between AMY's PCM oscillators and a 90s techno / D&B sampler rig:

1. **Sample-accurate note-on placement** — `sample_offset` (wire `J`).
   AMY events execute on render-block boundaries (256 samples, ~5.8 ms).
   That's fine for notes, fatal for chopped breaks: slices of arbitrary
   length can't butt-join, so you can't "recreate" a break from its cuts.
   `sample_offset=k` starts a PCM note-on at sample `k` *within* the block
   its note-on fires in. Schedule slice starts at `S` as block `S//256` +
   offset `S%256` and joins are gapless.

2. **Pitch-invariant time stretch / time-invariant pitch shift** — `fit`
   (wire `Y`), non-destructive and computed at note-on:
   - `fit=N` (N>0): play the sample over exactly N sequencer ticks
     (so it follows `tempo`), pitch untouched; `note` transposes as pure
     tuning without changing duration. The Ableton-style loop case.
   - `fit=0`: `note` transposes but duration stays the original. The
     "pitch the chord stab without re-timing the groove" case.
   - `fit=-1`: off (back to plain varispeed PCM).

Plus one enabling fix: untransposed PCM playback (no `note`, no freq mods)
now uses the preset's native sample rate *exactly* instead of going through
a `log2f`/`exp2f` roundtrip that drifted ~1 sample every few seconds —
without it, "gapless" couldn't be sample-exact against the source.

## How the stretcher works (and why not an FFT phase vocoder)

`src/pcm.c` (`render_pcm_stretch` and friends). It's a synchronous granular
overlap-add stretcher, all fixed point, targeting every AMY platform
including ESP32-S3/RP2350:

- Two overlapping grains, 1024 output samples each, 50% overlap, Q15 Hann
  windows (Hann at 50% overlap sums to exactly 1 — COLA — so gain is flat).
- Each grain reads the sample with a Q16 fractional step carrying the
  *pitch* (note tuning × preset-samplerate conversion), linear interpolated.
- Every 512 output samples a new grain spawns at the *input timeline*
  position, which advances at `remaining_frames / target_output_samples`
  per output sample — the *duration*. Pitch and duration are decoupled
  because these two rates are independent.
- **WSOLA-style alignment**: a naive granular shifter turns a pure tone
  into a comb spaced at the hop rate (~86 Hz) — you hear warble. At each
  spawn we search ±64 frames around the nominal start for the segment that
  best correlates (64-tap dot product) with what the still-playing grain is
  about to play, and start there. The timeline itself is never adjusted, so
  alignment jitter can't accumulate into tempo error. Verified: a 220 Hz
  tone shifted +7 semitones lands within 2% of the target pitch.
- Note-on transient trick: the first hop starts *both* grains at the same
  input position, one window ascending, one at its peak — their sum is 1,
  so the first ~12 ms reproduce the input exactly and drum transients don't
  get a window fade-in.
- End of (non-looping) sample: grains fade out over ≤ one grain (~23 ms
  windowed tail past the fit target). Looping modes (`ww`) wrap the input
  timeline at the loop marks.

Cost: ~2.5× plain PCM rendering per voice, plus the spawn-time correlation
search ((2·64+1)·64 int MACs per 512 output samples ≈ 16 MACs/sample).
`PCM_STRETCH_SEARCH 0` in pcm.c disables the search for very small parts.
State is ~48 bytes per osc, embedded in `synthinfo`; the Hann table is 2 KB.

Why not a phase vocoder: an FFT PV needs FFTs (which AMY doesn't ship),
kilobytes of per-voice spectral state, and float (or hairy block-floating
fixed-point FFT) math — a poor fit for the RP2350 class. Granular+WSOLA is
the 90s hardware answer (it *is* the Akai sound), costs almost nothing, and
degrades gracefully. If we ever want FFT quality on desktop/web, it can slot
in behind the same `fit=` API.

## What was verified (offline renders are deterministic)

- `sample_offset=k` starts playback at exactly sample `k` of its block
  (impulse test).
- A break split at an arbitrary onset (`cut=7777`) and scheduled
  back-to-back reconstructs the one-shot render **bit-exactly** (−200 dB).
- `reconstruct amen`: all 47 slices vs the original file: peak residual
  −57 dB (pure output-quantization noise, zero timing error). With
  `--no-offset` (block-quantized, i.e. what AMY could do before): +7 dB —
  the residual is *louder than the signal*, every join off by up to 255
  samples.
- `fit=0`, +7 semitones: pitch ratio within 2% of 2^(7/12), duration
  preserved.
- `fit=96` ticks: output duration within ~5 ms of 96 ticks, pitch within
  0.5% of the original.
- `make test`: all 132 golden tests still pass (nothing changes unless the
  new params are used).

## Scripts

```
python3 slice_breaks.py            # ~/.local/share/tulipcc2/breaks -> slices/ + manifests
                                   # onset detection (spectral flux), slice points snapped
                                   # to zero crossings, bpm estimate, slices tile the file
python3 play_sampler.py reconstruct amen              # gapless rebuild + residual measurement
python3 play_sampler.py reconstruct amen --no-offset  # hear the block-quantized version
python3 play_sampler.py hits amen --bpm 140 --shuffle # chopped 16th-grid hits, every hit
                                                      # at a different sub-block offset
python3 play_sampler.py loops amen thinking --bpm 120 # both breaks fit= to the session
                                                      # tempo, looped by the sequencer
python3 play_sampler.py pitch thinking --slice 1      # fit=0 semitone ladder, length fixed
```

All modes render deterministic WAVs by default; `--live` plays through the
sound device (loops/hits/pitch). Offline scheduling uses the fact that an
event sent when N blocks have rendered fires in block N: the `Conductor`
sends each event in its exact block with `sample_offset` carrying the
remainder.

## API summary

| kwarg | wire | meaning |
| --- | --- | --- |
| `sample_offset=k` | `Jk` | PCM note-on starts at sample k (0..255) of its block |
| `fit=N` | `YN` | stretch sample to N sequencer ticks, pitch-invariant |
| `fit=0` | `Y0` | pitch-shift via `note`, duration unchanged |
| `fit=-1` | `Y-1` | disable the fit engine |

Both are ordinary osc params: settable from C (`amy_event.sample_offset`,
`.fit_ticks`), wire, Python, JS, and Godot (generated APIs rebuilt with
`make c-api` / `make godot-api`). They're sticky per osc like `phase`.

## Not done yet / next steps

- **Keygroup / drum-kit synth** (proposal #3): an Akai-style instrument
  layer mapping note+velocity ranges → presets. The natural shape: a synth
  flag where note-on picks `preset = bank_base + note` (drum-kit style),
  and a keygroup table (note lo/hi, vel lo/hi, preset, tuning) stored like
  a patch for the full Akai style. The instrument layer (`instrument.c`,
  `bank_number`) already has most of the plumbing. Slices-as-pads works
  today by putting each slice preset on its own osc/synth.
- **Sample-granular event scheduling**: `sample_offset` handles the
  sub-block remainder but the host still has to aim the event at the right
  block (trivial offline, fiddly live). A `time`-in-samples variant of the
  delta queue (or letting the sequencer carry a sub-tick sample offset)
  would make live gapless chops exact too.
- **`fit` on streamed (`disk_sample`) presets**: needs random access;
  refused at note-on today.
- **MCU reality check**: the engine is fixed-point throughout and sized for
  small parts, but polyphony limits on ESP32-S3/RP2350 need measuring
  (`tools/arduino_loadsweep`); expect a few fit voices, not a dozen, on the
  S3.
- **Stereo grains** read L/R/mix like plain PCM (`PCM_LEFT`/`PCM_RIGHT`
  pairs work), but the WSOLA search correlates the mixed signal only.
