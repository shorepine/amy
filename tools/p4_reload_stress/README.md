# P4 patch-reload stress test

An ESP-IDF app for the **ESP32-P4X-Function-EV-Board** that reproduces the
patch-reload crash behind #1185, to compare `main` against the candidate
fixes (#1185, #1190) on the hardware where it was seen.

## What it does

It mirrors the #1185 report: AMY renders in its own tasks (I2S out,
multicore, multithread), and a task on core 0 reloads synth 1's patch by
sending wire-format messages, the same `amy_add_message` → parser →
`patches_load_patch` path as the reported crash. After each load it holds
four notes, so the old voice is live when the next load releases it. It
cycles Juno 1, DX7 130 and Juno 20, whose oscs-per-voice differ, so the new
voice reuses osc numbers the old one is freeing.

Every reload prints `RELOAD n`, so the log shows which reload faulted. A
panic reboots the board into a fresh run, so leaving it running collects many
runs without touching it.

| `STRESS_MODE` | Reloads per boot | Gap | Purpose |
|---|---|---|---|
| `0` (default) | 30 | 500 ms | The #1185 protocol: about 1 fault in 6 reloads on `main`, 30 clean with the fix |
| `1` | 1000 | none | Back to back, for the rarer render-side race |

I2S output is on BCLK 21, LRCLK 22, DOUT 23, as wired on the bench board;
it's output only, so MCLK and DIN are unused. Nothing has to be
connected, but I2S has to be on: its DMA paces the render tasks, and without
it AMY renders on the caller's thread and the race can't happen.

## Two races

`capture.py` sorts each panic into one of these by the functions in its
backtrace:

- **ingest**: the #1185 race. The sending thread allocates an osc
  (`ensure_osc_allocd` → `alloc_osc` → `reset_osc`) without the queue lock,
  while a flush frees or walks the same osc. #1185 locks around the
  allocation; #1190 removes it. Either should make these go away.
- **render**: loading a patch calls `flush_due_deltas()` on the *sending*
  thread, which runs the old voice's `FREE_OSC` there while the render tasks
  are reading that osc (`amy_render`, `render_*`, `hold_and_modify`).
  Rendering doesn't hold the lock, so neither PR covers this. A desktop
  AddressSanitizer run of the same workload hit it with both PRs.

The sort is a heuristic on function names, so read the decoded frames before
counting a run for or against a fix.

## Running it

Needs ESP-IDF 5.4 or newer with the `esp32p4` target, run from an IDF shell
(`. $IDF_PATH/export.sh`).

**Chip revision.** Early Function EV boards carry P4 silicon older than
v3.0. IDF 5.5 and later build for v3.0+ by default, and the image then
refuses to boot on older chips. If the boot log says so, set
`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` (menuconfig: Component config →
Hardware Settings → Chip revision) and rebuild.

Check out each AMY tree to compare next to this one:

```sh
git fetch origin main pull/1185/head:pr1185 claude/amy-pr-1185-osc-allocd-kao5og
git worktree add ../amy-main   origin/main
git worktree add ../amy-pr1185 pr1185
git worktree add ../amy-pr1190 origin/claude/amy-pr-1185-osc-allocd-kao5og
```

Build, flash and capture each one, with its own build directory so their
sdkconfigs and `.elf`s stay apart:

```sh
cd tools/p4_reload_stress
for t in main pr1185 pr1190; do
  idf.py -B build-$t -DAMY_DIR=$(realpath ../../../amy-$t) build
done

PORT=/dev/ttyUSB0          # the board's UART port
t=main
idf.py -B build-$t -p $PORT flash
python capture.py --port $PORT --seconds 300 \
    --elf build-$t/p4_reload_stress.elf --log $t.log
```

`capture.py` resets the board, prints the serial output live, then tallies:

```
run 1: clean, 30 reloads
run 2: PANIC after reload 5 [ingest] Core  0 panic'ed (Store access fault)
    reset_osc at .../amy.c:1100 <- ...
=== clean=1 ingest=1 render=0 other=0
```

`--from-log main.log --elf ...` re-tallies a saved capture.

### Stress mode and heap poisoning

```sh
idf.py -B build-$t-stress -DAMY_DIR=... -DSTRESS_MODE=1 build

# Comprehensive heap poisoning: catches a use-after-free that doesn't
# fault on its own (reported as CORRUPT HEAP).
idf.py -B build-$t-poison -DAMY_DIR=... -DSTRESS_MODE=1 \
    -DSDKCONFIG=build-$t-poison/sdkconfig \
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.poison" build
```

## Known gap: this harness did not reproduce #1185

Tested 2026-09-27 on an ESP32-P4X-Function-EV-Board (IDF 5.5.3): `main`,
pr1185 and pr1190 all ran completely clean here — 300 reloads each in mode 0
(10 runs x 30) and ~15,000 reloads each in mode 1 (15 runs x 1000), zero
`ingest` or `render` panics on any of the three, including `main`. Do not
read that as the bug being absent — it means this harness's raw
`amy_add_message()` patch-load + note-hold protocol doesn't recreate the
crash's actual trigger.

The real trigger, confirmed on the same board: tulip2's `synth.Group` /
`Synth` / `DrumSynth` + sequencer-tag allocation and teardown, driven the
way a person actually hits it — `run("parallax")` then `.quit()` (^Q),
repeated. On tulip2 with amy pinned to `main` (2ca613a) this panic'ed in
`reset_osc` (amy.c:1139, the ingest race) on cycle 14 of 20. The same loop
ran 40/40 clean cycles on both pr1185 (c40cdd8) and pr1190 (352b8bc /
de3cab3), which is the actual evidence that both fixes work — not the
mode-0/mode-1 runs above.

If you're picking this test back up: treat this app's numbers as
uninformative until its `amy_add_message()` sequence is rebuilt to
allocate/free through a synth group's actual lifecycle (osc/voice churn
across polyphony + a sequencer tag) instead of one synth's bare patch
reloads. Until then, use tulip2 + a real app's run/quit cycle as the
ground truth for this bug.

## Suggested order

1. **`main`, mode 0, about 10 runs.** This should reproduce roughly one fault
   in six reloads, in the ingest race. If it doesn't, the setup isn't
   matching the one in #1185, and the later results don't say much.
2. **#1185 and #1190, mode 0, the same number of reloads.** If a fix did
   nothing, 30 clean reloads in a row would happen only about 0.4% of the
   time ((5/6)^30).
3. **All three, mode 1, with and without heap poisoning.** This is where the
   render race should show up, fix or no fix.
