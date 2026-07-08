#!/usr/bin/env python3
"""Per-commit AMY render-load regression sweep on real AMYboard hardware.

The arduino-cli sibling of tulipcc's tools/amyboard_loadsweep (#1108): for
every state of AMY `main` since a start date that changed .c/.h code, build
the LoadTestChord sketch against that AMY (patched to print its render load
— see patch_render_load.py), flash a connected AMYboard over the DTR/RTS
flash dongle, capture 20s of load prints while the sketch stacks up an
8-note held chord, and record the trace.  plot.py then graphs the traces to
spot the commit a regression landed.

    python3 sweep.py --commit origin/main   # single-commit smoke test
    python3 sweep.py                        # the full sweep since --since
    python3 plot.py --results ~/amy-loadsweep/results

Commits are enumerated --first-parent on origin/main (each is a state main
was actually in, in time order); --all-commits instead walks every non-merge
commit touching .c/.h (includes intra-PR commits, unordered, some may not
build).  Resumable: a commit whose results/<label>/load.csv exists is
skipped, as are recorded failures (rerun those with --retry-failed).  Two
consecutive flash failures abort the sweep (bench trouble, not code
trouble).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SKETCH = "LoadTestChord"


def git(repo, *a, check=True):
    r = subprocess.run(["git", "-C", repo] + list(a), text=True,
                       capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(a)}: {r.stderr.strip()[:500]}")
    return (r.stdout or "").strip()


def enumerate_commits(tree, args):
    ref = "upstream-main"
    walk = ["--first-parent"] if not args.all_commits else ["--no-merges"]
    out = git(tree, "rev-list", "--reverse", *walk,
              f"--since={args.since}", ref, "--", "*.c", "*.h")
    shas = out.split() if out else []
    commits = []
    for i, sha in enumerate(shas):
        date, subject = git(tree, "log", "-1", "--format=%cs%x00%s",
                            sha).split("\0")
        commits.append({"idx": i, "sha": sha, "date": date, "subject": subject,
                        "label": f"{i:03d}_{date}_{sha[:7]}"})
    return commits


def init_workspace(work):
    tree = os.path.join(work, "tree")
    if not os.path.exists(os.path.join(tree, ".git")):
        print(f"[ws] cloning {REPO} -> {tree}")
        subprocess.run(["git", "clone", "-q", REPO, tree], check=True)
    git(tree, "fetch", "-q", REPO,
        "+refs/remotes/origin/main:refs/heads/upstream-main")
    sb = os.path.join(work, "sketchbook")
    os.makedirs(os.path.join(sb, "libraries"), exist_ok=True)
    link = os.path.join(sb, "libraries", "AMY")
    if os.path.islink(link):
        os.unlink(link)
    os.symlink(tree, link)
    return tree, sb


def build_commit(tree, sb, c, args):
    """Checkout + patch + compile.  Returns (build_dir, sketch_dir) or None."""
    git(tree, "checkout", "-q", "-f", c["sha"])
    git(tree, "clean", "-fdq")
    if not args.no_patch:
        r = subprocess.run([sys.executable,
                            os.path.join(HERE, "patch_render_load.py"),
                            os.path.join(tree, "src", "i2s.c")],
                           text=True, capture_output=True)
        print(r.stdout.strip() or r.stderr.strip())
        if r.returncode != 0:
            return None, "patch"
    # fresh copy of the sketch into the isolated sketchbook
    sketch_dir = os.path.join(sb, SKETCH)
    shutil.rmtree(sketch_dir, ignore_errors=True)
    shutil.copytree(os.path.join(HERE, SKETCH), sketch_dir)

    build_dir = os.path.join(args.work, "build", "current")
    shutil.rmtree(build_dir, ignore_errors=True)
    env = dict(os.environ, ARDUINO_DIRECTORIES_USER=sb)
    # Force -O2 everywhere (core default is -Os): commits in the window differ
    # in whether AMY carries its own optimize pragma, and comparing -Os builds
    # to pragma-O2 builds would fake a load discontinuity.
    r = subprocess.run(["arduino-cli", "compile", "--fqbn", args.fqbn,
                        "--build-property",
                        f"compiler.optimization_flags={args.opt}",
                        "--build-path", build_dir, sketch_dir],
                       text=True, capture_output=True, env=env, timeout=1800)
    if r.returncode != 0:
        return None, "build:" + (r.stdout + r.stderr)[-2000:]
    return (build_dir, sketch_dir), None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--since", default="2026-04-01")
    ap.add_argument("--all-commits", action="store_true",
                    help="every non-merge commit, not just main's spine")
    ap.add_argument("--commit", default=None,
                    help="test one ref only (smoke test)")
    ap.add_argument("--limit", type=int, default=None)
    ap.add_argument("--work", default=os.path.expanduser("~/amy-loadsweep"))
    ap.add_argument("--results", default=None)
    ap.add_argument("--fqbn", default="esp32:esp32:amyboard")
    ap.add_argument("--opt", default="-O2",
                    help="optimization flag forced on the whole build")
    ap.add_argument("--port", default="/dev/cu.usbserial-A5069RR4")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--no-patch", action="store_true")
    ap.add_argument("--retry-failed", action="store_true")
    ap.add_argument("--list", action="store_true",
                    help="just print the commit list and exit")
    args = ap.parse_args()
    results = args.results or os.path.join(args.work, "results")

    os.makedirs(args.work, exist_ok=True)
    tree, sb = init_workspace(args.work)

    if args.commit:
        sha = git(tree, "rev-parse", args.commit)
        date, subject = git(tree, "log", "-1", "--format=%cs%x00%s",
                            sha).split("\0")
        commits = [{"idx": 999, "sha": sha, "date": date, "subject": subject,
                    "label": f"smoke_{date}_{sha[:7]}"}]
    else:
        commits = enumerate_commits(tree, args)
        if args.limit:
            commits = commits[:args.limit]
    print(f"[sweep] {len(commits)} commits to test")
    if args.list:
        for c in commits:
            print(f"  {c['label']}  {c['subject'][:70]}")
        return 0

    flash_fails = 0
    for c in commits:
        outdir = os.path.join(results, c["label"])
        failed_marker = os.path.join(outdir, "FAILED")
        if os.path.exists(os.path.join(outdir, "load.csv")):
            print(f"== {c['label']}: done, skipping")
            continue
        if os.path.exists(failed_marker) and not args.retry_failed:
            print(f"== {c['label']}: previously failed, skipping")
            continue
        print(f"== {c['label']}: {c['subject'][:70]}")
        os.makedirs(outdir, exist_ok=True)
        if os.path.exists(failed_marker):
            os.unlink(failed_marker)

        built, err = build_commit(tree, sb, c, args)
        version = git(tree, "show", "HEAD:library.properties",
                      check=False).split("version=")[-1].split("\n")[0]
        if not built:
            kind = err.split(":", 1)[0]
            print(f"== {c['label']}: {kind.upper()} FAILED")
            with open(failed_marker, "w") as f:
                f.write(err)
            continue
        build_dir, sketch_dir = built

        meta = {"sha": c["sha"], "date": c["date"], "subject": c["subject"],
                "version": version, "idx": c["idx"], "opt": args.opt}
        r = subprocess.run([sys.executable, os.path.join(HERE, "measure.py"),
                            build_dir, "--out", outdir, "--port", args.port,
                            "--seconds", str(args.seconds),
                            "--label", c["label"],
                            "--meta", json.dumps(meta)])
        if r.returncode != 0 and not os.path.exists(
                os.path.join(outdir, "load.csv")):
            flash_fails += 1
            with open(failed_marker, "w") as f:
                f.write("measure failed\n")
            if flash_fails >= 2:
                sys.exit("[sweep] two consecutive flash/measure failures — "
                         "check the bench, then rerun (resumes automatically)")
        else:
            flash_fails = 0
    print("[sweep] done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
