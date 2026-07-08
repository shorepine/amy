#!/usr/bin/env python3
"""Graph a per-commit render-load sweep produced by sweep.py.

Three views: all commits overlaid, per-commit small multiples, and a
final-chord mean/max bar chart in commit order (the discontinuity view)
with AMY version bumps marked.  Commits whose serial output died early
(crash/wedge) are flagged.
"""
import argparse
import csv
import glob
import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt          # noqa: E402
import matplotlib.cm as cm               # noqa: E402
import numpy as np                       # noqa: E402


def load_commits(results):
    commits = []
    for d in sorted(glob.glob(os.path.join(results, "*/"))):
        csvp = os.path.join(d, "load.csv")
        if not os.path.exists(csvp):
            continue
        rows = [(float(r["t_s"]), float(r["load"]))
                for r in csv.DictReader(open(csvp))]
        meta = {}
        mp = os.path.join(d, "meta.json")
        if os.path.exists(mp):
            meta = json.load(open(mp))
        commits.append({"label": os.path.basename(d.rstrip("/")),
                        "rows": rows, "meta": meta})
    return commits


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--results",
                    default=os.path.expanduser("~/amy-loadsweep/results"))
    ap.add_argument("--out", default=None,
                    help="output PNG (default <results>/render_load_sweep.png)")
    ap.add_argument("--title",
                    default="AMYboard render load per AMY commit (8-note held chord)")
    args = ap.parse_args()
    out = args.out or os.path.join(args.results, "render_load_sweep.png")

    commits = load_commits(args.results)
    if not commits:
        raise SystemExit(f"no load.csv files under {args.results}")
    n = len(commits)
    tmax = max((r[0] for c in commits for r in c["rows"]), default=20) + 1
    colors = cm.viridis(np.linspace(0.05, 0.95, n))

    fig = plt.figure(figsize=(15, 18))
    gs = fig.add_gridspec(3, 1, height_ratios=[1.1, 1.8, 0.9], hspace=0.35)

    # overlay
    ax = fig.add_subplot(gs[0])
    for c, col in zip(commits, colors):
        t = [r[0] for r in c["rows"]]
        v = [r[1] for r in c["rows"]]
        ax.plot(t, v, color=col, lw=1.2, alpha=0.8)
    ax.axhline(0.98, color="red", ls=":", lw=1, alpha=0.6)
    for i in range(8):
        ax.axvline(2 + 2 * i + 1.0, color="gray", lw=0.5, alpha=0.3)
    ax.text(0.2, 0.985, "overload threshold", color="red", fontsize=8,
            va="bottom")
    ax.set_xlim(0, tmax)
    ax.set_ylim(0, 1.05)
    ax.set_xlabel("seconds since boot (note added every 2s from t≈3s)")
    ax.set_ylabel("AMY render load (0..1)")
    ax.set_title(args.title + f"  —  {commits[0]['label'][4:]} … "
                 f"{commits[-1]['label'][4:]} (color = commit order)")
    ax.grid(alpha=0.25)

    # small multiples
    cols = 6
    rows_n = int(np.ceil(n / cols))
    sub = gs[1].subgridspec(rows_n, cols, hspace=0.6, wspace=0.25)
    for i, (c, col) in enumerate(zip(commits, colors)):
        axs = fig.add_subplot(sub[i // cols, i % cols])
        t = [r[0] for r in c["rows"]]
        v = [r[1] for r in c["rows"]]
        axs.plot(t, v, color=col, lw=1.2)
        axs.fill_between(t, v, color=col, alpha=0.25)
        axs.set_xlim(0, tmax)
        axs.set_ylim(0, 1.05)
        axs.axhline(0.98, color="red", ls=":", lw=0.7, alpha=0.6)
        died = " ⚠died" if c["meta"].get("serial_died_early") else ""
        axs.set_title(c["label"][4:] + died, fontsize=6.5)
        axs.tick_params(labelsize=6)
        if i % cols:
            axs.set_yticklabels([])

    # final-chord summary in commit order
    axb = fig.add_subplot(gs[2])
    labels, means, maxes, died = [], [], [], []
    for c in commits:
        ss = [v for t, v in c["rows"] if t >= 16.5]   # all 8 notes sounding
        labels.append(c["label"][4:])
        means.append(np.mean(ss) if ss else 0)
        maxes.append(max((v for t, v in c["rows"]), default=0))
        died.append(c["meta"].get("serial_died_early", False))
    x = np.arange(n)
    axb.bar(x - 0.2, means, 0.4, color=list(colors),
            label="mean load, full chord (t≥16.5s)")
    axb.bar(x + 0.2, maxes, 0.4, color=list(colors), alpha=0.45,
            label="max load")
    for i, w in enumerate(died):
        if w:
            axb.text(i, (maxes[i] or 0.05) + 0.02, "died", ha="center",
                     fontsize=6.5, color="red", rotation=90)
    axb.axhline(0.98, color="red", ls=":", lw=1, alpha=0.6)
    axb.set_xticks(x)
    axb.set_xticklabels(labels, rotation=90, fontsize=5.5)
    axb.set_ylim(0, 1.1)
    axb.set_ylabel("render load")
    axb.set_title("full-chord render load per commit (discontinuity view)")
    axb.legend(fontsize=8)
    axb.grid(axis="y", alpha=0.25)

    prev = None
    for i, c in enumerate(commits):
        ver = c["meta"].get("version", "")
        if prev is not None and ver != prev:
            axb.axvline(i - 0.5, color="k", ls="--", lw=0.7, alpha=0.5)
            axb.text(i - 0.45, 1.03, ver, fontsize=6, rotation=45)
        prev = ver

    fig.savefig(out, dpi=130, bbox_inches="tight")
    print("wrote", out)
    for c, m, mx in zip(commits, means, maxes):
        print(f"{c['label']}  mean={m:.3f}  max={mx:.3f}"
              f"{'  DIED' if c['meta'].get('serial_died_early') else ''}"
              f"  {c['meta'].get('subject', '')[:60]}")


if __name__ == "__main__":
    main()
