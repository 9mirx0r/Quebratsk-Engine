#!/usr/bin/env python3
"""Stage 0 corpus inventory for the GoldSrc encyclopedia.

Every later claim about a GoldSrc file is measured against the retail corpus, so the corpus
has to be described once, reproducibly, before anything else is written. This walks every
installed game folder, hashes the formats the encyclopedia cares about, reads the small
header fields that identify a format version, and writes two generated artefacts per game:

    data/corpus/<game>.json            machine-readable inventory
    docs/encyclopedia/games/<game>.md  human-readable summary

The two Stage 0 gate questions are answered without a second scan:

    python tools/corpus_scan.py where models/player/arctic.mdl
    python tools/corpus_scan.py diff cstrike cstrike_hd

No game binary is opened. No asset is modified. The corpus is read-only.

Why the paths are resolved instead of hardcoded
-----------------------------------------------
The machine this was written on keeps Steam at ``C:\\Steam}`` -- a literal closing brace in the
folder name. The C++ ``SteamLibraryDetector`` already reads the registry and
``libraryfolders.vdf``, and this script follows the same order: explicit ``--root``, then
``QUEBRATSK_GAME_ROOT``, then the Steam registry, then the conventional fallbacks. The
fallback is a last resort, never the primary answer.

Why the output is deterministic
-------------------------------
The plan requires generated data that a test can regenerate and compare. No timestamps, no
absolute paths, sorted records and sorted keys mean two runs on two machines produce the same
bytes for the same corpus.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

SCHEMA_VERSION = 1
TOOL_VERSION = "1.0.0"

REPO_ROOT = Path(__file__).resolve().parent.parent
CORPUS_DIR = REPO_ROOT / "data" / "corpus"
DELTA_DIR = REPO_ROOT / "data" / "deltas"
GAMES_DOC_DIR = REPO_ROOT / "docs" / "encyclopedia" / "games"

# The Stage 0 extension list. Anything else in a game folder is outside this inventory.
EXTENSIONS = {
    ".bsp": "bsp",
    ".wad": "wad",
    ".mdl": "mdl",
    ".spr": "spr",
    ".wav": "wav",
    ".tga": "tga",
    ".pak": "pak",
    ".res": "res",
    ".cfg": "cfg",
    ".txt": "txt",
    ".lst": "lst",
}

# Formats that carry a version field in their header. TGA and WAV have identifiers and
# dimensions, but no version number; text formats have none at all.
VERSIONED_FORMATS = {"bsp": 30, "wad": 3, "mdl": 10, "spr": 2}

TEXT_EXTENSIONS = {".res", ".cfg", ".txt", ".lst"}

# Folder -> (display name, Steam app id). The ten retail GoldSrc games.
KNOWN_GAMES = {
    "valve": ("Half-Life", 70),
    "gearbox": ("Opposing Force", 50),
    "bshift": ("Blue Shift", 130),
    "cstrike": ("Counter-Strike 1.6", 10),
    "czero": ("Condition Zero", 80),
    "czeror": ("Condition Zero: Deleted Scenes", 100),
    "dod": ("Day of Defeat", 30),
    "dmc": ("Deathmatch Classic", 40),
    "tfc": ("Team Fortress Classic", 20),
    "ricochet": ("Ricochet", 60),
}

VARIANT_LABELS = {"_hd": "HD pack", "_spanish": "Spanish"}

BSP_HEADER_SIZE = 4 + 15 * 8
MDL_HEADER_SIZE = 244
SPR_HEADER_SIZE = 40
WAD_HEADER_SIZE = 12


class ParseError(Exception):
    """A file's header does not match the format its extension claims."""


@dataclass(frozen=True)
class Game:
    folder: str
    display_name: str
    app_id: int | None
    variant_of: str | None


# --------------------------------------------------------------------------------------
# Root resolution
# --------------------------------------------------------------------------------------


def _registry_steam_path() -> Path | None:
    """Read HKCU\\Software\\Valve\\Steam\\SteamPath, the value Steam itself maintains."""
    if sys.platform != "win32":
        return None
    try:
        import winreg
    except ImportError:
        return None
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            value, _ = winreg.QueryValueEx(key, "SteamPath")
    except OSError:
        return None
    return Path(value)


def _steam_libraries() -> list[Path]:
    """Steam roots, including secondary libraries listed in libraryfolders.vdf."""
    roots: list[Path] = []
    steam = _registry_steam_path()
    if steam is None:
        return roots
    roots.append(steam)
    vdf = steam / "steamapps" / "libraryfolders.vdf"
    try:
        text = vdf.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return roots
    for match in re.finditer(r'"path"\s+"([^"]+)"', text):
        # VDF escapes Windows backslashes; the captured value has them doubled.
        roots.append(Path(match.group(1).replace("\\\\", "\\")))
    return roots


def _looks_like_hl_root(path: Path) -> bool:
    if not path.is_dir():
        return False
    return ((path / "valve" / "liblist.gam").is_file()
            or (path / "cstrike" / "liblist.gam").is_file())


def resolve_corpus_root(explicit: str | None) -> Path:
    """Find the Half-Life install root: the folder that contains valve/, cstrike/, ..."""
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    env = os.environ.get("QUEBRATSK_GAME_ROOT")
    if env:
        candidates.append(Path(env))
    for library in _steam_libraries():
        candidates.append(library / "steamapps" / "common" / "Half-Life")
        candidates.append(library / "steamapps" / "common")
    candidates.extend(
        [
            Path("C:/Program Files (x86)/Steam/steamapps/common/Half-Life"),
            Path("C:/GOG Games/Half-Life"),
            Path("C:/Games/Half-Life"),
        ]
    )

    seen: set[str] = set()
    for candidate in candidates:
        for probe in (candidate, candidate / "Half-Life"):
            try:
                resolved = probe.resolve()
            except OSError:
                continue
            key = str(resolved).lower()
            if key in seen:
                continue
            seen.add(key)
            if _looks_like_hl_root(resolved):
                return resolved

    raise SystemExit(
        "Could not find a Half-Life installation. Pass --root or set QUEBRATSK_GAME_ROOT."
    )


def discover_games(root: Path) -> list[Game]:
    """Every game folder under root, including the _hd and _spanish variants.

    A folder counts when it is one of the ten known games, a known variant, or carries a
    liblist.gam of its own. platform/ is engine support, not a game.
    """
    games: list[Game] = []
    for child in sorted(root.iterdir(), key=lambda path: path.name.lower()):
        if not child.is_dir() or child.name.lower() == "platform":
            continue
        lower = child.name.lower()
        base = lower
        variant: str | None = None
        for suffix in VARIANT_LABELS:
            if lower.endswith(suffix) and lower[: -len(suffix)] in KNOWN_GAMES:
                base = lower[: -len(suffix)]
                variant = suffix
                break

        if base in KNOWN_GAMES:
            display_name, app_id = KNOWN_GAMES[base]
            if variant:
                display_name = f"{display_name} ({VARIANT_LABELS[variant]})"
            games.append(Game(child.name, display_name, app_id, base if variant else None))
        elif (child / "liblist.gam").is_file():
            games.append(Game(child.name, child.name, None, None))
    return games


# --------------------------------------------------------------------------------------
# Small readers
# --------------------------------------------------------------------------------------


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _bytes(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or offset + size > len(data):
        raise ParseError(f"need {size} bytes at offset {offset}, have {len(data)}")
    return data[offset : offset + size]


def inspect_bsp(path: Path, size: int) -> tuple[int | None, dict]:
    with path.open("rb") as handle:
        header = handle.read(BSP_HEADER_SIZE)
        if len(header) < BSP_HEADER_SIZE:
            raise ParseError(f"truncated BSP header ({len(header)} bytes)")
        version, = struct.unpack_from("<i", header, 0)
        lumps = [struct.unpack_from("<ii", header, 4 + index * 8) for index in range(15)]
        detail: dict = {"nonempty_lumps": sum(1 for _, length in lumps if length > 0)}

        if version != 30:
            # Source BSPs have 64 lumps and a different entity layout. Record the version,
            # do not pretend the GoldSrc lump table applies.
            detail["has_worldspawn"] = None
            return version, detail

        # Most GoldSrc maps put entities in lump 0. Blue Shift's 37 maps do not: they keep
        # the Quake-era order (vertices first, entities second), so reading lump 0 as text
        # reported every Blue Shift map as having no worldspawn. Find the entity lump by its
        # leading "{" among the first four lumps and record which layout was used.
        entity_index: int | None = None
        for index in range(min(4, len(lumps))):
            offset, length = lumps[index]
            if offset < 0 or length <= 0 or offset + length > size:
                continue
            handle.seek(offset)
            head = handle.read(min(length, 256))
            if head.lstrip().startswith(b"{"):
                entity_index = index
                break

        if entity_index is None:
            detail["has_worldspawn"] = None
            detail["entity_lump_error"] = "no entity lump in lumps 0..3"
            return version, detail

        entity_offset, entity_length = lumps[entity_index]
        detail["entity_lump_index"] = entity_index
        detail["lump_layout"] = {0: "standard", 1: "bshift"}.get(
            entity_index, f"lump-{entity_index}"
        )
        if entity_length > 64 * 1024 * 1024:
            detail["has_worldspawn"] = None
            detail["entity_lump_error"] = "implausible length"
            return version, detail

        handle.seek(entity_offset)
        raw = handle.read(entity_length)

    text = raw.decode("latin-1", "replace")
    detail["has_worldspawn"] = re.search(r'"classname"\s+"worldspawn"', text) is not None
    wad_refs: list[str] = []
    for match in re.finditer(r'"wad"\s+"([^"]*)"', text):
        wad_refs.extend(part.strip() for part in match.group(1).split(";") if part.strip())
    detail["wad_refs"] = wad_refs
    return version, detail


def inspect_wad(path: Path, size: int) -> tuple[int | None, dict]:
    with path.open("rb") as handle:
        header = handle.read(WAD_HEADER_SIZE)
        if len(header) < WAD_HEADER_SIZE:
            raise ParseError(f"truncated WAD header ({len(header)} bytes)")
        magic = header[:4]
        if magic not in (b"WAD3", b"WAD2"):
            raise ParseError(f"bad WAD magic {magic!r}")
        # WAD2 and WAD3 share one header: magic, lump count, directory offset. There is no
        # version field to read -- the magic is the version, which is why reading a version
        # at offset 4 made every retail WAD look like a corrupt file on the first pass.
        version = 3 if magic == b"WAD3" else 2
        numlumps, directory_offset = struct.unpack_from("<ii", header, 4)
        if numlumps < 0 or numlumps > 1_000_000:
            raise ParseError(f"implausible lump count {numlumps}")
        if directory_offset < 0 or directory_offset + numlumps * 32 > size:
            raise ParseError("WAD directory out of bounds")

        types: Counter[int] = Counter()
        remaining = numlumps
        handle.seek(directory_offset)
        while remaining:
            batch = min(remaining, 4096)
            data = handle.read(batch * 32)
            if len(data) != batch * 32:
                raise ParseError("truncated WAD directory")
            for index in range(batch):
                types[data[index * 32 + 12]] += 1
            remaining -= batch

    return version, {"numlumps": numlumps,
                     "lump_types": {str(kind): count for kind, count in sorted(types.items())}}


def inspect_mdl(path: Path, size: int) -> tuple[int | None, dict]:
    with path.open("rb") as handle:
        header = handle.read(MDL_HEADER_SIZE)
    if len(header) < 8:
        raise ParseError(f"truncated MDL header ({len(header)} bytes)")

    magic = header[:4]
    if magic not in (b"IDST", b"IDSQ"):
        raise ParseError(f"bad MDL magic {magic!r}")

    version, = struct.unpack_from("<i", header, 4)
    name = header[8:72].split(b"\x00", 1)[0].decode("latin-1", "replace")
    detail = {"header_name": name}

    # A sequence group is a different file with the same extension. Its header is shorter
    # and the counts live elsewhere, so reading the model offsets out of it produces numbers
    # like 16252928 bones. The magic is the only thing that separates them.
    if magic == b"IDSQ":
        detail["header_kind"] = "sequence-group"
        return version, detail

    if len(header) < MDL_HEADER_SIZE:
        raise ParseError(f"truncated MDL header ({len(header)} bytes)")
    if version == 10:
        detail.update(
            {
                "numbones": struct.unpack_from("<i", header, 140)[0],
                "numseq": struct.unpack_from("<i", header, 164)[0],
                "numseqgroups": struct.unpack_from("<i", header, 172)[0],
                "numtextures": struct.unpack_from("<i", header, 180)[0],
                "numskinfamilies": struct.unpack_from("<i", header, 196)[0],
                "numbodyparts": struct.unpack_from("<i", header, 204)[0],
            }
        )
        # The texture companion is an IDST file with no bones, no sequences and no name;
        # it exists only to carry the skins of a model that declares zero textures.
        detail["header_kind"] = (
            "texture-companion"
            if detail["numbones"] == 0 and detail["numseq"] == 0
            and detail["numtextures"] > 0 and not name
            else "model"
        )
    return version, detail


def inspect_spr(path: Path, size: int) -> tuple[int | None, dict]:
    with path.open("rb") as handle:
        header = handle.read(SPR_HEADER_SIZE)
    if len(header) < SPR_HEADER_SIZE:
        raise ParseError(f"truncated SPR header ({len(header)} bytes)")
    if header[:4] != b"IDSP":
        raise ParseError("bad SPR magic")

    (version, sprite_type, tex_format, _radius, width, height, numframes,
     _beamlength, synctype) = struct.unpack_from("<iiifiiifi", header, 4)
    if numframes < 0 or numframes > 1_000_000:
        raise ParseError(f"implausible frame count {numframes}")

    return version, {"type": sprite_type, "tex_format": tex_format, "frames": numframes,
                     "width": width, "height": height, "synctype": synctype}


def inspect_wav(path: Path, size: int) -> tuple[int | None, dict]:
    with path.open("rb") as handle:
        riff = handle.read(12)
        if len(riff) < 12:
            raise ParseError("truncated RIFF header")
        if riff[:4] != b"RIFF" or riff[8:12] != b"WAVE":
            raise ParseError("not a RIFF/WAVE file")

        fmt: dict | None = None
        data_size: int | None = None
        while True:
            chunk_header = handle.read(8)
            if len(chunk_header) < 8:
                break
            chunk_id, chunk_size = struct.unpack("<4sI", chunk_header)
            if chunk_id == b"fmt ":
                if chunk_size < 16 or chunk_size > 4096:
                    raise ParseError(f"implausible fmt chunk size {chunk_size}")
                chunk = handle.read(chunk_size)
                if len(chunk) != chunk_size:
                    raise ParseError("truncated fmt chunk")
                audio_format, channels, sample_rate, byte_rate, _align, bits = (
                    struct.unpack_from("<HHIIHH", chunk, 0)
                )
                if channels == 0 or channels > 8 or sample_rate == 0 or sample_rate > 384_000:
                    raise ParseError(
                        f"implausible WAV format: {channels} channels at {sample_rate} Hz"
                    )
                fmt = {
                    "codec": {1: "pcm", 0x11: "adpcm", 0x55: "mp3", 0x2000: "extensible"}
                    .get(audio_format, f"format-{audio_format}"),
                    "channels": channels,
                    "sample_rate": sample_rate,
                    "bits": bits,
                    "byte_rate": byte_rate,
                }
                if chunk_size & 1:
                    handle.seek(1, os.SEEK_CUR)
            elif chunk_id == b"data":
                data_size = chunk_size
                break
            else:
                handle.seek(chunk_size + (chunk_size & 1), os.SEEK_CUR)

    if fmt is None:
        raise ParseError("no fmt chunk")
    seconds = None
    if data_size is not None and fmt["byte_rate"] > 0:
        seconds = round(data_size / fmt["byte_rate"], 3)
    fmt.pop("byte_rate")
    fmt["seconds"] = seconds
    return None, fmt


def inspect_tga(path: Path, size: int) -> tuple[int | None, dict]:
    with path.open("rb") as handle:
        header = handle.read(18)
    if len(header) < 18:
        raise ParseError(f"truncated TGA header ({len(header)} bytes)")

    colormap_type, image_type = header[1], header[2]
    width, height = struct.unpack_from("<HH", header, 12)
    pixel_depth = header[16]
    if image_type == 0:
        return None, {"empty": True, "width": width, "height": height}
    if image_type not in (1, 2, 3, 9, 10, 11):
        raise ParseError(f"unknown TGA image type {image_type}")
    if width == 0 or height == 0:
        raise ParseError("zero TGA dimensions")
    if pixel_depth not in (8, 15, 16, 24, 32):
        raise ParseError(f"unexpected TGA depth {pixel_depth}")

    return None, {"width": width, "height": height, "bpp": pixel_depth,
                  "image_type": image_type, "colormapped": colormap_type != 0}


def inspect_pak(path: Path, size: int) -> tuple[int | None, dict]:
    with path.open("rb") as handle:
        header = handle.read(12)
    if len(header) < 12:
        raise ParseError(f"truncated PAK header ({len(header)} bytes)")
    if header[:4] != b"PACK":
        raise ParseError("bad PAK magic")
    directory_offset, directory_length = struct.unpack_from("<ii", header, 4)
    if (directory_offset < 0 or directory_length < 0
            or directory_offset + directory_length > size):
        raise ParseError("PAK directory out of bounds")
    if directory_length % 64:
        raise ParseError("PAK directory is not a multiple of 64 bytes")
    return None, {"entries": directory_length // 64}


def inspect_text(path: Path, size: int) -> tuple[int | None, dict]:
    with path.open("rb") as handle:
        raw = handle.read(256 * 1024)
    bom = raw.startswith(b"\xef\xbb\xbf")
    try:
        text = raw.decode("utf-8-sig")
        encoding = "utf-8"
    except UnicodeDecodeError:
        text = raw.decode("cp1252", "replace")
        encoding = "cp1252"
    lines = text.count("\n") + (1 if text and not text.endswith("\n") else 0)
    return None, {"lines": lines, "encoding": encoding, "bom": bom,
                  "truncated": size > len(raw)}


def inspect_file(path: Path, extension: str, size: int) -> tuple[int | None, dict]:
    if extension == ".bsp":
        return inspect_bsp(path, size)
    if extension == ".wad":
        return inspect_wad(path, size)
    if extension == ".mdl":
        return inspect_mdl(path, size)
    if extension == ".spr":
        return inspect_spr(path, size)
    if extension == ".wav":
        return inspect_wav(path, size)
    if extension == ".tga":
        return inspect_tga(path, size)
    if extension == ".pak":
        return inspect_pak(path, size)
    if extension in TEXT_EXTENSIONS:
        return inspect_text(path, size)
    raise ParseError(f"no reader for {extension}")


# --------------------------------------------------------------------------------------
# Scan
# --------------------------------------------------------------------------------------


def scan_game(root: Path, game: Game) -> tuple[list[dict], list[dict], list[dict]]:
    """Return (file records, parse/read errors, models whose sidecar is missing)."""
    game_root = root / game.folder
    files: list[dict] = []
    errors: list[dict] = []
    missing_sidecars: list[dict] = []

    for dirpath, dirnames, filenames in os.walk(game_root):
        dirnames.sort(key=str.lower)
        filenames.sort(key=str.lower)
        directory = Path(dirpath)
        relative_dir = directory.relative_to(game_root)
        filenames_lower = {name.lower() for name in filenames}

        for filename in filenames:
            extension = Path(filename).suffix.lower()
            if extension not in EXTENSIONS:
                continue
            path = directory / filename
            relative = (relative_dir / filename) if relative_dir != Path(".") else Path(filename)
            relative_path = relative.as_posix()

            try:
                size = path.stat().st_size
                digest = sha256_file(path)
            except OSError as exc:
                errors.append({"path": relative_path, "error": f"read: {exc}"})
                continue

            detail: dict = {}
            version: int | None = None
            try:
                version, detail = inspect_file(path, extension, size)
            except ParseError as exc:
                detail = {"parse_error": str(exc)}
                errors.append({"path": relative_path, "error": str(exc)})

            record = {
                "path": relative_path,
                "extension": extension,
                "format": EXTENSIONS[extension],
                "version": version,
                "size": size,
                "sha256": digest,
                "detail": detail,
            }
            files.append(record)

            if extension == ".mdl" and detail.get("header_kind") == "model":
                stem = Path(filename).stem

                # Textures: a model that declares none keeps them in "<stem>T.mdl".
                if detail.get("numtextures") == 0:
                    expected_name = stem + "T.mdl"
                    if expected_name.lower() not in filenames_lower:
                        expected_path = (relative_dir / expected_name
                                         if relative_dir != Path(".") else Path(expected_name))
                        missing_sidecars.append(
                            {
                                "path": relative_path,
                                "kind": "texture",
                                "expected": expected_path.as_posix(),
                            }
                        )

                # Sequences: group 0 is the model itself, groups 1..N-1 are "<stem>01.mdl".
                groups = detail.get("numseqgroups", 0)
                missing_groups = []
                for group in range(1, min(groups, 100)):
                    expected_name = f"{stem}{group:02d}.mdl"
                    if expected_name.lower() not in filenames_lower:
                        expected_path = (relative_dir / expected_name
                                         if relative_dir != Path(".") else Path(expected_name))
                        missing_groups.append(expected_path.as_posix())
                if missing_groups:
                    missing_sidecars.append(
                        {
                            "path": relative_path,
                            "kind": "sequence-group",
                            "expected": missing_groups,
                            "missing": len(missing_groups),
                        }
                    )

    files.sort(key=lambda record: record["path"].lower())
    errors.sort(key=lambda record: record["path"].lower())
    missing_sidecars.sort(key=lambda record: record["path"].lower())
    return files, errors, missing_sidecars


def index_wads(root: Path, folders: set[str]) -> dict[str, set[str]]:
    """Map lowercase WAD basename -> game folders that ship it."""
    index: dict[str, set[str]] = defaultdict(set)
    for folder in folders:
        base = root / folder
        if not base.is_dir():
            continue
        for dirpath, _, filenames in os.walk(base):
            for name in filenames:
                if name.lower().endswith(".wad"):
                    index[name.lower()].add(folder)
    return index


def resolve_wad(game_folder: str, basename: str, wad_index: dict[str, set[str]]) -> str:
    owners = wad_index.get(basename, set())
    if game_folder in owners:
        return "game"
    if "valve" in owners:
        return "valve"
    others = sorted(owners - {game_folder, "valve"}, key=str.lower)
    if others:
        return "other:" + others[0]
    return "missing"


def wad_findings(files: list[dict], game_folder: str,
                 wad_index: dict[str, set[str]]) -> list[dict]:
    entries: list[dict] = []
    seen: set[tuple[str, str]] = set()
    for record in files:
        if record["format"] != "bsp":
            continue
        for reference in record["detail"].get("wad_refs", []):
            basename = reference.replace("\\", "/").rsplit("/", 1)[-1].lower()
            if not basename:
                continue
            key = (record["path"].lower(), basename)
            if key in seen:
                continue
            seen.add(key)
            entries.append(
                {
                    "map": record["path"],
                    "reference": reference,
                    "basename": basename,
                    "resolved": resolve_wad(game_folder, basename, wad_index),
                }
            )
    entries.sort(key=lambda entry: (entry["map"].lower(), entry["basename"]))
    return entries


def build_hash_index(game_files: dict[str, list[dict]]) -> dict[str, list[tuple[str, dict]]]:
    index: dict[str, list[tuple[str, dict]]] = defaultdict(list)
    for game, files in game_files.items():
        for record in files:
            index[record["sha256"]].append((game, record))
    return index


def cross_game_duplicates(
    hash_index: dict[str, list[tuple[str, dict]]]
) -> list[dict]:
    groups: list[dict] = []
    for digest, entries in hash_index.items():
        if len({game for game, _ in entries}) < 2:
            continue
        entries.sort(key=lambda item: (item[0].lower(), item[1]["path"].lower()))
        groups.append(
            {
                "sha256": digest,
                "size": entries[0][1]["size"],
                "games": sorted({game for game, _ in entries}, key=str.lower),
                "paths": [{"game": game, "path": record["path"]} for game, record in entries],
            }
        )
    groups.sort(key=lambda group: (-group["size"], group["sha256"]))
    return groups


def duplicate_summary(
    game: str, files: list[dict], hash_index: dict[str, list[tuple[str, dict]]]
) -> dict:
    partners: Counter[str] = Counter()
    shared_files = 0
    shared_bytes = 0
    for record in files:
        others = {other for other, _ in hash_index[record["sha256"]] if other != game}
        if others:
            shared_files += 1
            shared_bytes += record["size"]
            for other in others:
                partners[other] += 1
    return {
        "cross_game_shared_files": shared_files,
        "cross_game_shared_bytes": shared_bytes,
        "files_unique_to_game": len(files) - shared_files,
        "partners": dict(sorted(partners.items(), key=lambda item: (-item[1], item[0].lower()))),
    }


def format_versions(files: list[dict]) -> dict[str, dict[str, int]]:
    versions: dict[str, Counter[str]] = defaultdict(Counter)
    for record in files:
        if record["version"] is None:
            continue
        versions[record["format"]][str(record["version"])] += 1

    def version_key(value: str) -> tuple[int, int | str]:
        try:
            return (0, int(value))
        except ValueError:
            return (1, value)

    return {
        fmt: {version: count for version, count in sorted(counts.items(), key=lambda kv: version_key(kv[0]))}
        for fmt, counts in sorted(versions.items())
    }


def build_game_document(game: Game, files: list[dict], findings: dict, duplicates: dict) -> dict:
    by_extension: dict[str, dict[str, int]] = {}
    for record in files:
        bucket = by_extension.setdefault(record["extension"], {"files": 0, "bytes": 0})
        bucket["files"] += 1
        bucket["bytes"] += record["size"]
    by_extension = {
        extension: by_extension[extension] for extension in sorted(by_extension)
    }
    return {
        "schema": SCHEMA_VERSION,
        "tool": "tools/corpus_scan.py",
        "tool_version": TOOL_VERSION,
        "game": game.folder,
        "display_name": game.display_name,
        "app_id": game.app_id,
        "variant_of": game.variant_of,
        "totals": {
            "files": len(files),
            "bytes": sum(record["size"] for record in files),
            "by_extension": by_extension,
        },
        "format_versions": format_versions(files),
        "duplicates": duplicates,
        "findings": findings,
        "files": files,
    }


# --------------------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------------------


def render_game_doc(document: dict) -> str:
    game = document["game"]
    display_name = document["display_name"]
    totals = document["totals"]
    findings = document["findings"]
    duplicates = document["duplicates"]

    lines = [
        f"# {game} — {display_name}",
        "",
        "> Generated by `tools/corpus_scan.py`. Do not edit by hand.",
        f"> Schema {SCHEMA_VERSION}, tool {TOOL_VERSION}. Scanned extensions only.",
        "",
        "## Inventory",
        "",
        f"**{totals['files']:,} files**, **{totals['bytes']:,} bytes**.",
        "",
        "| Extension | Files | Bytes |",
        "|---|---:|---:|",
    ]
    for extension, counts in totals["by_extension"].items():
        lines.append(f"| `{extension}` | {counts['files']:,} | {counts['bytes']:,} |")

    lines += ["", "## Format versions", "", "| Format | Version | Files |", "|---|---:|---:|"]
    version_rows = 0
    for fmt, versions in document["format_versions"].items():
        for version, count in versions.items():
            lines.append(f"| `{fmt}` | {version} | {count:,} |")
            version_rows += 1
    if version_rows == 0:
        lines.append("| — | — | — |")
    unversioned = sorted(
        {record["format"] for record in document["files"] if record["version"] is None}
    )
    if unversioned:
        lines.append("")
        lines.append(
            "No version field: "
            + ", ".join(f"`{fmt}`" for fmt in unversioned)
            + ". Those records carry size, hash and format-specific header facts only."
        )

    layouts = findings.get("bsp_lump_layouts", {})
    if layouts:
        lines.append("")
        lines.append(
            "BSP lump layouts: "
            + ", ".join(f"`{name}` {count:,}" for name, count in layouts.items())
            + "."
        )

    lines += ["", "## Findings", ""]
    maps_missing = findings["maps_without_worldspawn"]
    maps_unchecked = findings["maps_not_checked"]
    models_missing = findings["models_without_sidecar"]
    wad_refs = findings["wad_references"]
    wads_missing = findings["wads_missing"]
    wads_other = findings["wads_only_in_other_game"]
    parse_errors = findings["parse_errors"]
    zero_bytes = findings["zero_byte_files"]

    lines.append(f"- Maps without `worldspawn`: **{len(maps_missing):,}**")
    for record in maps_missing[:5]:
        lines.append(f"  - `{record['path']}`")
    if len(maps_missing) > 5:
        lines.append(f"  - ... and {len(maps_missing) - 5:,} more in the JSON.")

    lines.append(f"- Maps not checked (not BSP v30, or entity lump invalid): **{len(maps_unchecked):,}**")
    for record in maps_unchecked[:5]:
        lines.append(f"  - `{record['path']}` — {record['reason']}")
    if len(maps_unchecked) > 5:
        lines.append(f"  - ... and {len(maps_unchecked) - 5:,} more in the JSON.")

    lines.append(f"- Models with a missing sidecar: **{len(models_missing):,}**")
    for record in models_missing[:5]:
        if record["kind"] == "texture":
            lines.append(
                f"  - `{record['path']}` — texture companion `{record['expected']}` is absent"
            )
        else:
            names = ", ".join(f"`{name}`" for name in record["expected"][:3])
            suffix = " ..." if len(record["expected"]) > 3 else ""
            lines.append(
                f"  - `{record['path']}` — {record['missing']} sequence group(s) absent: "
                f"{names}{suffix}"
            )
    if len(models_missing) > 5:
        lines.append(f"  - ... and {len(models_missing) - 5:,} more in the JSON.")

    maps_with_wads = len({entry["map"] for entry in wad_refs})
    lines.append(
        f"- WAD references: **{len(wad_refs):,}** across **{maps_with_wads:,}** maps; "
        f"missing **{len(wads_missing):,}**, resolved only in another game **{len(wads_other):,}**."
    )
    if wads_missing:
        missing_names = sorted({entry["basename"] for entry in wads_missing})
        preview = ", ".join(f"`{name}`" for name in missing_names[:8])
        suffix = " ..." if len(missing_names) > 8 else ""
        lines.append(f"  - Missing WADs: {preview}{suffix}")
    if wads_other:
        other_names = sorted({entry["basename"] for entry in wads_other})
        preview = ", ".join(f"`{name}`" for name in other_names[:8])
        suffix = " ..." if len(other_names) > 8 else ""
        lines.append(f"  - Present only in another game: {preview}{suffix}")

    lines.append(f"- Parse errors: **{len(parse_errors):,}**")
    for record in parse_errors[:5]:
        lines.append(f"  - `{record['path']}` — {record['error']}")
    if len(parse_errors) > 5:
        lines.append(f"  - ... and {len(parse_errors) - 5:,} more in the JSON.")

    lines.append(f"- Zero-byte files: **{len(zero_bytes):,}**")
    for record in zero_bytes[:5]:
        lines.append(f"  - `{record['path']}`")

    lines += [
        "",
        "## Duplication",
        "",
        f"- Files with a byte-identical copy in another game: "
        f"**{duplicates['cross_game_shared_files']:,}** "
        f"({duplicates['cross_game_shared_bytes']:,} bytes).",
        f"- Files with no copy outside this folder: "
        f"**{duplicates['files_unique_to_game']:,}**.",
    ]
    if duplicates["partners"]:
        partners = ", ".join(
            f"`{game_name}` ({count:,})"
            for game_name, count in list(duplicates["partners"].items())[:6]
        )
        lines.append(f"- Overlap by game: {partners}.")

    largest = sorted(document["files"], key=lambda record: (-record["size"], record["path"].lower()))[:10]
    lines += ["", "## Largest files", "", "| Path | Bytes | Format |", "|---|---:|---|"]
    for record in largest:
        lines.append(f"| `{record['path']}` | {record['size']:,} | `{record['format']}` |")

    lines += [
        "",
        "## Regenerate",
        "",
        "```powershell",
        "python tools/corpus_scan.py scan",
        "```",
        "",
    ]
    return "\n".join(lines)


def render_index_doc(index: dict) -> str:
    lines = [
        "# GoldSrc corpus by game",
        "",
        "> Generated by `tools/corpus_scan.py`. Do not edit by hand.",
        f"> Schema {SCHEMA_VERSION}, tool {TOOL_VERSION}.",
        "",
        "## Totals",
        "",
        f"**{index['totals']['games']:,} game folders**, "
        f"**{index['totals']['files']:,} files**, "
        f"**{index['totals']['bytes']:,} bytes**.",
        "",
        "| Folder | Game | App | Files | Bytes | Unique to folder |",
        "|---|---|---:|---:|---:|---:|",
    ]
    for game in index["games"]:
        app = "" if game["app_id"] is None else str(game["app_id"])
        lines.append(
            f"| `{game['folder']}` | {game['display_name']} | {app} | "
            f"{game['files']:,} | {game['bytes']:,} | {game['unique_files']:,} |"
        )

    lines += ["", "## Format versions", "", "| Format | Version | Files |", "|---|---:|---:|"]
    for fmt, versions in index["format_versions"].items():
        for version, count in versions.items():
            lines.append(f"| `{fmt}` | {version} | {count:,} |")

    lines += ["", "## BSP lump layouts", "", "| Layout | Maps |", "|---|---:|"]
    for name, count in index.get("bsp_lump_layouts", {}).items():
        lines.append(f"| `{name}` | {count:,} |")

    lines += ["", "## Findings", ""]
    for key, value in index["findings"].items():
        lines.append(f"- {key.replace('_', ' ')}: **{value:,}**")
    lines += [
        "",
        "## Duplicates",
        "",
        f"- Cross-game duplicate groups: **{index['duplicates']['groups']:,}**.",
        f"- Files in those groups: **{index['duplicates']['files']:,}**.",
        f"- Redundant bytes: **{index['duplicates']['redundant_bytes']:,}**.",
        "",
        "## Regenerate",
        "",
        "```powershell",
        "python tools/corpus_scan.py scan",
        "python tools/corpus_scan.py report",
        "```",
        "",
    ]
    return "\n".join(lines)


def render_delta_doc(result: dict, limit: int) -> str:
    a = result["a"]
    b = result["b"]
    summary = result["summary"]
    lines = [
        f"# {a} vs {b}",
        "",
        f"> Generated by `tools/corpus_scan.py diff {a} {b}`. Do not edit by hand.",
        "",
        "Scanned Stage 0 formats only. A path that exists in both folders is compared by "
        "SHA-256, not by name.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "|---|---:|",
        f"| Files in `{a}` | {summary['a_files']:,} |",
        f"| Files in `{b}` | {summary['b_files']:,} |",
        f"| Shared paths | {summary['shared_paths']:,} |",
        f"| Byte-identical shared paths | {summary['identical']:,} |",
        f"| Differing shared paths | {summary['differing']:,} |",
        f"| Only in `{a}` | {summary['only_in_a']:,} |",
        f"| Only in `{b}` | {summary['only_in_b']:,} |",
        "",
        "## Differing shared paths",
        "",
    ]
    if result["differing"]:
        lines += ["| Path | " + a + " bytes | " + b + " bytes |", "|---|---:|---:|"]
        for record in result["differing"]:
            lines.append(
                f"| `{record['path']}` | {record['a_size']:,} | {record['b_size']:,} |"
            )
    else:
        lines.append("None. Every shared path is byte-identical.")

    lines += ["", f"## Files only in {b}", ""]
    if result["only_in_b"]:
        lines += ["| Path | Bytes | Format |", "|---|---:|---|"]
        for record in result["only_in_b"]:
            lines.append(f"| `{record['path']}` | {record['size']:,} | `{record['format']}` |")
    else:
        lines.append("None.")

    lines += ["", f"## Files only in {a}", ""]
    if result["only_in_a"]:
        lines += ["| Path | Bytes | Format |", "|---|---:|---|"]
        shown = result["only_in_a"][:limit]
        for record in shown:
            lines.append(f"| `{record['path']}` | {record['size']:,} | `{record['format']}` |")
        if len(result["only_in_a"]) > len(shown):
            lines.append(
                f"| ... | | | {len(result['only_in_a']) - len(shown):,} more in the JSON |"
            )
    else:
        lines.append("None.")

    lines += ["", "## Reproduce", "", "```powershell",
              f"python tools/corpus_scan.py diff {a} {b}", "```", ""]
    return "\n".join(lines)


# --------------------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------------------


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(data, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def command_scan(args: argparse.Namespace) -> int:
    root = resolve_corpus_root(args.root)
    games = discover_games(root)
    if args.games:
        wanted = {name.strip().lower() for name in args.games.split(",") if name.strip()}
        games = [game for game in games if game.folder.lower() in wanted]
        missing = wanted - {game.folder.lower() for game in games}
        if missing:
            raise SystemExit(f"Unknown game folder(s): {', '.join(sorted(missing))}")
    if not games:
        raise SystemExit("No game folders found.")
    if "valve" not in {game.folder for game in games}:
        raise SystemExit("The valve folder is required: it is the fallback for shared WADs.")

    if not args.quiet:
        print(f"Corpus root: {root}")
        print(f"Games: {len(games)}")

    game_files: dict[str, list[dict]] = {}
    game_errors: dict[str, list[dict]] = {}
    game_sidecars: dict[str, list[dict]] = {}
    for game in games:
        files, errors, missing_sidecars = scan_game(root, game)
        game_files[game.folder] = files
        game_errors[game.folder] = errors
        game_sidecars[game.folder] = missing_sidecars
        total_bytes = sum(record["size"] for record in files)
        if not args.quiet:
            print(f"  {game.folder:<18} {len(files):>6,} files {total_bytes:>14,} bytes")

    wad_index = index_wads(root, {game.folder for game in games} | {"valve"})
    hash_index = build_hash_index(game_files)
    duplicate_groups = cross_game_duplicates(hash_index)

    documents: dict[str, dict] = {}
    for game in games:
        files = game_files[game.folder]
        wad_entries = wad_findings(files, game.folder, wad_index)
        findings = {
            "maps_without_worldspawn": [
                {"path": record["path"], "size": record["size"]}
                for record in files
                if record["format"] == "bsp"
                and record["detail"].get("has_worldspawn") is False
            ],
            "maps_not_checked": [
                {
                    "path": record["path"],
                    "version": record["version"],
                    "reason": (
                        record["detail"].get("entity_lump_error")
                        or f"BSP version {record['version']} != 30"
                    ),
                }
                for record in files
                if record["format"] == "bsp"
                and record["detail"].get("has_worldspawn") is None
            ],
            "models_without_sidecar": game_sidecars[game.folder],
            "wad_references": wad_entries,
            "wads_missing": [entry for entry in wad_entries if entry["resolved"] == "missing"],
            "wads_only_in_other_game": [
                entry for entry in wad_entries if entry["resolved"].startswith("other:")
            ],
            "parse_errors": game_errors[game.folder],
            "zero_byte_files": [
                {"path": record["path"]} for record in files if record["size"] == 0
            ],
            "bsp_lump_layouts": dict(
                sorted(
                    Counter(
                        record["detail"].get("lump_layout", "unknown")
                        for record in files
                        if record["format"] == "bsp"
                    ).items()
                )
            ),
        }
        duplicates = duplicate_summary(game.folder, files, hash_index)
        documents[game.folder] = build_game_document(game, files, findings, duplicates)

    for game in games:
        write_json(CORPUS_DIR / f"{game.folder}.json", documents[game.folder])
        write_text(GAMES_DOC_DIR / f"{game.folder}.md", render_game_doc(documents[game.folder]))

    all_documents = list(documents.values())
    total_files = sum(document["totals"]["files"] for document in all_documents)
    total_bytes = sum(document["totals"]["bytes"] for document in all_documents)
    aggregate_versions: dict[str, Counter[str]] = defaultdict(Counter)
    for document in all_documents:
        for fmt, versions in document["format_versions"].items():
            aggregate_versions[fmt].update(versions)
    aggregate_findings = {
        key: sum(len(document["findings"][key]) for document in all_documents)
        for key in (
            "maps_without_worldspawn",
            "maps_not_checked",
            "models_without_sidecar",
            "wad_references",
            "wads_missing",
            "wads_only_in_other_game",
            "parse_errors",
            "zero_byte_files",
        )
    }
    aggregate_layouts: Counter[str] = Counter()
    for document in all_documents:
        aggregate_layouts.update(document["findings"]["bsp_lump_layouts"])
    index = {
        "schema": SCHEMA_VERSION,
        "tool": "tools/corpus_scan.py",
        "tool_version": TOOL_VERSION,
        "corpus_root_name": root.name,
        "games": [
            {
                "folder": document["game"],
                "display_name": document["display_name"],
                "app_id": document["app_id"],
                "variant_of": document["variant_of"],
                "files": document["totals"]["files"],
                "bytes": document["totals"]["bytes"],
                "unique_files": document["duplicates"]["files_unique_to_game"],
            }
            for document in all_documents
        ],
        "totals": {
            "games": len(all_documents),
            "files": total_files,
            "bytes": total_bytes,
        },
        "format_versions": {
            fmt: {version: count for version, count in sorted(counts.items(), key=lambda kv: int(kv[0]))}
            for fmt, counts in sorted(aggregate_versions.items())
        },
        "bsp_lump_layouts": dict(sorted(aggregate_layouts.items())),
        "findings": aggregate_findings,
        "duplicates": {
            "groups": len(duplicate_groups),
            "files": sum(len(group["paths"]) for group in duplicate_groups),
            "redundant_bytes": sum(
                group["size"] * (len(group["paths"]) - 1) for group in duplicate_groups
            ),
        },
    }
    write_json(CORPUS_DIR / "index.json", index)
    write_json(
        CORPUS_DIR / "duplicates.json",
        {
            "schema": SCHEMA_VERSION,
            "tool": "tools/corpus_scan.py",
            "tool_version": TOOL_VERSION,
            "group_count": len(duplicate_groups),
            "groups": duplicate_groups,
        },
    )
    write_text(GAMES_DOC_DIR / "README.md", render_index_doc(index))

    if not args.quiet:
        print(
            f"Wrote {len(documents)} game JSON files, {len(documents)} game docs, "
            f"index.json, duplicates.json."
        )
        print(
            f"Totals: {total_files:,} files, {total_bytes:,} bytes, "
            f"{len(duplicate_groups):,} cross-game duplicate groups."
        )
    if aggregate_findings["parse_errors"]:
        print(f"Parse/read errors: {aggregate_findings['parse_errors']:,}", file=sys.stderr)
    return 0


def load_corpus_documents() -> dict[str, dict]:
    index_path = CORPUS_DIR / "index.json"
    if not index_path.is_file():
        raise SystemExit("No corpus index found. Run `python tools/corpus_scan.py scan` first.")
    index = json.loads(index_path.read_text(encoding="utf-8"))
    documents: dict[str, dict] = {}
    for entry in index["games"]:
        path = CORPUS_DIR / f"{entry['folder']}.json"
        if not path.is_file():
            raise SystemExit(f"Missing corpus file for {entry['folder']}: {path}")
        documents[entry["folder"]] = json.loads(path.read_text(encoding="utf-8"))
    return documents


def _closest_matches(documents: dict[str, dict], query: str) -> list[tuple[str, dict]]:
    """Best basename matches by how many query directory components the path contains."""
    parts = [part for part in query.split("/") if part]
    if not parts:
        return []
    basename = parts[-1]
    candidates: list[tuple[int, str, dict]] = []
    for game, document in documents.items():
        for record in document["files"]:
            path_parts = record["path"].lower().split("/")
            if path_parts[-1] != basename:
                continue
            score = sum(1 for part in parts[:-1] if part in path_parts)
            candidates.append((score, game, record))
    if not candidates:
        return []
    best = max(score for score, _, _ in candidates)
    matches = [(game, record) for score, game, record in candidates if score == best]
    matches.sort(key=lambda item: (item[0].lower(), item[1]["path"].lower()))
    return matches


def command_where(args: argparse.Namespace) -> int:
    documents = load_corpus_documents()
    query = args.path.replace("\\", "/").strip("/").lower()
    if not query:
        raise SystemExit("Empty path.")

    exact: list[tuple[str, dict]] = []
    for game, document in documents.items():
        for record in document["files"]:
            if record["path"].lower() == query:
                exact.append((game, record))
    if exact:
        mode = "exact"
        matches = exact
    else:
        suffix = "/" + query
        matches = [
            (game, record)
            for game, document in documents.items()
            for record in document["files"]
            if record["path"].lower().endswith(suffix)
        ]
        if matches:
            mode = "suffix"
        else:
            matches = _closest_matches(documents, query)
            mode = "closest"

    if not matches:
        print(f"No file matches `{query}` in the scanned corpus.")
        return 1

    groups: dict[str, list[tuple[str, dict]]] = defaultdict(list)
    for game, record in matches:
        groups[record["sha256"]].append((game, record))

    result = {
        "query": query,
        "match_mode": mode,
        "matches": [
            {
                "game": game,
                "path": record["path"],
                "size": record["size"],
                "sha256": record["sha256"],
                "format": record["format"],
                "version": record["version"],
            }
            for game, record in matches
        ],
        "groups": [
            {
                "sha256": digest,
                "size": entries[0][1]["size"],
                "games": sorted({game for game, _ in entries}, key=str.lower),
                "paths": [record["path"] for _, record in entries],
            }
            for digest, entries in groups.items()
        ],
    }

    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 0

    print(f"Query: {query}")
    if mode == "exact":
        print("Match: exact path.")
    elif mode == "suffix":
        print("Match: path suffix.")
    else:
        print("Match: closest paths (no exact path; ranked by shared directory components).")
    print("")

    for game, record in matches:
        print(
            f"  {game:<18} {record['path']:<60} "
            f"{record['size']:>12,} bytes  sha256:{record['sha256']}"
        )

    print("")
    if len(groups) == 1:
        print("All matches are byte-identical.")
    else:
        print(f"Not byte-identical: {len(groups)} distinct byte sequences.")
    for number, (digest, entries) in enumerate(
        sorted(groups.items(), key=lambda item: (-item[1][0][1]["size"], item[0])), start=1
    ):
        games = ", ".join(sorted({game for game, _ in entries}, key=str.lower))
        print(f"  [{number}] {entries[0][1]['size']:,} bytes  {games}  sha256:{digest}")
    return 0


def compare_documents(a: dict, b: dict) -> dict:
    a_by_path = {record["path"].lower(): record for record in a["files"]}
    b_by_path = {record["path"].lower(): record for record in b["files"]}

    shared = sorted(set(a_by_path) & set(b_by_path))
    identical = [path for path in shared if a_by_path[path]["sha256"] == b_by_path[path]["sha256"]]
    differing = [
        {
            "path": a_by_path[path]["path"],
            "a_size": a_by_path[path]["size"],
            "b_size": b_by_path[path]["size"],
            "a_sha256": a_by_path[path]["sha256"],
            "b_sha256": b_by_path[path]["sha256"],
        }
        for path in shared
        if a_by_path[path]["sha256"] != b_by_path[path]["sha256"]
    ]
    only_in_a = [
        {
            "path": record["path"],
            "size": record["size"],
            "format": record["format"],
            "sha256": record["sha256"],
        }
        for path, record in sorted(a_by_path.items())
        if path not in b_by_path
    ]
    only_in_b = [
        {
            "path": record["path"],
            "size": record["size"],
            "format": record["format"],
            "sha256": record["sha256"],
        }
        for path, record in sorted(b_by_path.items())
        if path not in a_by_path
    ]

    return {
        "schema": SCHEMA_VERSION,
        "tool": "tools/corpus_scan.py",
        "tool_version": TOOL_VERSION,
        "a": a["game"],
        "b": b["game"],
        "summary": {
            "a_files": len(a["files"]),
            "a_bytes": a["totals"]["bytes"],
            "b_files": len(b["files"]),
            "b_bytes": b["totals"]["bytes"],
            "shared_paths": len(shared),
            "identical": len(identical),
            "differing": len(differing),
            "only_in_a": len(only_in_a),
            "only_in_b": len(only_in_b),
        },
        "identical": [a_by_path[path]["path"] for path in identical],
        "differing": differing,
        "only_in_a": only_in_a,
        "only_in_b": only_in_b,
    }


def command_diff(args: argparse.Namespace) -> int:
    documents = load_corpus_documents()
    for game in (args.a, args.b):
        if game not in documents:
            raise SystemExit(f"Unknown game folder `{game}`. Known: {', '.join(sorted(documents))}")

    result = compare_documents(documents[args.a], documents[args.b])
    summary = result["summary"]

    print(f"{args.a} vs {args.b}")
    print(f"  files: {summary['a_files']:,} vs {summary['b_files']:,}")
    print(f"  shared paths: {summary['shared_paths']:,}")
    print(f"  byte-identical: {summary['identical']:,}")
    print(f"  differing: {summary['differing']:,}")
    print(f"  only in {args.a}: {summary['only_in_a']:,}")
    print(f"  only in {args.b}: {summary['only_in_b']:,}")
    print("")

    differing = result["differing"]
    limit = len(differing) if args.all else args.limit
    if differing:
        print(f"Differing shared paths ({len(differing):,}):")
        for record in differing[:limit]:
            print(
                f"  {record['path']:<60} {record['a_size']:>12,} -> {record['b_size']:>12,} bytes"
            )
        if len(differing) > limit:
            print(f"  ... and {len(differing) - limit:,} more. Use --all or the JSON report.")
    else:
        print("Differing shared paths: none.")
    print("")

    if result["only_in_b"]:
        print(f"Only in {args.b} ({len(result['only_in_b']):,}):")
        for record in result["only_in_b"]:
            print(f"  {record['path']:<60} {record['size']:>12,} bytes  {record['format']}")
        print("")
    else:
        print(f"Only in {args.b}: none.")
        print("")

    if args.no_write:
        print("No report written (--no-write).")
        return 0

    json_path = Path(args.json_path) if args.json_path else DELTA_DIR / f"{args.a}-vs-{args.b}.json"
    doc_path = GAMES_DOC_DIR / f"{args.a}-vs-{args.b}.md"
    write_json(json_path, result)
    write_text(doc_path, render_delta_doc(result, limit=50))
    print(f"Wrote {json_path}")
    print(f"Wrote {doc_path}")
    return 0


def command_report(args: argparse.Namespace) -> int:
    documents = load_corpus_documents()
    index = json.loads((CORPUS_DIR / "index.json").read_text(encoding="utf-8"))

    print(
        f"Corpus: {index['totals']['games']:,} game folders, "
        f"{index['totals']['files']:,} files, {index['totals']['bytes']:,} bytes."
    )
    print("")
    print(f"{'Folder':<18} {'Game':<38} {'Files':>8} {'Bytes':>14} {'Unique':>8}")
    for game in index["games"]:
        print(
            f"{game['folder']:<18} {game['display_name']:<38} "
            f"{game['files']:>8,} {game['bytes']:>14,} {game['unique_files']:>8,}"
        )

    print("")
    print("Format versions:")
    for fmt, versions in index["format_versions"].items():
        rendered = ", ".join(f"{version}: {count:,}" for version, count in versions.items())
        print(f"  {fmt:<5} {rendered}")

    print("")
    print("BSP lump layouts:")
    for name, count in index.get("bsp_lump_layouts", {}).items():
        print(f"  {name:<10} {count:,}")

    print("")
    print("Findings:")
    for key, value in index["findings"].items():
        print(f"  {key.replace('_', ' '):<32} {value:,}")

    print("")
    print("Largest cross-game duplicate groups:")
    duplicates = json.loads((CORPUS_DIR / "duplicates.json").read_text(encoding="utf-8"))
    for group in duplicates["groups"][: args.limit]:
        games = ", ".join(group["games"])
        print(f"  {group['size']:>12,} bytes  {games}")
        for item in group["paths"][:4]:
            print(f"      {item['game']}/{item['path']}")
        if len(group["paths"]) > 4:
            print(f"      ... {len(group['paths']) - 4} more")

    zero_bytes = [
        (game, record)
        for game, document in documents.items()
        for record in document["files"]
        if record["size"] == 0
    ]
    parse_errors = [
        (game, record)
        for game, document in documents.items()
        for record in document["findings"]["parse_errors"]
    ]
    maps_missing = [
        (game, record)
        for game, document in documents.items()
        for record in document["findings"]["maps_without_worldspawn"]
    ]
    models_missing = [
        (game, record)
        for game, document in documents.items()
        for record in document["findings"]["models_without_sidecar"]
    ]
    missing_wads = [
        (game, record)
        for game, document in documents.items()
        for record in document["findings"]["wads_missing"]
    ]
    version_outliers = [
        (game, record)
        for game, document in documents.items()
        for record in document["files"]
        if record["format"] in VERSIONED_FORMATS
        and record["version"] != VERSIONED_FORMATS[record["format"]]
    ]

    print("")
    print(f"Zero-byte files: {len(zero_bytes):,}")
    for game, record in zero_bytes[: args.limit]:
        print(f"  {game}/{record['path']}")

    print("")
    print(f"Parse/read errors: {len(parse_errors):,}")
    for game, record in parse_errors[: args.limit]:
        print(f"  {game}/{record['path']} — {record['error']}")

    print("")
    print(f"Maps without worldspawn: {len(maps_missing):,}")
    for game, record in maps_missing[: args.limit]:
        print(f"  {game}/{record['path']}")

    print("")
    print(f"Models with a missing sidecar: {len(models_missing):,}")
    for game, record in models_missing[: args.limit]:
        if record["kind"] == "texture":
            print(f"  {game}/{record['path']} -> texture {record['expected']}")
        else:
            print(
                f"  {game}/{record['path']} -> {record['missing']} sequence group(s): "
                + ", ".join(record["expected"])
            )

    print("")
    print(f"WAD references missing: {len(missing_wads):,}")
    missing_by_name: dict[str, int] = defaultdict(int)
    for _, record in missing_wads:
        missing_by_name[record["basename"]] += 1
    for name, count in sorted(missing_by_name.items(), key=lambda item: (-item[1], item[0]))[: args.limit]:
        print(f"  {name:<40} {count:,} map references")

    print("")
    print(f"Version outliers: {len(version_outliers):,}")
    for game, record in version_outliers[: args.limit]:
        print(
            f"  {game}/{record['path']} — {record['format']} {record['version']} "
            f"(expected {VERSIONED_FORMATS[record['format']]})"
        )

    non_ascii = [
        (game, record)
        for game, document in documents.items()
        for record in document["files"]
        if any(ord(character) > 127 for character in record["path"])
    ]
    print("")
    print(f"Non-ASCII paths: {len(non_ascii):,}")
    for game, record in non_ascii[: args.limit]:
        print(f"  {game}/{record['path']}")

    # Same path, several games, different bytes. This is the map of "HD pack replaced it".
    by_path: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    for game, document in documents.items():
        for record in document["files"]:
            by_path[record["path"].lower()][game].add(record["sha256"])
    divergent = []
    for path, by_game in by_path.items():
        all_hashes: set[str] = set()
        for hashes in by_game.values():
            all_hashes |= hashes
        if len(by_game) > 1 and len(all_hashes) > 1:
            divergent.append((path, by_game))
    divergent.sort(key=lambda item: (-len(item[1]), item[0]))
    print("")
    print(f"Paths that exist in several games with different bytes: {len(divergent):,}")
    for path, by_game in divergent[: args.limit]:
        rendered = ", ".join(
            f"{game}:{len(hashes)}" for game, hashes in sorted(by_game.items())
        )
        print(f"  {path:<60} {rendered}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Stage 0 GoldSrc corpus inventory.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python tools/corpus_scan.py scan\n"
            "  python tools/corpus_scan.py where models/player/arctic.mdl\n"
            "  python tools/corpus_scan.py diff cstrike cstrike_hd\n"
            "  python tools/corpus_scan.py report\n"
        ),
    )
    parser.add_argument(
        "--root",
        help="Half-Life install root (default: QUEBRATSK_GAME_ROOT, then the Steam registry).",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    scan = subparsers.add_parser("scan", help="Walk the corpus and write data/corpus + docs.")
    scan.add_argument("--games", help="Comma-separated folder names (debugging subset).")
    scan.add_argument("--quiet", action="store_true", help="Suppress progress output.")
    scan.set_defaults(func=command_scan)

    where = subparsers.add_parser("where", help="Find a relative path across games and compare hashes.")
    where.add_argument("path", help="Relative path, e.g. models/player/arctic.mdl")
    where.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    where.set_defaults(func=command_where)

    diff = subparsers.add_parser("diff", help="Compare two game folders by relative path and SHA-256.")
    diff.add_argument("a")
    diff.add_argument("b")
    diff.add_argument("--json", dest="json_path", help="Override the delta JSON path.")
    diff.add_argument("--no-write", action="store_true", help="Print only; write no report.")
    diff.add_argument("--limit", type=int, default=40, help="Differences to print (default 40).")
    diff.add_argument("--all", action="store_true", help="Print every difference.")
    diff.set_defaults(func=command_diff)

    report = subparsers.add_parser("report", help="Print the corpus-wide anomaly report.")
    report.add_argument("--limit", type=int, default=20, help="Rows per section (default 20).")
    report.set_defaults(func=command_report)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
