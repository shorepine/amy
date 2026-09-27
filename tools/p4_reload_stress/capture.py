#!/usr/bin/env python3
"""Capture and tally reload_stress runs from the P4's serial port.

Each boot of the firmware is one run: STRESS_START, then RELOAD n lines,
ending in STRESS_DONE (clean) or a panic, after which the board reboots into
the next run. This reads the port for a while (or a saved log), splits it
into runs, and prints each run's outcome: how many reloads it got through,
and for a panic, the fault and the top frames (MEPC / RA decoded with
addr2line when --elf is given), sorted into the two races:

  ingest  the #1185 race: the sending thread allocating an osc
          (reset_osc / alloc_osc / ensure_osc_allocd on the ingest path)
          unlocked, while a flush frees or walks the same osc. #1185 and
          #1190 both remove this one.
  render  a FREE_OSC run by the sending thread's flush (patches_load_patch
          -> flush_due_deltas) while the render tasks are reading the osc
          (amy_render / render_* / hold_and_modify). Neither PR covers this.

Usage (from an ESP-IDF shell, so pyserial and the toolchain are on PATH):
  python capture.py --port /dev/ttyUSB0 --seconds 300 --elf build/p4_reload_stress.elf --log main.log
  python capture.py --from-log main.log --elf build/p4_reload_stress.elf

The sort is a heuristic on function names; read the decoded frames it prints
before counting a run for or against a fix.
"""
import argparse
import re
import shutil
import subprocess
import sys
import time

# Checked render-first: a crash under amy_render is the render race whatever
# osc routine it died in. Anything else touching osc storage (the ingest
# allocation, or a flush's play_delta walking an osc it is allocating) is
# the #1185 race.
RENDER = ("amy_render", "render_", "hold_and_modify", "compute_mod_scale",
          "amp_combine_controls")
INGEST = ("reset_osc", "alloc_osc", "ensure_osc_allocd", "free_osc",
          "amy_event_to_deltas_queue", "play_delta", "chained_osc_would_cause_loop")


def read_port(port, seconds, log_path):
    import serial  # pyserial ships with ESP-IDF's python env
    lines = []
    with serial.Serial(port, 115200, timeout=0.5) as ser:
        # Reset the board so capture starts at a fresh boot (EN via RTS).
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        end = time.time() + seconds
        buf = b""
        while time.time() < end:
            buf += ser.read(4096)
            *done, buf = buf.split(b"\n")
            for raw in done:
                line = raw.decode("utf-8", "replace").rstrip("\r")
                lines.append(line)
                print(line, flush=True)
    if log_path:
        with open(log_path, "w") as f:
            f.write("\n".join(lines) + "\n")
    return lines


def addr2line(elf, addrs):
    tool = shutil.which("riscv32-esp-elf-addr2line")
    if not elf or not tool or not addrs:
        return [""] * len(addrs)
    out = subprocess.run([tool, "-pfiaC", "-e", elf] + addrs,
                         capture_output=True, text=True).stdout
    # One block per address; inlined frames continue on "(inlined by)" lines.
    blocks, cur = [], []
    for line in out.splitlines():
        if line.startswith("0x") and cur:
            blocks.append(" <- ".join(cur))
            cur = []
        cur.append(re.sub(r"^0x[0-9a-f]+: ", "", line.strip()))
    if cur:
        blocks.append(" <- ".join(cur))
    return blocks + [""] * (len(addrs) - len(blocks))


def classify(text):
    if any(k in text for k in RENDER):
        return "render"
    if any(k in text for k in INGEST):
        return "ingest"
    return "other"


def split_runs(lines):
    runs, cur = [], None
    for line in lines:
        if "STRESS_START" in line:
            cur = {"start": line, "reloads": 0, "done": False, "panic": None,
                   "regs": {}, "bt": [], "heap": None}
            runs.append(cur)
            continue
        if cur is None:
            continue
        m = re.search(r"RELOAD (\d+)", line)
        if m:
            cur["reloads"] = int(m.group(1))
        if "STRESS_DONE" in line:
            cur["done"] = True
        m = re.search(r"Guru Meditation Error: (.*)", line)
        if m and not cur["panic"]:
            cur["panic"] = m.group(1).strip()
        if "CORRUPT HEAP" in line and not cur["heap"]:
            cur["heap"] = line.strip()
            cur["panic"] = cur["panic"] or "heap poisoning: corrupt heap"
        # CONFIG_ESP_SYSTEM_USE_EH_FRAME: "Backtrace: 0xPC:0xSP 0xPC:0xSP ..."
        m = re.search(r"Backtrace:\s*(.*)", line)
        if m and not cur["bt"]:
            cur["bt"] = re.findall(r"(0x[0-9a-fA-F]+):0x[0-9a-fA-F]+", m.group(1))
        for reg in ("MEPC", "RA"):
            m = re.search(r"\b%s\s*:\s*(0x[0-9a-fA-F]+)" % reg, line)
            if m and reg not in cur["regs"]:
                cur["regs"][reg] = m.group(1)
    return runs


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--seconds", type=int, default=300)
    ap.add_argument("--log", help="save the raw capture here")
    ap.add_argument("--from-log", help="tally a saved capture instead of reading the port")
    ap.add_argument("--elf", help="the build's .elf, to decode MEPC/RA")
    args = ap.parse_args()
    if args.from_log:
        with open(args.from_log, errors="replace") as f:
            lines = f.read().splitlines()
    elif args.port:
        lines = read_port(args.port, args.seconds, args.log)
    else:
        ap.error("give --port or --from-log")

    runs = split_runs(lines)
    # The last run is usually cut off by the end of the capture.
    finished = [r for r in runs if r["done"] or r["panic"]]
    print("\n=== %d runs (%d finished)" % (len(runs), len(finished)))
    tally = {"clean": 0, "ingest": 0, "render": 0, "other": 0}
    for i, r in enumerate(runs, 1):
        if r["done"]:
            tally["clean"] += 1
            print("run %d: clean, %d reloads" % (i, r["reloads"]))
        elif r["panic"]:
            addrs = r["bt"] or [r["regs"][k] for k in ("MEPC", "RA") if k in r["regs"]]
            frames = addr2line(args.elf, addrs)
            where = "\n    ".join(f for f in frames[:8] if f) or " ".join(addrs)
            kind = classify(where + " " + (r["heap"] or ""))
            tally[kind] += 1
            print("run %d: PANIC after reload %d [%s] %s\n    %s"
                  % (i, r["reloads"], kind, r["panic"], where))
        else:
            print("run %d: unfinished at reload %d (capture ended)" % (i, r["reloads"]))
    print("=== clean=%(clean)d ingest=%(ingest)d render=%(render)d other=%(other)d" % tally)
    if not args.elf and any(r["panic"] for r in runs):
        print("(pass --elf to decode the panic addresses; 'other' may just be undecoded)")


if __name__ == "__main__":
    sys.exit(main())
