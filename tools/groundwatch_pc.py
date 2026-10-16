#!/usr/bin/env python3
"""
GroundWatch PC side: live log + plot of the 8051's 2-second windows.

    python3 tools/groundwatch_pc.py --list                    # find the USB-UART port
    python3 tools/groundwatch_pc.py /dev/cu.usbserial-0001    # log to windows.csv + plot
    python3 tools/groundwatch_pc.py PORT --label footstep     # tag rows while recording
    python3 tools/groundwatch_pc.py --demo                    # fake data, no hardware

Every row in the CSV is one window. Plot the peaks per class to pick the
thresholds in src/config.h (PIEZO_THR_*, MED_PEAK, STRONG_PEAK, SOUND_THR, MAG_THR).
"""
import argparse
import csv
import os
import random
import sys
import threading
import time
from collections import deque
from datetime import datetime

FIELDS = (["event", "zone"]
          + [f"peak_{z}" for z in "LCR"] + [f"hits_{z}" for z in "LCR"]
          + [f"active_ms_{z}" for z in "LCR"] + [f"first_ms_{z}" for z in "LCR"]
          + ["mag_dev", "sound_bursts", "pir", "night", "ldr"])
ALARM = {"PERSON", "VEHICLE", "DIGGING"}
HISTORY = 60          # windows on screen (2 min)


def parse(line):
    """'EV,PERSON,C,87,...' -> dict, or None for anything else"""
    if not line.startswith("EV,"):
        return None
    vals = line.split(",")[1:]
    if len(vals) != len(FIELDS):
        return None
    return dict(zip(FIELDS, vals))


def serial_lines(port, baud):
    import serial                                   # pip3 install pyserial
    with serial.Serial(port, baud, timeout=1) as ser:
        while True:
            yield ser.readline().decode(errors="ignore").strip()


def demo_lines():
    """Plausible fake windows so the plot can be tried without the board"""
    yield "GW,ready,mag=1"
    while True:
        time.sleep(2)
        ev = random.choices(["NONE", "NOISE", "ANIMAL", "PERSON", "VEHICLE", "DIGGING"],
                            [40, 15, 10, 20, 8, 7])[0]
        z = random.randrange(3)
        pk = {"NONE": 0, "NOISE": 30, "ANIMAL": 30, "PERSON": 70, "VEHICLE": 50, "DIGGING": 160}[ev]
        peaks = [max(0, pk - abs(i - z) * 20 + random.randint(-8, 8)) if pk else 0 for i in range(3)]
        hits = [random.randint(1, 4) if p > 20 else 0 for p in peaks]
        first = [str(200 + abs(i - z) * 40) if h else "-" for i, h in enumerate(hits)]
        yield ",".join(["EV", ev, "LCR"[z] if pk else "-"]
                       + [str(p) for p in peaks] + [str(h) for h in hits]
                       + [str(h * 60) for h in hits] + first
                       + [str(900 if ev == "VEHICLE" else random.randint(0, 80)),
                          str(5 if ev == "DIGGING" else random.randint(0, 1)),
                          str(int(ev in ("ANIMAL", "PERSON", "DIGGING"))), "0", "140"])


def reader(lines, rows, out_path, label, quiet):
    new_file = not os.path.exists(out_path)
    with open(out_path, "a", newline="") as f:
        w = csv.writer(f)
        if new_file:
            w.writerow(["time", "label"] + FIELDS)
        for line in lines:
            if line.startswith("GW,"):
                print("board:", line)
                continue
            row = parse(line)
            if row is None:
                if line:
                    print("  ?", line)
                continue
            now = datetime.now().strftime("%H:%M:%S")
            w.writerow([now, label] + [row[k] for k in FIELDS])
            f.flush()
            rows.append(row)
            if not quiet or row["event"] != "NONE":
                flag = "  <-- ALARM" if row["event"] in ALARM else ""
                print(f"{now}  {row['event']:8} zone {row['zone']}  "
                      f"peaks L/C/R {row['peak_L']:>3} {row['peak_C']:>3} {row['peak_R']:>3}  "
                      f"mag {row['mag_dev']:>4}  snd {row['sound_bursts']}  "
                      f"pir {row['pir']}{flag}")


def plot(rows):
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation

    fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(10, 6))
    fig.canvas.manager.set_window_title("GroundWatch")
    colors = {"L": "#2a78d6", "C": "#1aa37a", "R": "#d65a2a"}

    def draw(_):
        data = list(rows)
        x = range(len(data))
        ax1.clear()
        ax2.clear()
        for z, c in colors.items():
            ax1.plot(x, [int(r[f"peak_{z}"]) for r in data], color=c, label=f"zone {z}")
        for i, r in enumerate(data):
            if r["event"] not in ("NONE", "NOISE"):
                ax1.annotate(r["event"], (i, int(r[f"peak_{r['zone']}"]) if r["zone"] in colors else 0),
                             fontsize=7, rotation=60,
                             color="crimson" if r["event"] in ALARM else "gray")
        ax1.set_ylabel("piezo peak (ADC counts)")
        ax1.legend(loc="upper left", fontsize=8)
        ax1.set_title("Piezo peaks per 2 s window")
        ax2.plot(x, [int(r["mag_dev"]) for r in data], color="#7a4fd6", label="mag dev")
        ax2.bar(x, [int(r["sound_bursts"]) * 50 for r in data], color="#c9a227", alpha=0.5,
                label="sound bursts x50")
        ax2.set_ylabel("magnetometer / sound")
        ax2.set_xlabel(f"window (last {HISTORY})")
        ax2.legend(loc="upper left", fontsize=8)

    _anim = FuncAnimation(fig, draw, interval=1000, cache_frame_data=False)
    plt.tight_layout()
    plt.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="serial port, e.g. /dev/cu.usbserial-0001")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--out", default="windows.csv")
    ap.add_argument("--label", default="", help="tag rows with what you are doing (footstep, vehicle...)")
    ap.add_argument("--no-plot", action="store_true")
    ap.add_argument("--quiet", action="store_true", help="don't print NONE windows")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--demo", action="store_true", help="fake data, no board needed")
    args = ap.parse_args()

    if args.list:
        from serial.tools import list_ports
        for p in list_ports.comports():
            print(p.device, "-", p.description)
        return 0
    if not args.port and not args.demo:
        ap.error("give a serial port (try --list) or --demo")

    lines = demo_lines() if args.demo else serial_lines(args.port, args.baud)
    rows = deque(maxlen=HISTORY)
    print(f"Logging to {args.out}  (Ctrl+C to stop)")

    if args.no_plot:
        try:
            reader(lines, rows, args.out, args.label, args.quiet)
        except KeyboardInterrupt:
            pass
        return 0

    threading.Thread(target=reader, args=(lines, rows, args.out, args.label, args.quiet),
                     daemon=True).start()
    try:
        plot(rows)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
