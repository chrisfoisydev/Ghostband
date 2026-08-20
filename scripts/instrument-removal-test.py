#!/usr/bin/env python3
"""Blind listening test: can a prompt actually remove an instrument?

WHY THIS EXISTS
---------------
The design canvas shows a row of instrument chips under "BAND", implying the performer
can choose which instruments play. MRT2 has exactly one real per-instrument control:

    void set_drumless(bool on);   // drums, and only drums

Everything else would have to be done by asking in the prompt — "no piano", "upright bass
only". A generative model *usually* complies. `CLAUDE.md` rule 2 forbids drawing a control
that looks live and is not, so the question is not "does it ever work" but "how often", and
that is measurable rather than arguable.

THE DECISION RULE, COMMITTED BEFORE LISTENING
---------------------------------------------
A test without a threshold agreed in advance is not a decision procedure, it is just
listening and then rationalising. So:

  >= 90% removed  ->  worth building as clearly-labelled "ASK THE BAND FOR" chips that
                      visibly compose prompt text. Never as switches: even 9 in 10 is a
                      control that fails on stage, and the performer has no recourse
                      mid-song.
  60-90%          ->  still worth building, but DRUMS must be visually distinguished as
                      the one that is guaranteed, and the wording must be softer still.
  < 60%           ->  drop the chips. The DESCRIBE THE BAND free-text field is better and
                      is already honest about being a description.

DRUMS is included as a control in both senses: it is the one instrument with a real API,
so it also calibrates how the prompt route compares to the guaranteed route.

USAGE
-----
    ./scripts/instrument-removal-test.py \\
        --model  ~/Documents/Magenta/magenta-rt-v2/models/mrt2_small/mrt2_small.mlxfn \\
        --resources ~/Documents/Magenta/magenta-rt-v2/resources \\
        --hello  ~/magenta-realtime/build/examples/hello_mrt2/hello_mrt2

    # ... listen to every clip in the output directory, fill in scores.csv ...

    ./scripts/instrument-removal-test.py --reveal --out <dir>

Clips are named by a shuffled index and the answer key is written to a file you are asked
not to open until you have scored. Knowing which clip is the "no piano" one makes you hear
its absence — that is not a small effect, and this decision is not worth biasing.
"""

import argparse
import csv
import json
import random
import shutil
import subprocess
import sys
from pathlib import Path

# Matches GhostBand's demo song, so the result is about the app's own prompt style rather
# than some prompt invented for the test.
BASE_PROMPT = ("warm organic indie folk ensemble, upright bass, brushed percussion, "
               "atmospheric piano, supportive accompaniment, instrumental")

# Each entry: the chip label, and the phrase a chip would add to the prompt.
INSTRUMENTS = [
    ("DRUMS", "no drums, no percussion"),
    ("PIANO", "no piano, no keys"),
    ("BASS",  "no bass"),
]

TRIALS_PER_INSTRUMENT = 6   # MRT2 is stochastic; one clip proves nothing
FRAMES = 200                # 200 * 40 ms = 8 s, long enough to hear an absence


def generate(hello, model, resources, prompt: str,
             dest: Path, frames: int, dry_run: bool) -> bool:
    """Run hello_mrt2 once and move its out.wav to `dest`. Returns success.

    Paths stay untyped and are only stringified below, so --dry-run works with none of
    them supplied — the point of a dry run is to exercise the harness without MRT2.
    """
    if dry_run:
        dest.write_text(f"DRY RUN\nprompt: {prompt}\n")
        return True

    cmd = [str(hello), str(model), str(resources), str(frames), "--prompt", prompt]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  hello_mrt2 failed: {result.stderr.strip()[:200]}", file=sys.stderr)
        return False

    # hello_mrt2 writes out.wav into the working directory.
    produced = Path("out.wav")
    if not produced.exists():
        print("  hello_mrt2 reported success but wrote no out.wav", file=sys.stderr)
        return False
    shutil.move(str(produced), str(dest))
    return True


def run(args) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    # Build the full clip list first, then shuffle, so ordering carries no information.
    clips = []
    for label, phrase in INSTRUMENTS:
        for trial in range(TRIALS_PER_INSTRUMENT):
            clips.append({"instrument": label,
                          "condition": "removed",
                          "prompt": f"{BASE_PROMPT}, {phrase}"})
        # One control clip per instrument: the same base prompt with nothing removed.
        # Without it there is no way to tell "the prompt worked" from "this generation
        # happened not to use piano anyway", which is a real and easy mistake to make.
        clips.append({"instrument": label,
                      "condition": "control",
                      "prompt": BASE_PROMPT})

    random.shuffle(clips)

    key = []
    for index, clip in enumerate(clips, start=1):
        name = f"clip-{index:02d}.wav"
        print(f"[{index}/{len(clips)}] generating {name} ...", flush=True)
        ok = generate(args.hello, args.model, args.resources,
                      clip["prompt"], out / name, args.frames, args.dry_run)
        if not ok:
            print("Aborting: generation failed. Nothing scored.", file=sys.stderr)
            return 1
        key.append({"clip": name, **clip})

    (out / "ANSWER-KEY-do-not-open.json").write_text(json.dumps(key, indent=1))

    # A scoring sheet with the answers absent.
    with open(out / "scores.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["clip", "drums_present", "piano_present", "bass_present", "notes"])
        for entry in key:
            w.writerow([entry["clip"], "", "", "", ""])

    print(f"\n{len(clips)} clips written to {out}")
    print("\nNow, WITHOUT opening the answer key:")
    print("  1. Listen to each clip.")
    print("  2. In scores.csv mark y/n for whether you hear drums, piano and bass.")
    print(f"  3. Then: {sys.argv[0]} --reveal --out {out}")
    return 0


def reveal(args) -> int:
    out = Path(args.out)
    key_path = out / "ANSWER-KEY-do-not-open.json"
    scores_path = out / "scores.csv"
    if not key_path.exists() or not scores_path.exists():
        print(f"Need both {key_path} and {scores_path}.", file=sys.stderr)
        return 1

    key = {e["clip"]: e for e in json.loads(key_path.read_text())}
    scored = {}
    with open(scores_path) as f:
        for row in csv.DictReader(f):
            scored[row["clip"]] = row

    column = {"DRUMS": "drums_present", "PIANO": "piano_present", "BASS": "bass_present"}
    results = {}

    for clip, entry in key.items():
        row = scored.get(clip)
        if row is None:
            continue
        heard = (row.get(column[entry["instrument"]], "") or "").strip().lower()
        if heard not in ("y", "n"):
            continue        # unscored, ignored rather than guessed at
        bucket = results.setdefault(entry["instrument"],
                                    {"removed": [], "control": []})
        bucket[entry["condition"]].append(heard == "y")

    print(f"\n{'instrument':10} {'asked to remove':>16} {'control':>12}   verdict")
    print("-" * 62)
    for label, _ in INSTRUMENTS:
        b = results.get(label)
        if not b or not b["removed"]:
            print(f"{label:10} {'not scored':>16}")
            continue

        n = len(b["removed"])
        gone = sum(1 for present in b["removed"] if not present)
        pct = 100.0 * gone / n

        ctrl = b["control"]
        ctrl_note = (f"{sum(1 for p in ctrl if p)}/{len(ctrl)} present" if ctrl else "-")

        if pct >= 90:   verdict = "chips OK, label as a request"
        elif pct >= 60: verdict = "chips only if DRUMS is distinguished"
        else:           verdict = "DROP chips - use the text field"
        print(f"{label:10} {f'{gone}/{n} = {pct:.0f}%':>16} {ctrl_note:>12}   {verdict}")

    print("\nA control clip that did NOT contain the instrument means that generation")
    print("would have omitted it anyway - discount those trials before concluding.")
    print("\nRecord the outcome in docs/DESIGN_GAP_ANALYSIS.md §1.")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--model")
    p.add_argument("--resources")
    p.add_argument("--hello", help="path to the hello_mrt2 binary")
    p.add_argument("--out", default="instrument-test",
                   help="output directory (default: instrument-test)")
    p.add_argument("--frames", type=int, default=FRAMES,
                   help=f"MRT2 frames per clip, 40 ms each (default {FRAMES})")
    p.add_argument("--reveal", action="store_true", help="score against the answer key")
    p.add_argument("--dry-run", action="store_true",
                   help="exercise everything except generation; writes stub files")
    p.add_argument("--seed", type=int, help="fix the shuffle, for reproducibility")
    args = p.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    if args.reveal:
        return reveal(args)

    if not args.dry_run and not all([args.model, args.resources, args.hello]):
        p.error("--model, --resources and --hello are required unless --dry-run")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
