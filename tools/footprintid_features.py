#!/usr/bin/env python3
"""
Check the detector and the hand rules on real walking, using FootprintID.

FootprintID (Pan et al. 2017; Dong et al. 2021, doi:10.5281/zenodo.4691144,
CC BY 4.0): 10 people walking a hallway at 8 controlled speeds, recorded by
geophones at 1 kHz. Sensors 1, 2, 3 sit in a line at 0, 15 and 30 ft, the
same layout as GroundWatch's L, C, R. Only people 1-6 have all three.

NOT training data for GroundWatch: geophones measure ground velocity on a
concrete floor, while GroundWatch uses piezo discs (strain) in soil, so the
waveforms, ringing and amplitudes differ. What does carry over is the timing of
walking (step gaps across speeds) and the walker's pass along the line, which
makes it a fair check that the detector finds real footsteps and that the
rules don't misread them (it is how the digging-rule bug was found).

Each trace runs through the real firmware detector (build/replay compiles
src/sense.c on the host), at three gains (raw counts / 8, / 4, / 2). Windows:
    PERSON      overlaps the walker's pass (the longest run of activity on any
                of the three sensors) by at least 0.5 s
    BACKGROUND  at least 1 s clear of every run of activity
Edge windows and windows near a stray blip are dropped, and so are PERSON
windows where the detector found nothing.

    make replay
    python3 tools/footprintid_features.py datasets/footprintid/People.mat datasets/footprintid_windows.csv
"""
import csv
import os
import subprocess
import sys
import tempfile

import numpy as np
import scipy.io as sio

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPLAY = os.path.join(REPO, "build", "replay")
SETTLE_MS, WINDOW_MS = 1500, 2000          # must match tests/replay.c and src/config.h
GAINS = {"x0.5": 1 / 8, "x1": 1 / 4, "x2": 1 / 2}   # raw ±512 counts -> 8-bit around 128

FIELDS = (["event", "zone"]
          + [f"{k}_{z}" for k in ("peak", "hits", "active_ms", "first_ms", "gap_min_ms", "gap_max_ms", "noise")
             for z in "LCR"]
          + ["pir", "night", "ldr"])


def trace(P, person, sensor, speed, t, amp=0):
    return P[0, person]["Sen"][0, 0][0, sensor]["S"][0, 0][0, speed][t, amp].ravel().astype(float)


def activity_runs(chans, block=250, k=3.0, merge_ms=2000):
    """Runs of activity (ms), merged across channels: a 250 ms block is active when
    its mean |x| is over k x that channel's median block. Runs closer than
    merge_ms join (the gaps between footsteps). Returns [(start, end), ...]."""
    n = min(len(x) for x in chans) // block
    act = np.zeros(n, bool)
    for x in chans:
        d = np.abs(x[: n * block] - np.median(x))
        env = d.reshape(n, block).mean(axis=1)
        act |= env > k * np.median(env)
    runs, start = [], None
    for i, a in enumerate(np.append(act, False)):
        if a and start is None:
            start = i
        elif not a and start is not None:
            runs.append([start * block, i * block])
            start = None
    merged = []
    for r in runs:
        if merged and r[0] - merged[-1][1] <= merge_ms:
            merged[-1][1] = r[1]
        else:
            merged.append(r)
    return [tuple(r) for r in merged]


def replay_windows(data, tmp):
    """Run (n, 3) uint8 samples through the firmware detector; return the window feature lists."""
    path = os.path.join(tmp, "t.bin")
    data.tofile(path)
    res = subprocess.run([REPLAY, path, "--pir", "1"], capture_output=True, text=True, check=True)
    return [l.split(",")[3:] for l in res.stdout.splitlines() if l.startswith("EV,")]


def to8(chans, gain):
    return np.clip(np.round(128 + chans * gain), 0, 254).astype(np.uint8)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    mat, out = sys.argv[1], sys.argv[2]
    if not os.path.exists(REPLAY):
        print("build/replay missing: run 'make replay' first")
        return 1
    P = sio.loadmat(mat)["people"]
    tmp = tempfile.mkdtemp(prefix="gw_fp_")

    traces = []
    for person in range(P.shape[1]):
        sen = P[0, person]["Sen"][0, 0]
        if any(sen[0, s].size == 0 or sen[0, s].dtype.names is None for s in (0, 1, 2)):
            print(f"person {person + 1}: sensors 1-3 not all recorded, skipped")
            continue
        n_speeds = sen[0, 0]["S"][0, 0].shape[1]
        for speed in range(n_speeds):
            n_traces = min(sen[0, s]["S"][0, 0][0, speed].shape[0] for s in (0, 1, 2))
            for t in range(n_traces):
                ch = [trace(P, person, s, speed, t) for s in (0, 1, 2)]
                n = min(len(c) for c in ch)
                chans = np.stack([c[:n] - np.median(c[:n]) for c in ch], axis=1)
                runs = activity_runs(list(chans.T))
                if not runs:
                    continue
                walk = max(runs, key=lambda r: r[1] - r[0])
                traces.append((person + 1, speed + 1, t + 1, chans, runs, walk))
    print(f"{len(traces)} traces")

    rows, counts = 0, {}
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["person", "speed", "trace", "gain", "window", "label"] + FIELDS[2:])

        def emit(meta, label, vals, wi):
            nonlocal rows
            if label == "PERSON" and sum(int(v) for v in vals[3:6]) == 0:
                counts["PERSON missed by the detector"] = counts.get("PERSON missed by the detector", 0) + 1
                return
            w.writerow(list(meta) + [wi, label] + vals)
            counts[label] = counts.get(label, 0) + 1
            rows += 1

        for i, (person, speed, t, chans, runs, (first, last)) in enumerate(traces):
            for gname, g in GAINS.items():
                meta = (person, speed, t, gname)
                # real windows
                for wi, vals in enumerate(replay_windows(to8(chans, g), tmp)):
                    a = SETTLE_MS + wi * WINDOW_MS
                    b = a + WINDOW_MS
                    overlap = max(0, min(b, last) - max(a, first))
                    clear = all(b <= r0 - 1000 or a >= r1 + 1000 for r0, r1 in runs)
                    if overlap >= 500:
                        emit(meta, "PERSON", vals, wi)
                    elif clear:
                        emit(meta, "BACKGROUND", vals, wi)
            if (i + 1) % 100 == 0:
                print(f"  {i + 1}/{len(traces)} traces, {rows} windows", flush=True)
    print(f"wrote {rows} windows to {out}")
    for k in sorted(counts):
        print(f"  {k:30} {counts[k]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
