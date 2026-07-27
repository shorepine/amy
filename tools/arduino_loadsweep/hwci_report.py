#!/usr/bin/env python3
"""Turn measure.py output dirs into the AMY HW CI markdown report + verdict.

Reads <outdir>/{meta.json,load*.csv} written by measure.py after a
LoadTestChord run (one held Juno note added every 2 s until an 8-note chord)
and prints a markdown report to stdout: the smoothed render load at each
chord size, plus the settled full-chord load.  When measure.py captured
several runs (--runs N: flash once, reset + re-capture), each chord size's
load is the MEAN across the clean runs and the report also shows the per-run
full-chord values so run-to-run noise is visible.  With --base <dir> (a
second measure.py run of the same sketch built against the PR's merge base)
the table gains before/after columns and a delta, so the PR's own load cost
is visible directly.

Exit code is the HW CI verdict for the PR run only: 0 if the test RAN
(flashed, all 8 notes were sent, load samples arrived for the whole capture),
1 if it couldn't run (flash/serial failure, board crash/wedge, capture
interference).  A failed BASELINE run never fails CI — it's reported and the
table falls back to PR-only columns.  The load VALUES never gate either —
they're informational; regression hunting across commits is
sweep.py/plot.py's job, not per-PR CI's.

Usage: hwci_report.py <outdir> [--base <outdir>]
"""
import argparse
import csv
import json
import os
import sys


def read_rows(outdir, name):
    try:
        with open(os.path.join(outdir, name)) as f:
            return [(float(r["t_s"]), int(r["load"]))
                    for r in csv.DictReader(f)]
    except OSError:
        return []


def settled_loads(rows, notes):
    """note index i (0-based) -> the settled load for that chord size:
    the last 2 Hz sample before the next note joins."""
    loads = {}
    for i, n in enumerate(notes):
        end = notes[i + 1]["t_s"] if i + 1 < len(notes) else None
        window = [l for t, l in rows if t >= n["t_s"] and
                  (end is None or t < end)]
        if window:
            loads[n["i"]] = window[-1]
    return loads


def run_problems(run, rows):
    """Problems for one capture run (run: per-run dict from meta.json,
    or a pre---runs meta.json acting as its own single run)."""
    problems = []
    errors = run.get("errors", [])
    for e in errors:
        # "interference on attempt 1" alone means the retry succeeded
        if (e == "interference on attempt 1" and rows
                and "interference on attempt 2" not in errors):
            continue
        problems.append(e)
    notes = run.get("notes", [])
    if not rows and not any("RENDER_LOAD" in p for p in problems):
        problems.append("no RENDER_LOAD lines captured")
    if rows and len(notes) < 6:
        problems.append(f"only {len(notes)}/6 notes were played")
    if run.get("board_crashed"):
        problems.append("board crashed during the run (panic/watchdog reboot)")
    if run.get("serial_died_early"):
        problems.append("serial output stopped early (crash/wedge?)")
    return problems


def analyze(outdir):
    """Read one measure.py output dir -> {meta, notes, loads, finals,
    has_rows, problems}.

    measure.py may have captured several runs of the flashed build (--runs:
    flash once, reset + re-capture); loads then maps note index i -> the MEAN
    settled load across the clean runs, and finals lists each clean run's
    full-chord settled load.
    """
    problems = []
    meta = {}
    try:
        meta = json.load(open(os.path.join(outdir, "meta.json")))
    except (OSError, ValueError):
        problems.append("no meta.json — measure.py died before writing results")
    run_metas = meta.get("runs") or [meta]   # old meta = its own single run
    multi = len(run_metas) > 1

    per_run_loads, finals, has_rows = [], [], False
    for k, rm in enumerate(run_metas):
        rows = read_rows(outdir, rm.get("load_csv", "load.csv"))
        has_rows = has_rows or bool(rows)
        pfx = f"run {k + 1}: " if multi else ""
        problems += [pfx + p for p in run_problems(rm, rows)]
        if rm.get("interfered"):
            continue                    # trace invalid — keep out of the means
        loads = settled_loads(rows, rm.get("notes", []))
        if loads:
            per_run_loads.append(loads)
        if rm.get("render_us_final") is not None:
            finals.append(rm["render_us_final"])

    loads = {i: round(sum(lk[i] for lk in per_run_loads if i in lk) /
                      sum(1 for lk in per_run_loads if i in lk))
             for i in {i for lk in per_run_loads for i in lk}}
    return {"meta": meta, "has_rows": has_rows, "notes": meta.get("notes", []),
            "loads": loads, "finals": finals,
            "problems": list(dict.fromkeys(problems))}


def fmt(v, signed=False):
    if v is None:
        return "—"
    return f"{v:+d}" if signed else f"{v:d}"


def fmtpct(v, signed=False):
    if v is None:
        return "—%"
    return f"{100 * v:+.1f}%" if signed else f"{100 * v:+.1f}%"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("outdir", help="measure.py output dir for the PR build")
    ap.add_argument("--base", default=None,
                    help="measure.py output dir for the merge-base build; "
                         "adds before/after columns")
    args = ap.parse_args()

    pr = analyze(args.outdir)
    base = analyze(args.base) if args.base else None
    ok = not pr["problems"]

    base_sha = (base or {}).get("meta", {}).get("sha", "")
    base_col = f"main @ {base_sha[:7]}" if base_sha else "before"
    with_base = bool(base and base["loads"])

    lines = []
    if pr["has_rows"] and pr["notes"]:
        if with_base:
            lines += [f"| notes held | {base_col} | this PR | Δ |",
                      "|---:|---:|---:|---:|"]
        else:
            lines += ["| notes held | render &mu;s |", "|---:|---:|"]
        for n in pr["notes"]:
            after = pr["loads"].get(n["i"])
            row = [str(n["i"] + 1)]
            if with_base:
                before = base["loads"].get(n["i"])
                delta = (after - before
                         if after is not None and before is not None else None)
                row += [fmt(before), fmt(after), fmt(delta, signed=True)]
            else:
                row += [fmt(after)]
            lines.append("| " + " | ".join(row) + " |")

        final = pr["meta"].get("render_us_final")
        maxl = pr["meta"].get("render_us_max")
        lines.append("")
        headline = f"**Full chord settled render &mu;s: {fmt(final)}"
        if with_base:
            bfinal = base["meta"].get("render_us_final")
            bdelta = ((final - bfinal) / bfinal
                      if final is not None and bfinal is not None else None)
            headline += (f" (was {fmt(bfinal)}, Δ {fmtpct(bdelta, signed=True)})")
        headline += (f"** (peak {fmt(maxl)}, "
                     f"{pr['meta'].get('n_samples', 0)} samples"
                     + (f", mean of {len(pr['finals'])} runs"
                        if len(pr["finals"]) > 1 else "") + ")")
        lines.append(headline)

        if len(pr["finals"]) > 1:
            sub = ("<sub>per-run full-chord &mu;s — this PR: "
                   + " / ".join(str(v) for v in pr["finals"]))
            if with_base and len(base["finals"]) > 1:
                sub += (f" · {base_col}: "
                        + " / ".join(str(v) for v in base["finals"]))
            lines += ["", sub + "</sub>"]

    if base and base["problems"]:
        lines += ["", "_Baseline run "
                  + ("had problems" if base["loads"] else "unavailable")
                  + " (does not gate CI): "
                  + "; ".join(base["problems"]) + "_"]

    if ok:
        verdict = "✅ **PASS** — the bench ran the test to completion."
    else:
        verdict = ("❌ **FAIL** — the bench could not run the test:\n" +
                   "\n".join(f"- {p}" for p in pr["problems"]))
    print("\n".join([verdict, ""] + lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
