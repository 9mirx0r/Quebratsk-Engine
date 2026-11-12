#!/usr/bin/env python3
"""Render a fixed set of maps and compare each frame with a reference picture.

The other checks count things: entities, surfaces, textures found. None of them can see
that a level is drawn inside out, shifted, or with the wrong texture on every wall. This
one looks. For each view below it runs demo/tests/shot_map.tscn (which needs a window),
and then judges the frame twice:

  * on its own: a frame that is one flat colour means nothing was drawn;
  * against its reference: too many pixels changed means the picture is not the one that
    was approved, for better or worse. Open both and decide.

    python tools/visual_check.py --godot "C:/path/to/godot.exe"            # compare
    python tools/visual_check.py --godot ... --update                      # approve as new references
    python tools/visual_check.py --godot ... valve_c1a0 bshift_yard1       # only these views

References live in tests/reference_shots/, which git ignores: they are pictures of game
levels that belong to their publisher, so each machine keeps its own and none is
published. A view with no reference yet is reported as `new`, never as passed; look at
it, then run with --update. Frames of the latest run stay in build/visual_check/ next
to a `<view>.diff.png` that marks in red what changed.

Needs Pillow (`pip install pillow`). Exit codes: 0 every view matches, 1 a view failed,
2 nothing could run (no Godot, no Pillow) or some view has no reference yet.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
REFERENCE_DIR = REPO_ROOT / "tests" / "reference_shots"
OUTPUT_DIR = REPO_ROOT / "build" / "visual_check"

# (view name, map path fragment, extra yaw in degrees, pitch in degrees)
# One view per game where it can be had, each from the map's first player spawn. A map
# whose spawn sits in a sealed black room (bshift/ba_tram1 does) is no use here.
VIEWS = [
    ("valve_c1a0", "valve/maps/c1a0.bsp", 0, 0),
    ("valve_crossfire", "valve/maps/crossfire.bsp", 0, 0),
    ("bshift_yard1", "bshift/maps/ba_yard1.bsp", 0, 0),
    ("bshift_canal1", "bshift/maps/ba_canal1.bsp", 0, 0),
    ("gearbox_of1a1", "gearbox/maps/of1a1.bsp", 0, 0),
    ("cstrike_dust2", "cstrike/maps/de_dust2.bsp", 0, 0),
    ("tfc_2fort", "tfc/maps/2fort.bsp", 0, 0),
    ("dod_anzio", "dod/maps/dod_anzio.bsp", 0, 0),
]

RESOLUTION = "1280x720"
# A pixel counts as changed when any channel moved by more than this (0-255). Leaves room
# for the last-bit differences a driver update brings, not for a different picture.
CHANNEL_TOLERANCE = 24
# Share of changed pixels above which the view fails.
MAX_CHANGED = 0.02


def find_godot(explicit: str | None) -> str | None:
    for candidate in (explicit, os.environ.get("GODOT_BIN")):
        if candidate and Path(candidate).is_file():
            return candidate
    return shutil.which("godot") or shutil.which("godot4")


def render(godot: str, fragment: str, yaw: float, pitch: float, out: Path, timeout: int) -> str | None:
    """Returns None on success, otherwise why there is no frame."""
    out.unlink(missing_ok=True)
    command = [godot, "--path", str(REPO_ROOT / "demo"), "--resolution", RESOLUTION,
               "res://tests/shot_map.tscn", "--", fragment, str(out), str(yaw), str(pitch)]
    try:
        done = subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return f"did not finish within {timeout} s"
    output = (done.stdout + done.stderr).decode("utf-8", errors="replace")
    for line in output.splitlines():
        if "** FAILED" in line:
            return line.strip()
    if done.returncode != 0 or not out.is_file():
        return f"no frame written (exit code {done.returncode})"
    return None


def compare(frame_path: Path, reference_path: Path, diff_path: Path) -> tuple[float, str | None]:
    """Returns (share of changed pixels, problem or None)."""
    from PIL import Image, ImageChops

    frame = Image.open(frame_path).convert("RGB")
    reference = Image.open(reference_path).convert("RGB")
    if frame.size != reference.size:
        return 1.0, f"size {frame.size} differs from the reference's {reference.size}"
    # Per pixel, the largest move of any channel; then a mask of those past the tolerance.
    red, green, blue = ImageChops.difference(frame, reference).split()
    worst = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    mask = worst.point(lambda value: 255 if value > CHANNEL_TOLERANCE else 0)
    changed = mask.histogram()[255] / (frame.width * frame.height)
    if changed > 0:
        marked = frame.copy()
        marked.paste((255, 0, 0), mask=mask)
        marked.save(diff_path)
    else:
        diff_path.unlink(missing_ok=True)
    return changed, None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare rendered map views with their references.")
    parser.add_argument("views", nargs="*", help="View names (default: all).")
    parser.add_argument("--godot", help="Path to the Godot binary.")
    parser.add_argument("--update", action="store_true",
                        help="Store this run's frames as the references.")
    parser.add_argument("--timeout", type=int, default=180, help="Seconds per view (default 180).")
    args = parser.parse_args(argv)

    try:
        from PIL import Image
    except ImportError:
        print("Pillow is not installed: pip install pillow")
        return 2
    godot = find_godot(args.godot)
    if godot is None:
        print("No Godot binary: pass --godot, set GODOT_BIN, or put godot on PATH.")
        return 2

    known = {view[0] for view in VIEWS}
    unknown = [name for name in args.views if name not in known]
    if unknown:
        print(f"No such view: {', '.join(unknown)}. Known: {', '.join(sorted(known))}")
        return 1
    views = [view for view in VIEWS if not args.views or view[0] in args.views]

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    REFERENCE_DIR.mkdir(parents=True, exist_ok=True)
    width = max(len(view[0]) for view in views)
    counts = {"same": 0, "new": 0, "updated": 0, "FAILED": 0}

    for name, fragment, yaw, pitch in views:
        frame_path = OUTPUT_DIR / f"{name}.png"
        reference_path = REFERENCE_DIR / f"{name}.png"
        problem = render(godot, fragment, yaw, pitch, frame_path, args.timeout)
        detail = ""
        if problem is None:
            colours = Image.open(frame_path).convert("RGB").getcolors(maxcolors=2)
            if colours is not None and len(colours) == 1:
                problem = f"the whole frame is one colour {colours[0][1]}: nothing was drawn"

        if problem is not None:
            verdict = "FAILED"
            detail = problem
        elif args.update:
            shutil.copyfile(frame_path, reference_path)
            verdict = "updated"
        elif not reference_path.is_file():
            verdict = "new"
            detail = f"no reference yet; look at {frame_path} and run with --update"
        else:
            changed, problem = compare(frame_path, reference_path, OUTPUT_DIR / f"{name}.diff.png")
            if problem is not None:
                verdict, detail = "FAILED", problem
            elif changed > MAX_CHANGED:
                verdict = "FAILED"
                detail = (f"{changed:.1%} of the pixels changed (limit {MAX_CHANGED:.0%}); "
                          f"see {OUTPUT_DIR / (name + '.diff.png')}")
            else:
                verdict = "same"
                detail = f"{changed:.2%} of the pixels changed"
        counts[verdict] += 1
        print(f"{name:<{width}}  {verdict:<7}  {detail}", flush=True)

    print("")
    print(f"{counts['same']} match their reference, {counts['updated']} stored as reference, "
          f"{counts['new']} have no reference yet, {counts['FAILED']} failed.")
    if counts["FAILED"]:
        return 1
    return 2 if counts["new"] else 0


if __name__ == "__main__":
    sys.exit(main())
