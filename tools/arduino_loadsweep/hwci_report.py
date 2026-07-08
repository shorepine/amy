#!/usr/bin/env python3
"""Turn one measure.py output dir into the AMY HW CI markdown report + verdict.

Reads <outdir>/{meta.json,load.csv} written by measure.py after a LoadTestChord
run (one held Juno note added every 2 s until an 8-note chord) and prints a
markdown report to stdout: the smoothed render load at each chord size, plus
the settled full-chord load.

Exit code is the HW CI verdict: 0 if the test RAN (flashed, all 8 notes were
sent, load samples arrived for the whole capture), 1 if it couldn't run
(flash/serial failure, board crash/wedge, capture interference).  The load
VALUES never gate — they're informational; regressions are hunted with
sweep.py/plot.py, not per-PR.

Usage: hwci_report.py <outdir>
"""
import csv
import json
import os
import sys


def main(outdir):
    problems = []
    meta, rows = {}, []
    try:
        meta = json.load(open(os.path.join(outdir, "meta.json")))
    except (OSError, ValueError):
        problems.append("no meta.json — measure.py died before writing results")
    try:
        with open(os.path.join(outdir, "load.csv")) as f:
            rows = [(float(r["t_s"]), float(r["load"]))
                    for r in csv.DictReader(f)]
    except OSError:
        pass

    errors = meta.get("errors", [])
    for e in errors:
        # "interference on attempt 1" alone means the retry succeeded
        if (e == "interference on attempt 1" and rows
                and "interference on attempt 2" not in errors):
            continue
        problems.append(e)
    notes = meta.get("notes", [])
    if not rows and not any("RENDER_LOAD" in p for p in problems):
        problems.append("no RENDER_LOAD lines captured")
    if rows and len(notes) < 8:
        problems.append(f"only {len(notes)}/8 notes were played")
    if meta.get("board_crashed"):
        problems.append("board crashed during the run (panic/watchdog reboot)")
    if meta.get("serial_died_early"):
        problems.append("serial output stopped early (crash/wedge?)")

    ok = not problems

    # Load at each chord size: the last sample before the NEXT note joins
    # (i.e. after ~2 s of this chord, the settled value), and for the full
    # chord meta's load_final (mean of the capture's last 3 s).
    lines = []
    if rows and notes:
        lines += ["| notes held | render load |", "|---:|---:|"]
        for i, n in enumerate(notes):
            end = notes[i + 1]["t_s"] if i + 1 < len(notes) else None
            window = [l for t, l in rows if t >= n["t_s"] and
                      (end is None or t < end)]
            load = window[-1] if window else None
            lines.append(f"| {n['i'] + 1} | "
                         f"{f'{load:.3f}' if load is not None else '—'} |")
        final = meta.get("load_final")
        maxl = meta.get("load_max")
        lines.append("")
        lines.append(f"**Full chord settled load: "
                     f"{f'{final:.3f}' if final is not None else '—'}**"
                     f" (peak {f'{maxl:.3f}' if maxl is not None else '—'},"
                     f" {meta.get('n_samples', 0)} samples)")

    if ok:
        verdict = "✅ **PASS** — the bench ran the test to completion."
    else:
        verdict = ("❌ **FAIL** — the bench could not run the test:\n" +
                   "\n".join(f"- {p}" for p in dict.fromkeys(problems)))
    print("\n".join([verdict, ""] + lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
