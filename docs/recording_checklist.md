# Recording training data

The classifier that decides PERSON / ANIMAL / DIGGING / NOISE is trained on recordings from **our own board, piezos and soil**. Public datasets use different sensors (geophones) on different ground, so they can't stand in. This page covers how to record, and how to turn the recordings into firmware.

Until enough recordings exist, the board uses the hand-written rules in `src/fusion.c`. The PIR rule ("no PIR → not an alarm") is always applied and isn't learned, so raw recordings don't need the PIR.

## 1. Before you start

- [ ] The board is wired and passes the bring-up steps in [`connections.md`](../connections.md): the LCD works and the piezo readings respond to taps.
- [ ] Piezos are buried at the **same depth** (for example 5–10 cm) and spacing as they will be in the demo. Write both down.
- [ ] The **P3.3 jumper is to GND**, then press reset. The LCD should show `Raw 57600`.
- [ ] The USB-UART is connected. Find the port with `python3 tools/groundwatch_pc.py --list`.
- [ ] Wait 10 s after reset so the board's baselines settle.

## 2. Record one class per file

```sh
python3 tools/groundwatch_raw.py /dev/cu.usbserial-0001 --label footstep --seconds 60
```

The **first word of the label is the class**. The tool adds the date and time to the file name.

| Label (first word) | Class | What to do |
|---|---|---|
| `background` | BACKGROUND | Nobody within about 5 m. Stand still and stay quiet. |
| `footstep` | PERSON | Walk past or across the line, and keep walking for the whole recording (turn round and come back). |
| `animal` | ANIMAL | A pet walking near the line. Skip this class if nobody has a pet; the rule stays in charge of it. |
| `digging` | DIGGING | Shovel or trowel strikes into the soil 1–2 m from a piezo. |
| `knock` | NOISE | Isolated events, a few seconds apart: drop a stone, a single stamp, a stick tapped on the ground. |
| `rain` / `wind` | NOISE | Whenever the weather obliges. |

**Aim for:** at least **5 recordings of 60 s per class**, made on at least 2 different days. Variety matters more than length:
- **Footsteps:**
  - all three of you, plus anyone else willing
  - slow, normal and fast walking, and a run
  - different shoes, and both directions
  - passing at about 0.5 m, 1 m and 2 m from the line
  - crossing at L, at C and at R
- **Background:** quiet times, plus windy times and times with traffic or machinery nearby.
- **Digging:** soft and hard strikes, at different distances.

The trainer needs at least **2 files per class** before it will run.

## 3. Keep it clean

- One class per file. If someone wanders through during a `background` take, delete the file and redo it.
- Note each session in `recordings/notes.md`: date, weather, soil (dry or wet), piezo depth, who walked.
- Recordings are git-ignored, because they're large. Put the `recordings/` folder in the team's shared drive, and never edit or rename a file except to fix its label.

## 4. Train and check

```sh
make replay                                   # the host build of the firmware detector
python3 tools/train_tree.py recordings/       # trains, validates, writes src/tree.c
```

The report shows the tree's scores **on recordings it never saw** (each file is held out in turn), next to the current hand rules on the same windows. Read the confusion matrix: missing a PERSON is worse than calling an animal a person.

- **If the tree is better than the rules:** run `make && make test`. The tests check that the C tree gives the same answers as Python. Then flash `build/groundwatch.hex`.
- **If it isn't:** delete `src/tree.c`, which puts the rules back, and record more varied data. The detector thresholds in `src/config.h` can also be tuned by replaying files: `./build/replay recordings/footstep_*.csv`.

## 5. Confirm on the board

Remove the P3.3 jumper and reset, which returns the board to normal mode. Walk past with the bridge running (`groundwatch_pc.py PORT --cloud`). Check that the LCD, the alarm and the dashboard agree with what you did.
