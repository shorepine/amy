#!/usr/bin/env python3
"""Flash one LoadTestChord build to the AMYboard and record its render load.

Flashes a compiled arduino-cli build over the debug-UART dongle by invoking
the esp32 core's esptool directly — NOT `arduino-cli upload`, because the
amyboard board definition uses 1200bps-touch + wait-for-upload-port, which
makes arduino-cli hop to the board's native USB port instead of the dongle.
The dongle's DTR/RTS lines drive the board's auto-reset circuit, so esptool
enters the ROM bootloader by itself (no BOOT/RST pressing) and recovers a
wedged board.  After flashing, pulses RTS to restart the board at a known
t=0, then tails the same UART for the `RENDER_LOAD ms=<ms> load=<0..1>`
lines that patch_render_load.py adds and the `NOTE i=<n> ...` markers the
sketch prints.  Writes <out>/{load.csv,serial.log,meta.json}.

A wedged/crashed board (prints stop early) is flagged in meta.json as
serial_died_early; the next run's esptool handshake recovers it via DTR/RTS.
Requires pyserial and an installed esp32:esp32 arduino core (for esptool).
"""
import argparse
import csv
import glob
import json
import os
import re
import subprocess
import sys
import time

RL = re.compile(r"RENDER_LOAD ms=(\d+) load=([\d.]+)")
NOTE = re.compile(r"NOTE i=(\d+) note=(\d+) ms=(\d+)")
# A power-on reset or ROM-bootloader entry mid-capture means someone else
# pulsed the board's reset lines (another harness sharing the bench) — the
# trace is invalid.  A watchdog/panic reboot is NOT interference: that's the
# commit under test crashing, which is a result (flagged board_crashed).
RESET_SIG = re.compile(r"rst:0x1 |waiting for download")
CRASH_SIG = re.compile(r"Guru Meditation|panic'ed|rst:0x[0-9a-f]+ \((?!POWERON)")
ARDUINO15 = os.path.expanduser("~/Library/Arduino15")
BENCH_LOCK = "/tmp/amyboard-bench.lock"


def acquire_bench(port, timeout_s=900):
    """Serialize bench access across processes/sessions.

    Holds an flock on BENCH_LOCK for the rest of this process AND waits for
    the serial port itself to be free (macOS cu.* devices are not exclusive,
    so a lock alone doesn't protect against harnesses that don't take it).
    """
    import fcntl
    fd = os.open(BENCH_LOCK, os.O_CREAT | os.O_RDWR, 0o666)
    deadline = time.time() + timeout_s
    waited = False
    while True:
        got_lock = False
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            got_lock = True
        except OSError:
            pass
        holders = subprocess.run(
            ["lsof", "-t", port, port.replace("/cu.", "/tty.")],
            capture_output=True, text=True).stdout.strip()
        if got_lock and not holders:
            if waited:
                print("[bench] free, proceeding")
            return fd   # keep open: lock is held until process exit
        if got_lock:
            fcntl.flock(fd, fcntl.LOCK_UN)
        if time.time() > deadline:
            sys.exit(f"[bench] {port} still busy after {timeout_s}s "
                     f"(lock={'ok' if got_lock else 'held'}, "
                     f"holders={holders or 'none'})")
        if not waited:
            print(f"[bench] waiting for {port} "
                  f"(lock={'ok' if got_lock else 'held elsewhere'}, "
                  f"holder pids={holders or '?'})")
            waited = True
        time.sleep(5.0)


def find_one(pattern, what):
    cands = sorted(glob.glob(os.path.join(ARDUINO15, pattern)))
    if not cands:
        sys.exit(f"[flash] cannot find {what} under {ARDUINO15}")
    return cands[-1]


def upload(build_dir, port, baud):
    esptool = find_one("packages/esp32/tools/esptool_py/*/esptool", "esptool")
    boot_app0 = find_one("packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin",
                         "boot_app0.bin")
    app = glob.glob(os.path.join(build_dir, "*.ino.bin"))
    if not app:
        return f"no .ino.bin in {build_dir}"
    base = app[0][:-len(".bin")]
    cmd = [esptool, "--chip", "esp32s3", "--port", port, "--baud", str(baud),
           "--before", "default-reset", "--after", "hard-reset",
           "write-flash", "-z",
           "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep",
           "0x0", base + ".bootloader.bin",
           "0x8000", base + ".partitions.bin",
           "0xe000", boot_app0,
           "0x10000", base + ".bin"]
    for attempt in (1, 2):
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=240)
        if r.returncode == 0:
            print(f"[flash] ok (attempt {attempt})")
            return None
        print(f"[flash] attempt {attempt} failed rc={r.returncode}")
        time.sleep(2.0)
    return (r.stdout or "")[-1500:] + (r.stderr or "")[-1500:]


def run_reset_pulse(ser):
    """Reset into normal run mode via the dongle (DTR deasserted, RTS pulse)."""
    ser.dtr = False
    ser.rts = True
    time.sleep(0.15)
    ser.rts = False


def capture(port, baud, seconds):
    import serial
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.reset_input_buffer()
    run_reset_pulse(ser)
    t0 = time.time()
    lines, buf = [], b""
    while time.time() - t0 < seconds:
        data = ser.read(4096)
        if data:
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                lines.append((time.time() - t0,
                              line.decode("utf-8", "replace").rstrip("\r")))
    ser.close()
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("build_dir", help="arduino-cli --build-path output dir")
    ap.add_argument("--out", required=True)
    ap.add_argument("--port", default="/dev/cu.usbserial-A5069RR4",
                    help="debug-UART dongle (flash + stderr console)")
    ap.add_argument("--flash-baud", type=int, default=921600)
    ap.add_argument("--baud", type=int, default=115200, help="console baud")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--label", default=None)
    ap.add_argument("--meta", default=None,
                    help="JSON string of extra fields for meta.json")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    meta = {"label": args.label or os.path.basename(args.out.rstrip("/")),
            "seconds": args.seconds, "errors": []}
    if args.meta:
        meta.update(json.loads(args.meta))

    acquire_bench(args.port)

    lines = None
    for attempt in (1, 2):
        err = upload(args.build_dir, args.port, args.flash_baud)
        if err:
            meta["errors"].append("upload failed")
            meta["upload_tail"] = err
            json.dump(meta, open(os.path.join(args.out, "meta.json"), "w"),
                      indent=1)
            sys.exit("[flash] upload failed twice")

        time.sleep(1.0)   # let esptool's post-flash reset settle before re-open
        print(f"[capture] {args.seconds:.0f}s on {args.port}")
        lines = capture(args.port, args.baud, args.seconds)
        if not any(RESET_SIG.search(l) and ts > 2.0 for ts, l in lines):
            break
        print("[capture] board was reset mid-capture (external interference?)"
              f" — {'retrying' if attempt == 1 else 'giving up'}")
        meta["errors"].append(f"interference on attempt {attempt}")
    else:
        with open(os.path.join(args.out, "serial.log"), "w") as f:
            for ts, line in lines:
                f.write(f"{ts:8.3f} {line}\n")
        json.dump(meta, open(os.path.join(args.out, "meta.json"), "w"), indent=1)
        sys.exit("[capture] interference on both attempts")

    with open(os.path.join(args.out, "serial.log"), "w") as f:
        for ts, line in lines:
            f.write(f"{ts:8.3f} {line}\n")

    rows, notes = [], []
    for ts, line in lines:
        m = RL.search(line)
        if m:
            rows.append((round(ts, 3), int(m.group(1)), float(m.group(2))))
        m = NOTE.search(line)
        if m:
            notes.append({"t_s": round(ts, 3), "i": int(m.group(1)),
                          "note": int(m.group(2)), "board_ms": int(m.group(3))})

    with open(os.path.join(args.out, "load.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_s", "board_ms", "load"])
        w.writerows(rows)

    meta["n_samples"] = len(rows)
    meta["notes"] = notes
    if any(CRASH_SIG.search(l) and ts > 2.0 for ts, l in lines):
        meta["board_crashed"] = True
        print("[result] board CRASHED during run (panic/watchdog reboot)")
    if rows:
        meta["load_max"] = max(r[2] for r in rows)
        meta["last_sample_t"] = rows[-1][0]
        final = [r[2] for r in rows if r[0] >= args.seconds - 3.0]
        meta["load_final"] = round(sum(final) / len(final), 4) if final else None
        if rows[-1][0] < args.seconds - 3.0:
            meta["serial_died_early"] = True   # prints stopped: crash/wedge
    else:
        meta["errors"].append("no RENDER_LOAD lines captured")
    print(f"[result] {len(rows)} samples, {len(notes)} notes, "
          f"max={meta.get('load_max')}, final={meta.get('load_final')}"
          f"{'  DIED EARLY' if meta.get('serial_died_early') else ''}")

    json.dump(meta, open(os.path.join(args.out, "meta.json"), "w"), indent=1)
    return 0 if rows else 1


if __name__ == "__main__":
    sys.exit(main())
