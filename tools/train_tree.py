#!/usr/bin/env python3
"""
Train the GroundWatch window classifier (a small decision tree) on labelled
raw recordings from the board, and export it as integer C for the 8051.

    make replay
    python3 tools/train_tree.py recordings/
    make && make test        # src/tree.c now exists, so the build uses it

Recordings come from tools/groundwatch_raw.py and are labelled by the start of
the file name (see docs/recording_checklist.md):
    footstep/person/walk -> PERSON      animal/dog/cat/goat/cow -> ANIMAL
    digging/dig/shovel   -> DIGGING     knock/rain/wind/noise   -> NOISE
    background/quiet     -> BACKGROUND
Each file is replayed through the real firmware detector, so the features are
exactly what the board computes. Windows with no detected hit are BACKGROUND
whatever the file says.

Validation is leave-one-recording-out (grouped folds): every score is on
recordings the tree never saw. It is compared with the hand-written rules in
src/fusion.c. The PIR rule ("no PIR -> not an alarm") stays in the firmware and
is not learned.

Writes src/tree.c and tests/tree_vectors.h (inputs plus the expected outputs,
so make test checks the C matches Python exactly). Delete src/tree.c to go back
to the hand rules.
"""
import argparse
import glob
import os
import subprocess
import sys

import numpy as np
import pandas as pd
from sklearn.metrics import confusion_matrix, f1_score
from sklearn.model_selection import GroupKFold
from sklearn.tree import DecisionTreeClassifier

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASSES = ["BACKGROUND", "PERSON", "ANIMAL", "DIGGING", "NOISE"]
EV = {"BACKGROUND": "EV_NOISE", "PERSON": "EV_PERSON", "ANIMAL": "EV_ANIMAL",
      "DIGGING": "EV_DIGGING", "NOISE": "EV_NOISE"}   # a window with hits the tree calls background: ignore it
FEATURES = ["hits_z", "hits_max", "zones_hit", "peak_z", "snr_z", "ms_per_hit",
            "gap_min", "gap_max", "irregular", "spread_ms"]
LABEL_WORDS = {"PERSON": ("footstep", "person", "walk"), "ANIMAL": ("animal", "dog", "cat", "goat", "cow"),
               "DIGGING": ("digging", "dig", "shovel"), "NOISE": ("knock", "rain", "wind", "noise"),
               "BACKGROUND": ("background", "quiet")}

# must match src/config.h for the rule baseline
MED_PEAK, MED_PEAK_NIGHT, STRONG_PEAK, DIG_HITS = 40, 25, 100, 3


# ---------------- features: the exact integer maths src/tree.c does ----------------
def features(df):
    """Integer features from window fields, identical to tree_features() in C."""
    Z = "LCR"
    peak = df[[f"peak_{z}" for z in Z]].to_numpy(int)
    hits = df[[f"hits_{z}" for z in Z]].to_numpy(int)
    act = df[[f"active_ms_{z}" for z in Z]].to_numpy(int)
    first = df[[f"first_ms_{z}" for z in Z]].replace("-", 65535).to_numpy(int)
    gmin = df[[f"gap_min_ms_{z}" for z in Z]].replace("-", 0).to_numpy(int)
    gmax = df[[f"gap_max_ms_{z}" for z in Z]].replace("-", 0).to_numpy(int)
    noise = df[[f"noise_{z}" for z in Z]].to_numpy(int)
    n = len(df)
    # zone: earliest onset, a tie goes to the stronger peak (fusion_zone)
    key = np.where(hits > 0, first * 256 + (255 - peak), 1 << 30)
    z = key.argmin(axis=1)
    r = np.arange(n)
    hz, pz = hits[r, z], peak[r, z]
    out = pd.DataFrame(index=df.index)
    out["hits_z"] = hz
    out["hits_max"] = hits.max(axis=1)
    out["zones_hit"] = (hits > 0).sum(axis=1)
    out["peak_z"] = pz
    out["snr_z"] = np.minimum(255, (pz * 4) // (noise[r, z] + 1))
    out["ms_per_hit"] = np.where(hz > 0, act[r, z] // np.maximum(hz, 1), 0)
    out["gap_min"] = gmin[r, z]                     # 0 when the window logged no gap ('-')
    out["gap_max"] = gmax[r, z]
    # longest gap / shortest gap, x16 (16 = perfectly regular); 0 below 3 hits
    out["irregular"] = np.where(hz >= 3, np.minimum(255, (gmax[r, z] * 16) // np.maximum(gmin[r, z], 8)), 0)
    f = np.where(hits > 0, first, -1)
    fmin = np.where(hits > 0, first, 1 << 20).min(axis=1)
    out["spread_ms"] = np.where((hits > 0).sum(axis=1) >= 2, np.minimum(2000, f.max(axis=1) - fmin), 0)
    out["zone"] = z
    out["night"] = df["night"].astype(int).to_numpy() if "night" in df else 0
    return out


def rule_baseline(F):
    """The hand-written rules in src/fusion.c (with PIR present)."""
    # fusion_regular(): at least one gap (in-window or carried) and longest <= 2 x shortest
    reg = (F.hits_z > 0) & (F.gap_min > 0) & (F.gap_max <= 2 * F.gap_min)
    return np.select(
        [F.hits_max == 0, (F.peak_z >= np.where(F.night == 1, MED_PEAK_NIGHT, MED_PEAK)) & reg,
         (F.peak_z >= STRONG_PEAK) & (F.hits_z >= DIG_HITS)],
        ["BACKGROUND", "PERSON", "DIGGING"], "ANIMAL")


# ---------------- data ----------------
def load_recordings(folder):
    """Replay every labelled recording through the detector and label its windows."""
    replay = os.path.join(REPO, "build", "replay")
    rows = []
    for path in sorted(glob.glob(os.path.join(folder, "*.csv"))):
        name = os.path.basename(path).lower()
        label = next((c for c, words in LABEL_WORDS.items() if name.startswith(words)), None)
        if label is None:
            print(f"  skipped {name}: no class word at the start of the name")
            continue
        res = subprocess.run([replay, path, "--pir", "1"], capture_output=True, text=True, check=True)
        for line in res.stdout.splitlines():
            if line.startswith("EV,"):
                rows.append([name, label] + line.split(",")[3:])
    cols = ["group", "label"] + [f"{k}_{z}" for k in ("peak", "hits", "active_ms", "first_ms", "gap_min_ms",
                                                      "gap_max_ms", "noise") for z in "LCR"] + ["pir", "night", "ldr"]
    d = pd.DataFrame(rows, columns=cols)
    for c in cols[2:]:
        d[c] = pd.to_numeric(d[c], errors="ignore")
    hits = d[["hits_L", "hits_C", "hits_R"]].astype(int).sum(axis=1)
    d.loc[hits == 0, "label"] = "BACKGROUND"
    d["origin"] = "board"
    return d


# ---------------- evaluation ----------------
def report(name, y, p, labels):
    labels = [c for c in labels if c in set(y) | set(p)]
    cm = confusion_matrix(y, p, labels=labels)
    print(f"\n{name}: macro-F1 {f1_score(y, p, labels=labels, average='macro', zero_division=0):.3f}, "
          f"accuracy {np.mean(np.asarray(y) == np.asarray(p)):.3f}")
    print("  true \\ predicted " + "".join(f"{c[:10]:>11}" for c in labels) + "   recall")
    for i, c in enumerate(labels):
        rec = cm[i, i] / cm[i].sum() if cm[i].sum() else float("nan")
        print(f"  {c:17}" + "".join(f"{v:11d}" for v in cm[i]) + f"   {rec:6.1%}")


def cross_validate(F, y, groups, w, depth, leaf, eval_mask):
    pred = np.empty(len(y), dtype=object)
    k = min(5, len(set(groups)))
    for tr, te in GroupKFold(n_splits=k).split(F, y, groups):
        m = DecisionTreeClassifier(max_depth=depth, min_samples_leaf=leaf, class_weight="balanced", random_state=0)
        m.fit(F.iloc[tr][FEATURES], y[tr], sample_weight=w[tr])
        pred[te] = m.predict(F.iloc[te][FEATURES])
    yy, pp = y[eval_mask], pred[eval_mask]
    return f1_score(yy, pp, average="macro", zero_division=0), pred


# ---------------- export ----------------
def export_c(model, path):
    """Nested ifs on tree_f fields. Each split is a 16-bit compare on indirect RAM,
    about 15 bytes of 8051 code, so a depth-5 tree (at most 31 splits) is under 0.5 KB."""
    t = model.tree_
    names = list(model.classes_)

    def node(i, ind):
        pad = "    " * ind
        if t.children_left[i] == -1:
            c = names[int(np.argmax(t.value[i][0]))]
            return f"{pad}return {EV[c]};   /* {c} */\n"
        f = FEATURES[t.feature[i]]
        thr = int(np.floor(t.threshold[i]))           # x <= 3.5  ==  x <= 3 for integers
        return (f"{pad}if (tree_f.{f} <= {thr}) {{\n" + node(t.children_left[i], ind + 1)
                + f"{pad}}} else {{\n" + node(t.children_right[i], ind + 1) + f"{pad}}}\n")

    with open(path, "w") as fh:
        fh.write(f"""/*
 * GENERATED by tools/train_tree.py - do not edit by hand; retrain instead.
 * Decision tree, depth {model.get_depth()}, {model.get_n_leaves()} leaves, classes {", ".join(names)}.
 * Features are integers computed from the 2 s window (see tree_features).
 */
#include "tree.h"

__idata tree_feat_t tree_f;

/* The 8051's directly addressed RAM is nearly full (the sampling ISR needs
 * it), so the working variables live in indirect RAM and the window comes in
 * through a 1-byte indirect pointer (main keeps it in __idata). */
void tree_features(const window_t __idata *w, uint8_t z)
{{
    tree_feat_t __idata *f = &tree_f;
    static __idata uint8_t  i, hz, n;
    static __idata uint16_t fmin, fmax, gmin, gmax, d;

    hz = w->hits[z];
    n = 0;
    fmin = 0xFFFF;
    fmax = 0;
    gmin = w->gap_min[z] * 8u;
    gmax = w->gap_max[z] * 8u;

    f->hits_z = hz;
    f->hits_max = 0;
    for (i = 0; i < N_PIEZO; i++) {{
        if (w->hits[i] > f->hits_max) f->hits_max = w->hits[i];
        if (w->hits[i]) {{
            n++;
            if (w->first[i] < fmin) fmin = w->first[i];
            if (w->first[i] > fmax) fmax = w->first[i];
        }}
    }}
    f->zones_hit = n;
    f->peak_z = w->peak[z];
    d = (uint16_t)w->noise[z] + 1;
    f->snr_z = ((uint16_t)w->peak[z] * 4) / d;
    if (f->snr_z > 255) f->snr_z = 255;
    f->ms_per_hit = hz ? w->active[z] / hz : 0;
    f->gap_min = (w->gap_min[z] != T8_NONE) ? gmin : 0;
    f->gap_max = (w->gap_min[z] != T8_NONE) ? gmax : 0;
    if (hz >= 3) {{
        d = (gmin < 8) ? 8 : gmin;
        f->irregular = (gmax * 16) / d;
        if (f->irregular > 255) f->irregular = 255;
    }} else {{
        f->irregular = 0;
    }}
    f->spread_ms = (n >= 2) ? fmax - fmin : 0;
    if (f->spread_ms > 2000) f->spread_ms = 2000;
}}

uint8_t tree_predict(void)
{{
{node(0, 1)}}}
""")


def export_vectors(F, pred, path, n=300):
    idx = np.random.default_rng(1).choice(len(F), size=min(n, len(F)), replace=False)
    with open(path, "w") as fh:
        fh.write("/* GENERATED by tools/train_tree.py: features and the Python tree's output */\n")
        fh.write("static const struct { uint16_t f[%d]; uint8_t ev; } tree_vectors[] = {\n" % len(FEATURES))
        for i in idx:
            vals = ", ".join(str(int(F.iloc[i][c])) for c in FEATURES)
            fh.write(f"    {{ {{ {vals} }}, {EV[pred[i]]} }},\n")
        fh.write("};\n")


# ---------------- main ----------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recordings", help="folder of labelled raw recordings (CSV from groundwatch_raw.py)")
    ap.add_argument("--max-depth", type=int, default=0, help="fix the depth (default: pick by validation, 2-5)")
    ap.add_argument("--min-leaf", type=int, default=10, help="smallest number of windows in a leaf")
    ap.add_argument("--no-export", action="store_true", help="only report, don't write src/tree.c")
    args = ap.parse_args()

    if not os.path.exists(os.path.join(REPO, "build", "replay")):
        print("build/replay missing: run 'make replay' first")
        return 1
    d = load_recordings(args.recordings)
    if d.empty:
        print("no labelled recordings found")
        return 1
    F = features(d)
    y = d.label.to_numpy(object)
    groups = d.group.to_numpy()

    print("windows per class (files per class):")
    files = d.groupby("label").group.nunique()
    for c, n in d.label.value_counts().items():
        print(f"  {c:11} {n:5}  ({files[c]} files)")
    thin = [c for c in set(y) if files[c] < 2]
    if thin or len(set(groups)) < 3:
        print(f"\nneed at least 2 recordings per class to validate (short: {', '.join(sorted(thin)) or 'files'})")
        return 1

    depths = [args.max_depth] if args.max_depth else [2, 3, 4, 5]   # deeper won't fit 8 KB comfortably
    best = None
    print("\nleave-recording-out macro-F1 by depth:")
    for depth in depths:
        f1, pred = cross_validate(F, y, groups, np.ones(len(y)), depth, args.min_leaf, np.ones(len(y), bool))
        print(f"  depth {depth}: {f1:.3f}")
        if best is None or f1 > best[1] + 0.01:         # prefer the shallower tree unless clearly better
            best = (depth, f1, pred)
    depth, _, pred = best
    print(f"\nchosen depth {depth}")
    report("trained tree (recordings it never saw)", y, pred, CLASSES)
    report("current hand rules", y, rule_baseline(F), CLASSES)

    model = DecisionTreeClassifier(max_depth=depth, min_samples_leaf=args.min_leaf,
                                   class_weight="balanced", random_state=0)
    model.fit(F[FEATURES], y)
    imp = sorted(zip(model.feature_importances_, FEATURES), reverse=True)
    print("\nfeature importance: " + ", ".join(f"{n} {v:.2f}" for v, n in imp if v > 0.005))
    if not args.no_export:
        export_c(model, os.path.join(REPO, "src", "tree.c"))
        export_vectors(F, model.predict(F[FEATURES]), os.path.join(REPO, "tests", "tree_vectors.h"))
        print("wrote src/tree.c and tests/tree_vectors.h: run make && make test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
