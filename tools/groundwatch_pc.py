#!/usr/bin/env python3
"""
GroundWatch PC side: live log + plot of the 8051's 2-second windows.

    python3 tools/groundwatch_pc.py --list                    # find the USB-UART port
    python3 tools/groundwatch_pc.py /dev/cu.usbserial-0001    # log to windows.csv + plot
    python3 tools/groundwatch_pc.py PORT --label footstep     # tag rows while recording
    python3 tools/groundwatch_pc.py --demo                    # fake data, no hardware
    python3 tools/groundwatch_pc.py PORT --cloud              # also upload to Supabase

Every row in the CSV is one window. Plot the peaks per class to pick the
thresholds in src/config.h (PIEZO_THR_*, MED_PEAK, STRONG_PEAK, DIG_HITS).

--cloud reads SUPABASE_URL and SUPABASE_SECRET_KEY from the environment or from
a .env file at the repo root (copy .env.example). Never commit .env: the secret
key can write to the database.
"""
import argparse
import csv
import json
import queue
import urllib.error
import urllib.request
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
          + [f"gap_min_ms_{z}" for z in "LCR"] + [f"gap_max_ms_{z}" for z in "LCR"]
          + [f"noise_{z}" for z in "LCR"]
          + ["pir", "night", "ldr"])
ALARM = {"PERSON", "DIGGING"}
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
    yield "GW,ready"
    while True:
        time.sleep(2)
        ev = random.choices(["NONE", "NOISE", "ANIMAL", "PERSON", "DIGGING"],
                            [45, 15, 12, 20, 8])[0]
        z = random.randrange(3)
        pk = {"NONE": 0, "NOISE": 30, "ANIMAL": 30, "PERSON": 70, "DIGGING": 160}[ev]
        peaks = [max(0, pk - abs(i - z) * 20 + random.randint(-8, 8)) if pk else 0 for i in range(3)]
        hits = [random.randint(3 if ev == "DIGGING" else 1, 5) if p > 20 else 0 for p in peaks]
        first = [str(200 + abs(i - z) * 40) if h else "-" for i, h in enumerate(hits)]
        gap = 500 if ev == "PERSON" else 300
        gmin = [str(gap - (random.randint(150, 250) if ev == "ANIMAL" else random.randint(0, 40)))
                if h >= 2 else "-" for h in hits]
        gmax = [str(gap + (random.randint(300, 600) if ev == "ANIMAL" else random.randint(0, 60)))
                if h >= 2 else "-" for h in hits]
        noise = [str(random.randint(1, 3) + (4 if ev == "NOISE" else 0)) for _ in range(3)]
        yield ",".join(["EV", ev, "LCR"[z] if pk else "-"]
                       + [str(p) for p in peaks] + [str(h) for h in hits]
                       + [str(h * 60) for h in hits] + first + gmin + gmax + noise
                       + [str(int(ev in ("ANIMAL", "PERSON", "DIGGING"))), "0", "140"])


def load_env(path):
    """Minimal .env reader: KEY=value lines, # comments. Real env vars win."""
    env = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    env[k.strip()] = v.strip().strip('"').strip("'")
    env.update({k: v for k, v in os.environ.items() if k.startswith("SUPABASE_")})
    return env


def to_record(row, label, device):
    """CSV-style row dict -> events table record"""
    def num(v):
        return None if v in ("", "-") else int(v)
    rec = {"device": device, "event": row["event"],
           "zone": None if row["zone"] == "-" else row["zone"],
           "pir": row["pir"] == "1", "night": row["night"] == "1",
           "ldr": num(row["ldr"]), "label": label or None}
    for z in "LCR":
        rec[f"peak_{z.lower()}"] = num(row[f"peak_{z}"])
        rec[f"hits_{z.lower()}"] = num(row[f"hits_{z}"])
        rec[f"active_ms_{z.lower()}"] = num(row[f"active_ms_{z}"])
        rec[f"first_ms_{z.lower()}"] = num(row[f"first_ms_{z}"])
        rec[f"gap_min_ms_{z.lower()}"] = num(row[f"gap_min_ms_{z}"])
        rec[f"gap_max_ms_{z.lower()}"] = num(row[f"gap_max_ms_{z}"])
        rec[f"noise_{z.lower()}"] = num(row[f"noise_{z}"])
    return rec


class Cloud:
    """Uploads records to Supabase in the background, in small batches.
    If the network drops, rows wait in memory (up to MAX_PENDING) and go up later."""
    MAX_PENDING = 2000

    def __init__(self, url, key):
        self.endpoint = url.rstrip("/") + "/rest/v1/events"
        self.headers = {"apikey": key, "Content-Type": "application/json",
                        "Prefer": "return=minimal"}
        if key.startswith("eyJ"):                  # legacy service_role JWT
            self.headers["Authorization"] = "Bearer " + key
        self.q = queue.Queue(self.MAX_PENDING)
        self.sent = 0
        self.failing = False
        threading.Thread(target=self._run, daemon=True).start()

    def put(self, rec):
        try:
            self.q.put_nowait(rec)
        except queue.Full:
            pass                                   # offline too long: newest rows are dropped

    def _post(self, batch):
        req = urllib.request.Request(self.endpoint, data=json.dumps(batch).encode(),
                                     headers=self.headers, method="POST")
        urllib.request.urlopen(req, timeout=10).close()

    def _run(self):
        batch = []
        while True:
            try:
                batch.append(self.q.get(timeout=1))
                while len(batch) < 50:
                    batch.append(self.q.get_nowait())
            except queue.Empty:
                pass
            if not batch:
                continue
            try:
                self._post(batch)
                self.sent += len(batch)
                if self.failing:
                    print(f"cloud: back online, {self.sent} rows uploaded")
                self.failing = False
                batch = []
            except urllib.error.HTTPError as e:
                print(f"cloud: upload rejected ({e.code}): {e.read().decode(errors='ignore')[:200]}")
                if 400 <= e.code < 500:
                    batch = []                     # bad key or bad row: retrying won't help
                time.sleep(5)
            except OSError as e:
                if not self.failing:
                    print(f"cloud: offline ({e}), will retry")
                self.failing = True
                time.sleep(5)


def reader(lines, rows, out_path, label, quiet, cloud=None, device="gw-01"):
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
            if cloud:
                cloud.put(to_record(row, label, device))
            if not quiet or row["event"] != "NONE":
                flag = "  <-- ALARM" if row["event"] in ALARM else ""
                print(f"{now}  {row['event']:8} zone {row['zone']}  "
                      f"peaks L/C/R {row['peak_L']:>3} {row['peak_C']:>3} {row['peak_R']:>3}  "
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
        ax2.plot(x, [int(r["ldr"]) for r in data], color="#c9a227", label="LDR")
        ax2.bar(x, [int(r["pir"]) * 255 for r in data], color="#7a4fd6", alpha=0.3,
                label="PIR active")
        ax2.set_ylim(0, 260)
        ax2.set_ylabel("light / PIR")
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
    ap.add_argument("--cloud", action="store_true", help="also upload every window to Supabase")
    ap.add_argument("--device", default=None, help="device name in the database (default gw-01, or demo)")
    args = ap.parse_args()

    if args.list:
        from serial.tools import list_ports
        for p in list_ports.comports():
            print(p.device, "-", p.description)
        return 0
    if not args.port and not args.demo:
        ap.error("give a serial port (try --list) or --demo")

    cloud = None
    device = args.device or ("demo" if args.demo else "gw-01")
    if args.cloud:
        env = load_env(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".env"))
        if not env.get("SUPABASE_URL") or not env.get("SUPABASE_SECRET_KEY"):
            ap.error("--cloud needs SUPABASE_URL and SUPABASE_SECRET_KEY (copy .env.example to .env)")
        cloud = Cloud(env["SUPABASE_URL"], env["SUPABASE_SECRET_KEY"])
        print(f"Uploading to {env['SUPABASE_URL']} as device '{device}'")

    lines = demo_lines() if args.demo else serial_lines(args.port, args.baud)
    rows = deque(maxlen=HISTORY)
    print(f"Logging to {args.out}  (Ctrl+C to stop)")

    if args.no_plot:
        try:
            reader(lines, rows, args.out, args.label, args.quiet, cloud, device)
        except KeyboardInterrupt:
            pass
        return 0

    threading.Thread(target=reader, args=(lines, rows, args.out, args.label, args.quiet, cloud, device),
                     daemon=True).start()
    try:
        plot(rows)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
