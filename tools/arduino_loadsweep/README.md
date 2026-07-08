# arduino_loadsweep — per-commit AMY render-load regression sweep

The arduino-cli sibling of tulipcc's `tools/amyboard_loadsweep`
([tulipcc#1108](https://github.com/shorepine/tulipcc/pull/1108)): find the AMY
commit where render load regressed by building a fixed test sketch against
every state of `main` since a start date, running it on a real AMYboard, and
graphing the measured render load per commit.

## What it does

For each `--first-parent` commit on `origin/main` since `--since` (default
2026-04-01) that changed any `.c`/`.h` file:

1. Check the commit out in a throwaway clone (`~/amy-loadsweep/tree`).
2. Patch `src/i2s.c` in place to print `RENDER_LOAD ms=<ms> load=<0..1>` on
   stderr at 2 Hz (`patch_render_load.py`). Pre-#826 trees get the
   measurement backported from [amy#826](https://github.com/shorepine/amy/pull/826);
   post-#826 trees just print AMY's own `amy_global.render_load` (same
   smoothing, directly comparable) with the failsafe disabled.
3. Build `LoadTestChord` against it with arduino-cli
   (`esp32:esp32:amyboard`, isolated sketchbook — your real one is untouched).
4. Flash over the debug-UART dongle, whose DTR/RTS lines drive the board's
   auto-reset circuit — no BOOT/RST pressing, and a wedged board is recovered
   automatically by the next flash (`measure.py`).
5. Capture 20 s of the same UART while the sketch stacks up a held chord:
   `reset`, `synth=1 num_voices=8 patch=1`, then one held note
   (`40+2i`, vel 0.2) every 2 s — 8 load steps, full chord from t≈17s.
6. Write `results/<idx>_<date>_<sha>/` with `load.csv`, `serial.log`,
   `meta.json`.

## Usage

```sh
cd tools/arduino_loadsweep

# smoke-test one commit first
python3 sweep.py --commit upstream-main

# the full sweep (resumable: done/failed commits are skipped on rerun)
python3 sweep.py

# graph it
python3 plot.py --results ~/amy-loadsweep/results
```

Needs: `arduino-cli` with the `esp32:esp32` core, `pyserial`, `matplotlib`,
an AMYboard whose debug UART is on a DTR/RTS-wired dongle (default
`/dev/cu.usbserial-A5069RR4`, override with `--port`).

Other knobs: `--all-commits` (every non-merge commit touching `.c`/`.h`, not
just main's spine), `--limit N`, `--retry-failed`, `--list` (print the commit
list and exit), `--seconds`, `--work`.

Two consecutive flash failures abort the sweep on the assumption the bench
(not the code) is broken; rerunning resumes where it left off.

## PR hardware CI

The same sketch + patch + `measure.py` also power AMY's per-PR hardware CI
(`.github/workflows/amy-hwci-build.yml` builds the firmware in the cloud —
the PR *and* a baseline at its merge base; `amy-hwci.yml` flashes both
back-to-back on the self-hosted bench Pi and comments a before/after/Δ
load table on the PR, formatted by `hwci_report.py`).
CI **FAIL means only that the PR's test couldn't run** — load values and a
failed baseline are informational there; regression *hunting* is this
sweep's job.

## Sharing the bench

`measure.py` serializes bench access so multiple harnesses (e.g. this sweep
and tulipcc's amyboard_loadsweep) can share one board: it takes an exclusive
flock on **`/tmp/amyboard-bench.lock`** for the whole flash+capture and also
waits until `lsof` shows no other holder of the serial port. Any other tool
that flashes this board should take the same flock. A power-on reset or
ROM-bootloader entry appearing mid-capture is treated as external
interference: the measurement is retried once, then recorded as failed
(panic/watchdog reboots are NOT interference — they're the commit under test
crashing, flagged `board_crashed` in meta.json).
