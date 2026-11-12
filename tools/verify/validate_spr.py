#!/usr/bin/env python3
"""Validate every SPR sprite in the retail GoldSrc corpus.

A sprite is a 40-byte header, a 768-byte palette, and a run of frames:

    header      4  magic    "IDSP"
    (40 bytes)  4  int32    version (2)
                4  int32    type        (0..4, orientation)
                4  int32    tex_format  (0..3, render mode)
                4  float    bounding radius
                4  int32    max width
                4  int32    max height
                4  int32    num_frames
                4  float    beam length
                4  int32    sync type
    palette     2  uint16   count (256 in every file)
              768  rgb      palette
    frames      repeated num_frames times:
                4  int32    frame type: 0 = one frame, 1 = group
               16  int32x4  origin x, origin y, width, height
             w*h  uint8      palette indices
                (a group is: 4-byte count, then count frames stored the same way)

The layout was measured, not remembered. Three things settle it:

    - The palette count (256) sits at offset 40 in all 1,361 files, never at
      36: the header is 40 bytes, with tex_format before the radius.
    - Every frame is preceded by its 4-byte type, so the first frame begins at
      810 (40 + 2 + 768) and the walk lands exactly on the file size in all
      1,361 files. Reading the frame header without that int32 shifts every
      field by four bytes.
    - The header settles two conventions exactly: its width/height equal the
      largest frame's dimensions in 1,361/1,361 files, and its bounding radius
      equals hypot(width, height) / 2 in 1,361/1,361.

Hard errors are defects a reader must reject: wrong version, a palette that is
not 256 entries, an unknown frame type, frame dimensions outside 1..4096
(the engine's own guard), pixel data that runs past the file. Warnings are
facts the engine tolerates or conventions the corpus never breaks: trailing
bytes after the last frame (ignored), header dimensions or radius that
disagree with the frames, unknown render modes, non-zero beam length.

There is no RLE in this corpus: every frame stores width*height raw palette
indices, and no sprite uses the group frame type (type 1).

Exit code is 1 when any hard error exists.

Usage:
    python tools/verify/validate_spr.py
    python tools/verify/validate_spr.py --json data/validation/spr.json
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from collections import Counter
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from corpus_scan import discover_games, resolve_corpus_root, write_json  # noqa: E402

HEADER_SIZE = 40
PALETTE_COUNT_AT = 40
PALETTE_BYTES = 768
FRAMES_AT = PALETTE_COUNT_AT + 2 + PALETTE_BYTES
FRAME_SIZE = 16
MAX_DIM = 4096             # the parser refuses anything larger
MAX_GROUP_MEMBERS = 64     # no group occurs in the corpus; keep the bound sane

SPRITE_TYPES = {
    0: "parallel upright",
    1: "facing upright",
    2: "parallel",
    3: "oriented",
    4: "parallel oriented",
}
RENDER_MODES = {
    0: "normal",
    1: "additive",
    2: "indexalpha",
    3: "alphatest",
}
SYNC_TYPES = {0: "sync", 1: "random"}


class SPRError(Exception):
    """Not a readable sprite at all: the header is missing or not IDSP."""


def _text_frames(frame_types: Counter[int]) -> str:
    return ", ".join(f"type {kind} x{count:,}" for kind, count in sorted(frame_types.items())) \
        or "none"


def validate_sprite(raw: bytes, relative: str) -> tuple[dict, list[str], list[str]]:
    hard: list[str] = []
    warn: list[str] = []
    counts: Counter[str] = Counter()

    version, sprite_type, tex_format, radius, width, height, num_frames, beam, sync_type = \
        struct.unpack_from("<iiifiiifi", raw, 4)

    if version != 2:
        hard.append(f"version {version}, expected 2")
    if num_frames < 0:
        hard.append(f"negative frame count {num_frames}")

    palette_count, = struct.unpack_from("<H", raw, PALETTE_COUNT_AT)
    if palette_count != 256:
        hard.append(f"palette count {palette_count}, expected 256")
    if FRAMES_AT > len(raw):
        hard.append(f"file is {len(raw):,} bytes; a palette ends at {FRAMES_AT:,}")
        return _record(relative, raw, counts), hard, warn

    pos = FRAMES_AT
    max_w = max_h = 0
    frame_types: Counter[int] = Counter()
    walked = 0
    overrun = False
    for frame in range(num_frames):
        if pos + 4 > len(raw):
            hard.append(f"frame {frame}: type field runs past the end of the file")
            overrun = True
            break
        frame_type, = struct.unpack_from("<i", raw, pos)
        pos += 4
        frame_types[frame_type] += 1
        if frame_type == 0:
            members = 1
        elif frame_type == 1:
            if pos + 4 > len(raw):
                hard.append(f"frame {frame}: group count runs past the end of the file")
                overrun = True
                break
            members, = struct.unpack_from("<i", raw, pos)
            pos += 4
            if members <= 0 or members > MAX_GROUP_MEMBERS:
                hard.append(f"frame {frame}: group member count {members} outside "
                            f"[1, {MAX_GROUP_MEMBERS}]")
                overrun = True
                break
        else:
            hard.append(f"frame {frame}: frame type {frame_type}, expected 0 or 1")
            overrun = True
            break

        for member in range(members):
            if pos + FRAME_SIZE > len(raw):
                hard.append(f"frame {frame}: frame header runs past the end of the file")
                overrun = True
                break
            origin_x, origin_y, w, h = struct.unpack_from("<4i", raw, pos)
            pos += FRAME_SIZE
            if not 1 <= w <= MAX_DIM or not 1 <= h <= MAX_DIM:
                hard.append(f"frame {frame}: dimensions {w}x{h} outside [1, {MAX_DIM}]")
                overrun = True
                break
            pixels = w * h
            if pos + pixels > len(raw):
                hard.append(f"frame {frame}: pixels [{pos}, {pos + pixels}) run past "
                            f"the end of the file ({len(raw):,} bytes)")
                overrun = True
                break
            pos += pixels
            counts["members"] += 1
            counts["pixels"] += pixels
            max_w = max(max_w, w)
            max_h = max(max_h, h)
        if overrun:
            break
        walked += 1

    counts["frames"] = walked
    if not overrun and pos != len(raw):
        if pos < len(raw):
            warn.append(f"{len(raw) - pos:,} trailing bytes after the last frame; "
                        "the engine never reads them")
        else:
            hard.append(f"frames end at {pos:,}, past the file size {len(raw):,}")

    if walked == num_frames and not overrun:
        if (max_w, max_h) != (width, height):
            warn.append(f"header size {width}x{height}, but the frames reach "
                        f"{max_w}x{max_h}")
        expected_radius = math.hypot(width, height) / 2
        if abs(radius - expected_radius) > 1e-3:
            warn.append(f"bounding radius {radius:.3f}, expected the half diagonal "
                        f"{expected_radius:.3f}")

    if sprite_type not in SPRITE_TYPES:
        warn.append(f"sprite type {sprite_type} outside 0..4")
    if tex_format not in RENDER_MODES:
        warn.append(f"render mode {tex_format} outside 0..3")
    if sync_type not in SYNC_TYPES:
        warn.append(f"sync type {sync_type} outside 0..1")
    if beam != 0:
        warn.append(f"beam length {beam:.3f}; always zero in the corpus")

    record = _record(relative, raw, counts)
    record.update(type=sprite_type, tex_format=tex_format, sync_type=sync_type,
                  width=width, height=height, declared_frames=num_frames,
                  frame_types=dict(frame_types))
    return record, hard, warn


def _record(relative: str, raw: bytes, counts: Counter[str]) -> dict:
    return {
        "path": relative,
        "size": len(raw),
        "counts": {key: value for key, value in counts.items() if value},
    }


def validate(root: Path, games) -> dict:
    files: list[dict] = []
    failed: list[dict] = []
    warnings_count = 0
    merged: dict[str, Counter] = {}

    for game in games:
        game_root = root / game.folder
        for path in sorted(game_root.rglob("*.spr"), key=lambda item: str(item).lower()):
            relative = f"{game.folder}/{path.relative_to(game_root).as_posix()}"
            try:
                raw = path.read_bytes()
                if len(raw) < HEADER_SIZE:
                    raise SPRError(f"file is {len(raw)} bytes, a header needs {HEADER_SIZE}")
                if raw[0:4] != b"IDSP":
                    raise SPRError(f"magic {raw[0:4]!r}; not a sprite")
                record, hard, warn = validate_sprite(raw, relative)
                record["hard_errors"] = hard
                record["warnings"] = warn
                warnings_count += len(warn)
                files.append(record)

                merged.setdefault("types", Counter())[record["type"]] += 1
                merged.setdefault("tex_format", Counter())[record["tex_format"]] += 1
                merged.setdefault("sync_type", Counter())[record["sync_type"]] += 1
                merged.setdefault("declared_frames", Counter())[record["declared_frames"]] += 1
                merged.setdefault("dims", Counter())[(record["width"], record["height"])] += 1
                merged.setdefault("frame_types", Counter()).update(record["frame_types"])
            except (SPRError, OSError, struct.error) as exc:
                failed.append({"path": relative, "error": str(exc)})

    hard = sum(len(record["hard_errors"]) for record in files) + len(failed)

    totals: Counter[str] = Counter()
    for record in files:
        totals.update(record["counts"])

    return {
        "format": "spr",
        "validator": "tools/verify/validate_spr.py",
        "corpus_root_name": root.name,
        "games": sorted({game.folder for game in games}, key=str.lower),
        "summary": {
            "files": len(files) + len(failed),
            "validated": len(files),
            "hard_errors": hard,
            "warnings": warnings_count,
            "bytes": sum(record["size"] for record in files),
            "records": dict(totals),
        },
        "distributions": {
            key: {
                str(value) if not isinstance(value, tuple) else f"{value[0]}x{value[1]}": count
                for value, count in counter.most_common(16)
            }
            for key, counter in sorted(merged.items())
        },
        "failed": failed,
        "files": files,
    }


def render_report(result: dict, limit: int) -> str:
    summary = result["summary"]
    records = summary["records"]
    lines = [
        f"SPR corpus validation: {summary['validated']:,} of {summary['files']:,} files",
        f"  hard errors: {summary['hard_errors']:,}",
        f"  warnings:    {summary['warnings']:,}",
        f"  bytes:       {summary['bytes']:,}",
        "",
        "Record totals: " + ", ".join(
            f"{name} {count:,}" for name, count in sorted(records.items(), key=lambda kv: -kv[1])
        ),
        "Types: " + ", ".join(
            f"{value} x{count:,}" for value, count in
            sorted(result["distributions"].get("types", {}).items(), key=lambda kv: -kv[1])),
        "Render modes: " + ", ".join(
            f"{value} x{count:,}" for value, count in
            sorted(result["distributions"].get("tex_format", {}).items(), key=lambda kv: -kv[1])),
    ]

    if result["failed"]:
        lines += ["", f"Unreadable files ({len(result['failed']):,}):"]
        for record in result["failed"][:limit]:
            lines.append(f"  {record['path']} - {record['error']}")

    hard_records = [record for record in result["files"] if record["hard_errors"]]
    if hard_records:
        lines += ["", f"Files with hard errors ({len(hard_records):,}):"]
        for record in hard_records[:limit]:
            lines.append(f"  {record['path']}: {len(record['hard_errors'])}")
            for error in record["hard_errors"][:4]:
                lines.append(f"    - {error}")

    warning_records = [record for record in result["files"] if record["warnings"]]
    if warning_records:
        lines += ["", f"Files with warnings ({len(warning_records):,}):"]
        for record in warning_records[:limit]:
            lines.append(f"  {record['path']}: {len(record['warnings'])}")

    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate SPR sprites in the GoldSrc corpus.")
    parser.add_argument("--root", help="Half-Life install root (default: auto-detect).")
    parser.add_argument("--json", help="Write the full result as JSON to this path.")
    parser.add_argument("--limit", type=int, default=12, help="Examples per section.")
    args = parser.parse_args(argv)

    root = resolve_corpus_root(args.root)
    games = discover_games(root)
    result = validate(root, games)
    print(render_report(result, args.limit))

    if args.json:
        write_json(Path(args.json), result)

    return 1 if result["summary"]["hard_errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
