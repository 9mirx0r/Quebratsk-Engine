#!/usr/bin/env python3
"""Validate every WAD3 container in the retail GoldSrc corpus.

WAD3 is first in Stage 1 because every other format needs a texture: a BSP face
names a miptex, a model skin names one, a decal is one. If the container is
misunderstood, every texture bug downstream looks like a different bug.

The layouts below are measured on the corpus, not remembered. Three of them are
not what a Quake-era reader would assume:

    header     12 bytes   magic "WAD3", lump count, directory offset. No version
                          field: the magic is the version.
    directory  32 bytes per entry
    lump       file offset, disk size, uncompressed size, type, compression,
               padding, 16-byte name

    miptex     name[16], width, height, offsets[4] (relative to the lump), then
               mip0..mip3, a uint16 palette count (256) and 768 RGB bytes.
               `disk_size` is often larger than that: the surplus is stale data
               from the next lump, not texture data.

    qpic       width, height, width*height palette indices, 768 RGB bytes, and
               four trailing bytes of unknown purpose. The palette is *after*
               the pixels, not before them.

    font       0x46 lumps exist (15 of them, all in gfx.wad) but are not decoded
               in Stage 1. Only their bounds are checked.

Hard errors are defects a reader must reject. Warnings are facts worth knowing
that the engine tolerates. The exit code is 1 when any hard error exists, so the
corpus gate can call this directly.

Usage:
    python tools/verify/validate_wad3.py
    python tools/verify/validate_wad3.py --limit 20
    python tools/verify/validate_wad3.py --json data/validation/wad3.json
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from collections import Counter
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from corpus_scan import discover_games, resolve_corpus_root, sha256_file  # noqa: E402

HEADER = struct.Struct("<4sii")
LUMP = struct.Struct("<iiiBBH16s")
MIPTEX = struct.Struct("<16sII4I")
QPIC = struct.Struct("<ii")

HEADER_SIZE = HEADER.size          # 12
LUMP_SIZE = LUMP.size              # 32
MIPTEX_SIZE = MIPTEX.size          # 40
PALETTE_SIZE = 768
PALETTE_ENTRIES = 256
QPIC_TRAILER = 4

TYPE_MIPTEX = 0x43
TYPE_QPIC = 0x42
TYPE_FONT = 0x46
TYPE_NAMES = {TYPE_MIPTEX: "miptex", TYPE_QPIC: "qpic", TYPE_FONT: "font"}

# The software renderer only guarantees textures up to 512, but the container
# allows more. Large dimensions are reported, never rejected.
LARGE_TEXTURE = 1024


class WAD3Error(Exception):
    """A hard defect: the file cannot be read as a WAD3."""


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _text(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("latin-1")


def _name_problem(name: str) -> str | None:
    if not name:
        return "empty name"
    if "/" in name or "\\" in name:
        return "path separator in name"
    if any(ord(character) < 0x20 or ord(character) > 0x7E for character in name):
        return "non-printable character in name"
    return None


def check_miptex(
    blob: bytes, name: str, next_name: str | None
) -> tuple[dict, list[str], list[str]]:
    """Validate one miptex lump. Returns (facts, hard errors, warnings)."""
    hard: list[str] = []
    warn: list[str] = []
    facts: dict = {}

    if len(blob) < MIPTEX_SIZE:
        return facts, [f"miptex {name!r}: lump is {len(blob)} bytes, header needs 40"], warn

    header_name_raw, width, height, *offsets = MIPTEX.unpack_from(blob, 0)
    header_name = _text(header_name_raw)
    facts.update({"width": width, "height": height})

    if header_name.lower() != name.lower():
        warn.append(f"miptex {name!r}: directory name differs from header name {header_name!r}")

    if width == 0 or height == 0:
        hard.append(f"miptex {name!r}: zero dimension {width}x{height}")
        return facts, hard, warn
    if width % 16 or height % 16:
        hard.append(f"miptex {name!r}: dimension {width}x{height} is not a multiple of 16")
    if width > LARGE_TEXTURE or height > LARGE_TEXTURE:
        warn.append(f"miptex {name!r}: large texture {width}x{height}")

    pixel_count = width * height
    expected = [pixel_count, pixel_count // 4, pixel_count // 16, pixel_count // 64]
    if offsets[0] < MIPTEX_SIZE:
        hard.append(f"miptex {name!r}: mip0 offset {offsets[0]} overlaps the header")
        return facts, hard, warn
    if any(later <= earlier for earlier, later in zip(offsets, offsets[1:])):
        hard.append(f"miptex {name!r}: mip offsets are not increasing {offsets}")
        return facts, hard, warn

    for level in range(3):
        actual = offsets[level + 1] - offsets[level]
        if actual != expected[level]:
            hard.append(
                f"miptex {name!r}: mip{level} is {actual} bytes, {width}x{height} needs "
                f"{expected[level]}"
            )

    palette_at = offsets[3] + expected[3]
    if palette_at + 2 + PALETTE_SIZE > len(blob):
        hard.append(
            f"miptex {name!r}: palette at {palette_at} does not fit in {len(blob)} bytes"
        )
        return facts, hard, warn
    entries = _u16(blob, palette_at)
    if entries != PALETTE_ENTRIES:
        hard.append(f"miptex {name!r}: palette declares {entries} entries, expected 256")

    declared_end = palette_at + 2 + entries * 3
    if declared_end > len(blob):
        hard.append(f"miptex {name!r}: palette data ends at {declared_end}, lump is {len(blob)}")
    else:
        surplus = len(blob) - declared_end
        # Up to three bytes are ordinary 4-byte alignment padding. More than
        # that is a builder artefact: measured on de_vertigo.wad, the surplus is
        # a stale copy of the next lump's header inside this lump's declared
        # size. The engine never reads past the palette, and neither should a
        # reader, so this is a warning rather than an error.
        if surplus <= 3:
            facts["padded"] = surplus > 0
        else:
            stale = bool(next_name) and next_name.encode("latin-1") in blob[declared_end:]
            warn.append(
                f"miptex {name!r}: {surplus} bytes past the palette"
                + (f" (stale copy of next lump {next_name!r})" if stale else "")
            )
            facts["trailing_is_next_lump"] = stale

    facts.update(
        {
            "masked": name.startswith("{"),
            "palette_entries": entries,
            "declared_end": declared_end,
            "lump_size": len(blob),
        }
    )
    return facts, hard, warn


def check_qpic(blob: bytes, name: str) -> tuple[dict, list[str], list[str]]:
    """Validate one qpic lump: dimensions, pixels, palette, 4-byte trailer."""
    hard: list[str] = []
    warn: list[str] = []
    facts: dict = {}
    if len(blob) < QPIC.size:
        return facts, [f"qpic {name!r}: lump is {len(blob)} bytes, header needs 8"], warn
    width, height = QPIC.unpack_from(blob, 0)
    facts.update({"width": width, "height": height})
    if width <= 0 or height <= 0 or width > 4096 or height > 4096:
        hard.append(f"qpic {name!r}: implausible dimensions {width}x{height}")
        return facts, hard, warn

    pixels_end = QPIC.size + width * height
    expected = pixels_end + PALETTE_SIZE
    if len(blob) < expected:
        hard.append(
            f"qpic {name!r}: {width}x{height} plus palette needs {expected} bytes, "
            f"lump has {len(blob)}"
        )
        return facts, hard, warn
    extra = len(blob) - expected
    if extra not in (0, QPIC_TRAILER):
        warn.append(f"qpic {name!r}: {extra} bytes after the palette, expected 0 or 4")
    facts["trailer"] = extra
    return facts, hard, warn


def check_font(blob: bytes, name: str) -> tuple[dict, list[str], list[str]]:
    """Bounds only. The font lump structure is an open question in Stage 1."""
    if len(blob) < 16:
        return {}, [f"font {name!r}: lump is {len(blob)} bytes, header needs 16"], []
    first_int = struct.unpack_from("<i", blob, 0)[0]
    return {"first_int": first_int}, [], []


def validate_wad(path: Path, relative: str) -> dict:
    size = path.stat().st_size
    with path.open("rb") as handle:
        prologue = handle.read(HEADER_SIZE)
        if len(prologue) < HEADER_SIZE:
            raise WAD3Error(f"truncated header ({len(prologue)} bytes)")
        magic, num_lumps, dir_offset = HEADER.unpack(prologue)
        if magic not in (b"WAD3", b"WAD2"):
            raise WAD3Error(f"bad magic {magic!r}")
        if num_lumps < 0 or num_lumps > 1_000_000:
            raise WAD3Error(f"implausible lump count {num_lumps}")
        if dir_offset < HEADER_SIZE or dir_offset + num_lumps * LUMP_SIZE > size:
            raise WAD3Error(f"directory at {dir_offset} ({num_lumps} entries) is out of bounds")

        handle.seek(dir_offset)
        directory = handle.read(num_lumps * LUMP_SIZE)
        if len(directory) != num_lumps * LUMP_SIZE:
            raise WAD3Error("truncated directory")

        entries = [LUMP.unpack_from(directory, index * LUMP_SIZE) for index in range(num_lumps)]
        names = [_text(entry[6]) for entry in entries]

        hard: list[str] = []
        warn: list[str] = []
        types: Counter[int] = Counter()
        name_counts: Counter[str] = Counter()
        dimensions: Counter[tuple[int, int]] = Counter()
        miptex_count = 0
        qpic_count = 0
        font_count = 0
        masked = 0
        animated = 0
        stale_tails = 0

        for index, (file_pos, disk_size, uncompressed_size, lump_type, compression,
                    _padding, name_raw) in enumerate(entries):
            name = names[index]
            types[lump_type] += 1
            next_name = names[index + 1] if index + 1 < len(entries) else None

            if compression != 0:
                hard.append(f"lump {index} {name!r}: compression {compression}, WAD3 has none")
            if disk_size < 0 or file_pos < HEADER_SIZE:
                hard.append(f"lump {index} {name!r}: negative size or offset inside header")
                continue
            if file_pos + disk_size > size:
                hard.append(
                    f"lump {index} {name!r}: {file_pos}+{disk_size} exceeds file size {size}"
                )
                continue
            if disk_size != uncompressed_size:
                warn.append(
                    f"lump {index} {name!r}: disk size {disk_size} != uncompressed "
                    f"{uncompressed_size}"
                )
            problem = _name_problem(name)
            if problem:
                hard.append(f"lump {index}: {problem} ({name_raw!r})")
            else:
                name_counts[name.lower()] += 1

            if lump_type == TYPE_MIPTEX:
                miptex_count += 1
                handle.seek(file_pos)
                blob = handle.read(disk_size)
                facts, lump_hard, lump_warn = check_miptex(blob, name, next_name)
                hard.extend(lump_hard)
                warn.extend(lump_warn)
                if facts.get("width"):
                    dimensions[(facts["width"], facts["height"])] += 1
                if facts.get("masked"):
                    masked += 1
                if name[:1] == "+":
                    animated += 1
                if facts.get("trailing_is_next_lump"):
                    stale_tails += 1
            elif lump_type == TYPE_QPIC:
                qpic_count += 1
                handle.seek(file_pos)
                blob = handle.read(disk_size)
                _facts, lump_hard, lump_warn = check_qpic(blob, name)
                hard.extend(lump_hard)
                warn.extend(lump_warn)
            elif lump_type == TYPE_FONT:
                font_count += 1
                handle.seek(file_pos)
                blob = handle.read(disk_size)
                _facts, lump_hard, lump_warn = check_font(blob, name)
                hard.extend(lump_hard)
                warn.extend(lump_warn)
            elif lump_type not in TYPE_NAMES:
                warn.append(f"lump {index} {name!r}: unknown type 0x{lump_type:02x}")

        duplicates = sorted(name for name, count in name_counts.items() if count > 1)
        if duplicates:
            warn.append(
                f"{len(duplicates)} duplicate texture name(s): {', '.join(duplicates[:5])}"
            )

    return {
        "path": relative,
        "size": size,
        "sha256": sha256_file(path),
        "magic": magic.decode("ascii"),
        "num_lumps": num_lumps,
        "dir_offset": dir_offset,
        "lump_types": {f"0x{kind:02x}": count for kind, count in sorted(types.items())},
        "miptex": miptex_count,
        "qpic": qpic_count,
        "font": font_count,
        "masked": masked,
        "animated": animated,
        "stale_tails": stale_tails,
        "dimensions": {f"{w}x{h}": count for (w, h), count in sorted(dimensions.items())},
        "textures": sorted(name_counts),
        "name_sha256": hashlib.sha256(
            "\n".join(sorted(name_counts)).encode("ascii", "replace")
        ).hexdigest(),
        "hard_errors": hard,
        "warnings": warn,
    }


def validate(root: Path, games) -> dict:
    files: list[dict] = []
    failed: list[dict] = []

    for game in games:
        game_root = root / game.folder
        for path in sorted(game_root.rglob("*.wad"), key=lambda item: str(item).lower()):
            relative = f"{game.folder}/{path.relative_to(game_root).as_posix()}"
            try:
                files.append(validate_wad(path, relative))
            except (WAD3Error, OSError) as exc:
                failed.append({"path": relative, "error": str(exc)})

    hard = sum(len(record["hard_errors"]) for record in files) + len(failed)
    warnings = sum(len(record["warnings"]) for record in files)

    type_totals: Counter[str] = Counter()
    dimension_totals: Counter[str] = Counter()
    magic_totals: Counter[str] = Counter()
    for record in files:
        magic_totals[record["magic"]] += 1
        type_totals.update(record["lump_types"])
        dimension_totals.update(record["dimensions"])

    hashes = Counter(record["name_sha256"] for record in files)
    file_hashes = Counter(record["sha256"] for record in files)
    all_names: set[str] = set()
    for record in files:
        all_names.update(record["textures"])

    return {
        "format": "wad3",
        "validator": "tools/verify/validate_wad3.py",
        "corpus_root_name": root.name,
        "games": sorted({game.folder for game in games}, key=str.lower),
        "summary": {
            "files": len(files) + len(failed),
            "validated": len(files),
            "hard_errors": hard,
            "warnings": warnings,
            "miptex": sum(record["miptex"] for record in files),
            "qpic": sum(record["qpic"] for record in files),
            "font": sum(record["font"] for record in files),
            "masked": sum(record["masked"] for record in files),
            "animated": sum(record["animated"] for record in files),
            "stale_tails": sum(record["stale_tails"] for record in files),
            "unique_names": len(all_names),
            "distinct_name_sets": len(hashes),
            "distinct_files": len(file_hashes),
            "bytes": sum(record["size"] for record in files),
        },
        "magic": dict(sorted(magic_totals.items())),
        "lump_types": dict(sorted(type_totals.items())),
        "dimensions": dict(
            sorted(dimension_totals.items(), key=lambda item: (-item[1], item[0]))
        ),
        "failed": failed,
        "files": files,
    }


def render_report(result: dict, limit: int) -> str:
    summary = result["summary"]
    lines = [
        f"WAD3 corpus validation: {summary['validated']:,} of {summary['files']:,} files",
        f"  hard errors: {summary['hard_errors']:,}",
        f"  warnings:    {summary['warnings']:,}",
        f"  miptex:      {summary['miptex']:,}   qpic: {summary['qpic']:,}   font: {summary['font']:,}",
        f"  masked:      {summary['masked']:,}   animated: {summary['animated']:,}",
        f"  stale tails: {summary['stale_tails']:,}   unique names: {summary['unique_names']:,}",
        f"  distinct texture-name sets: {summary['distinct_name_sets']:,}   "
        f"distinct files: {summary['distinct_files']:,}   bytes: {summary['bytes']:,}",
        "",
        "Container magic: " + ", ".join(f"{k} {v:,}" for k, v in result["magic"].items()),
        "Lump types:      " + ", ".join(f"{k} {v:,}" for k, v in result["lump_types"].items()),
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

    top_dimensions = list(result["dimensions"].items())[:12]
    if top_dimensions:
        lines += ["", "Most common miptex dimensions:"]
        for dimension, count in top_dimensions:
            lines.append(f"  {dimension:<12} {count:>6,}")

    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate WAD3 files in the GoldSrc corpus.")
    parser.add_argument("--root", help="Half-Life install root (default: auto-detect).")
    parser.add_argument("--json", help="Write the full result as JSON to this path.")
    parser.add_argument("--limit", type=int, default=12, help="Examples per section.")
    args = parser.parse_args(argv)

    root = resolve_corpus_root(args.root)
    games = discover_games(root)
    result = validate(root, games)
    print(render_report(result, args.limit))

    if args.json:
        from corpus_scan import write_json

        write_json(Path(args.json), result)

    return 1 if result["summary"]["hard_errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
