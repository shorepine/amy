# arduino_loadsweep — per-commit AMY render-load measurement

The arduino-cli sibling of tulipcc's `tools/amyboard_loadsweep`
([tulipcc#1108](https://github.com/shorepine/tulipcc/pull/1108)): find the AMY
commit where render load regressed by building a fixed test sketch against
every state of `main` since a start date, running it on a real AMYboard, and
graphing the measured render load per commit.

## What it does

The root-level amy/Makefile has a `speedtest` rule that:
 * Compiles the LoadTestChord sketch in this directory for the amyboard target
   with -DARDUINO_SPEEDTEST, which causes i2s.c to report the microseconds per
   osc rendering/mixdown every ~500ms.
 * Runs measure.py in this directory, which then ...
 * Flashes the just-compiled sketch to amyboard over the debug-UART dongle,
   whose DTR/RTS lines drive the board's
   auto-reset circuit — no BOOT/RST pressing, and a wedged board is recovered
   automatically by the next flash
 * Capture 20 s of the same UART while the sketch stacks up a held chord:
   `reset`, `synth=1 num_voices=8 patch=1`, then one held note
   (`40+2i`, vel 0.2) every 2 s — 6 load steps, full chord from t≈13s.
 * Write `results/<idx>_<date>_<sha>/` with `load.csv`, `serial.log`,
   `meta.json`.

## Usage

```sh
cd <AMY root>
make speedtest
```

Needs: `arduino-cli` with the `esp32:esp32` core, `pyserial`, 
an AMYboard whose debug UART is on a DTR/RTS-wired dongle (default
`/dev/cu.usbserial-0001`, override with `--port`).

Two consecutive flash failures abort the sweep on the assumption the bench
(not the code) is broken; rerunning resumes where it left off.

## PR hardware CI

The same sketch + `measure.py` also power AMY's per-PR hardware CI
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
