#!/usr/bin/env python3
"""
Record raw piezo samples from the board in raw mode (P3.3 jumpered to GND at
reset). The board streams L, C, R, 0xFF at 57600 baud, about 1000 rounds a
second; this writes them to a CSV you can label, plot, replay through the
firmware detector (build/replay) or use for training.

    python3 tools/groundwatch_raw.py PORT --label footstep --seconds 30
    python3 tools/groundwatch_raw.py PORT --label background --seconds 60 --plot

Output: recordings/<label>_<date>_<time>.csv with columns t_ms,L,C,R.
t_ms counts sample rounds (1 round = 0.998 ms on the board; within a round
C is 0.25 ms after L and R 0.5 ms after L).
"""
import argparse
import csv
import os
import sys
import time
from datetime import datetime

SYNC = 0xFF
BAUD = 57600


def rounds(ser):
    """Yield (L, C, R) tuples, resyncing on 0xFF whenever the framing slips."""
    buf = bytearray()
    synced = False
    while True:
        chunk = ser.read(512)
        if not chunk:
            continue
        buf += chunk
        if not synced:
            i = buf.find(bytes([SYNC]))
            if i < 0:
                buf.clear()
                continue
            del buf[:i + 1]
            synced = True
        while len(buf) >= 4:
            l, c, r, s = buf[0], buf[1], buf[2], buf[3]
            if s != SYNC or SYNC in (l, c, r):
                synced = False            # lost a byte: find the next sync
                break
            del buf[:4]
            yield l, c, r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--label", required=True, help="what is happening: footstep, digging, animal, background...")
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--outdir", default="recordings")
    ap.add_argument("--plot", action="store_true", help="plot the recording when done")
    args = ap.parse_args()

    import serial                                   # pip3 install pyserial
    os.makedirs(args.outdir, exist_ok=True)
    path = os.path.join(args.outdir, f"{args.label}_{datetime.now():%Y%m%d_%H%M%S}.csv")

    n = 0
    t_end = time.monotonic() + args.seconds
    with serial.Serial(args.port, BAUD, timeout=0.2) as ser, open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_ms", "L", "C", "R"])
        print(f"Recording '{args.label}' for {args.seconds:g} s -> {path}")
        for l, c, r in rounds(ser):
            w.writerow([n, l, c, r])
            n += 1
            if n % 1000 == 0:
                print(f"\r  {n / 1000:.0f} s  last L/C/R {l:3} {c:3} {r:3}", end="", flush=True)
            if time.monotonic() >= t_end:
                break
    print(f"\nSaved {n} samples ({n / 1000:.1f} s) to {path}")
    if n < args.seconds * 900:
        print("  fewer samples than expected: is the board in raw mode (P3.3 to GND at reset)?")

    if args.plot:
        import matplotlib.pyplot as plt
        data = list(csv.DictReader(open(path)))
        t = [int(r["t_ms"]) / 1000 for r in data]
        for z, col in zip("LCR", ["#2a78d6", "#1aa37a", "#d65a2a"]):
            plt.plot(t, [int(r[z]) for r in data], color=col, lw=0.6, label=z)
        plt.xlabel("s")
        plt.ylabel("ADC counts")
        plt.title(args.label)
        plt.legend()
        plt.show()
    return 0


if __name__ == "__main__":
    sys.exit(main())
