#!/usr/bin/env python3
"""Validate every BSP30 map in the retail GoldSrc corpus.

BSP30 is the format the engine is built around: entities, geometry, lighting and
collision all come from one file. The lump table below was *measured*, not
remembered: it is the only assignment of the 15 lumps under which every record of
every lump satisfies cross-referenced bounds, and walking every model's hull-0
tree visits every node and every leaf.

    lump  0  entities      free-form text, braces balanced
    lump  1  planes        20 bytes: unit normal, distance, type
    lump  2  textures      nummiptex + offsets, embedded miptex headers
    lump  3  vertices      12 bytes: float xyz
    lump  4  visibility    opaque; bounded by leaf visofs
    lump  5  nodes         24 bytes: hull-0 BSP tree
    lump  6  texinfo       40 bytes: s/t projection + miptex index + flags
    lump  7  faces         20 bytes: plane, side, surfedge run, texinfo, styles
    lump  8  lighting      opaque RGB lightmaps; bounded by face lightmap offsets
    lump  9  clipnodes     8 bytes: hull-1..3 trees, children are node or CONTENTS
    lump 10  leafs         28 bytes: contents, visofs, box, marksurface run, ambient
    lump 11  marksurfaces  uint16 face indices
    lump 12  edges         uint16 vertex pairs
    lump 13  surfedges     int32 signed edge indices
    lump 14  models        64 bytes: bounds, four hull roots, face run

Blue Shift maps use one alternate layout: lumps 0 and 1 are swapped (planes first,
entities second). Everything else is identical. Detection is by content: the
entities lump starts with `{`.

Hard errors are defects a reader must reject. Warnings are facts worth knowing the
engine tolerates. Exit code is 1 when any hard error exists.

Usage:
    python tools/verify/validate_bsp30.py
    python tools/verify/validate_bsp30.py --limit 20
    python tools/verify/validate_bsp30.py --json data/validation/bsp30.json
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from corpus_scan import discover_games, resolve_corpus_root  # noqa: E402

HEADER = struct.Struct("<i30i")    # version + 15 x (offset, length)
HEADER_SIZE = HEADER.size          # 124

PLANE = struct.Struct("<4fi")      # normal[3], distance, type
VERTEX = struct.Struct("<3f")
TEXINFO = struct.Struct("<8f2i")   # vector_s[4], vector_t[4], miptex, flags
FACE = struct.Struct("<HHiHh4Bi")  # planenum, side, firstedge, numedges, texinfo, styles[4], lightofs
NODE = struct.Struct("<i2h6h2H")   # planenum, children[2], mins[3], maxs[3], firstface, numfaces
LEAF = struct.Struct("<2i6h2H4B")  # contents, visofs, mins[3], maxs[3], firstmark, nummark, ambient[4]
CLIPNODE = struct.Struct("<i2h")   # planenum, children[2]
EDGE = struct.Struct("<2H")
SURFEDGE = struct.Struct("<i")
MARKSURFACE = struct.Struct("<H")
MODEL = struct.Struct("<9f7i")     # mins[3], maxs[3], origin[3], headnode[4], visleafs, firstface, numfaces

SIZES = {
    "planes": 20, "vertices": 12, "texinfo": 40, "faces": 20, "nodes": 24,
    "leafs": 28, "clipnodes": 8, "edges": 4, "surfedges": 4, "marksurfaces": 2,
    "models": 64,
}

STANDARD_LAYOUT = {
    0: "entities", 1: "planes", 2: "textures", 3: "vertices", 4: "visibility",
    5: "nodes", 6: "texinfo", 7: "faces", 8: "lighting", 9: "clipnodes",
    10: "leafs", 11: "marksurfaces", 12: "edges", 13: "surfedges", 14: "models",
}
# Blue Shift swaps planes and entities; the other thirteen lumps are unchanged.
BSHIFT_LAYOUT = dict(STANDARD_LAYOUT)
BSHIFT_LAYOUT[0], BSHIFT_LAYOUT[1] = "planes", "entities"

CONTENTS_MIN, CONTENTS_MAX = -23, -1
MAX_COORD = 131072.0          # GoldSrc world extent bound; measured max is far below
MAX_TEXTURE_DIM = 4096
MAX_TEXINFO_NORM = 1024.0     # measured max is 256 (czero); above this is worth a look
PLANE_TYPE_RANGE = range(0, 9)


class BSPError(Exception):
    """A hard defect: the file cannot be read as a BSP30 map."""


def _text(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("latin-1")


def read_lumps(path: Path) -> tuple[int, list[bytes], int, list[tuple[int, int]]]:
    raw = path.read_bytes()
    if len(raw) < HEADER_SIZE:
        raise BSPError(f"file is {len(raw)} bytes, header needs {HEADER_SIZE}")
    version, *entries = HEADER.unpack_from(raw, 0)
    lumps: list[bytes] = []
    used_ranges: list[tuple[int, int]] = []
    for index, (offset, length) in enumerate(zip(entries[0::2], entries[1::2])):
        if offset < HEADER_SIZE or length < 0 or offset + length > len(raw):
            raise BSPError(f"lump {index} [{offset}, {offset + length}) is out of bounds")
        lumps.append(raw[offset:offset + length])
        if length:
            used_ranges.append((offset, offset + length))
    return version, lumps, len(raw), used_ranges


def detect_layout(lumps: list[bytes]) -> tuple[str, int]:
    """Entities is the lump that starts with '{'. Standard has it at 0, bshift at 1."""
    candidates = [index for index in (0, 1) if lumps[index].lstrip()[:1] == b"{"]
    if len(candidates) != 1:
        found = ", ".join(f"lump {index}" for index in candidates) or "none"
        raise BSPError(f"cannot locate entities lump among lumps 0-1 ({found})")
    index = candidates[0]
    layout = "standard" if index == 0 else "bshift"
    return layout, index


def check_entities(blob: bytes) -> tuple[list[str], list[str], int]:
    """Structural check with a quote-aware scanner.

    Braces inside quoted values are content, not structure: masked texture names
    like `{hrpoint` appear in infodecal values across the corpus.
    """
    hard: list[str] = []
    warn: list[str] = []
    depth = 0
    in_quote = False
    entities = 0
    for byte in blob:
        char = chr(byte)
        if char == '"':
            in_quote = not in_quote
        elif in_quote:
            continue
        elif char == "{":
            depth += 1
            if depth == 1:
                entities += 1
        elif char == "}":
            depth -= 1
            if depth < 0:
                hard.append("entities: '}' without a matching '{'")
                depth = 0
    if depth:
        hard.append(f"entities: {depth} unclosed block(s) at end of lump")
    if entities == 0:
        hard.append("entities: no top-level entity block")
    elif b"classname" not in blob:
        hard.append("entities: no 'classname' key in any entity")
    if not blob.rstrip(b"\x00 \t\r\n"):
        warn.append("entities lump is whitespace only")
    return hard, warn, entities


def check_planes(blob: bytes) -> tuple[list[str], list[str], Counter, float]:
    hard: list[str] = []
    warn: list[str] = []
    types: Counter[int] = Counter()
    worst = 1.0
    count = len(blob) // 20
    unpack = PLANE.unpack_from
    for index in range(count):
        nx, ny, nz, distance, plane_type = unpack(blob, index * 20)
        types[plane_type] += 1
        length = (nx * nx + ny * ny + nz * nz) ** 0.5
        worst = max(worst, abs(length - 1.0))
        if not (0.99 <= length <= 1.01):
            hard.append(f"plane {index}: normal length {length:.6f} is not unit")
        if plane_type not in PLANE_TYPE_RANGE:
            hard.append(f"plane {index}: type {plane_type} out of range")
        if distance != distance or abs(distance) > MAX_COORD * 4:
            hard.append(f"plane {index}: implausible distance {distance}")
    return hard, warn, types, worst


def check_vertices(blob: bytes) -> tuple[list[str], list[str], float]:
    hard: list[str] = []
    warn: list[str] = []
    count = len(blob) // 12
    unpack = VERTEX.unpack_from
    max_abs = 0.0
    for index in range(count):
        x, y, z = unpack(blob, index * 12)
        extent = max(abs(x), abs(y), abs(z))
        if extent != extent or extent > MAX_COORD:
            hard.append(f"vertex {index}: coordinate {max(x, y, z):.1f} outside world bounds")
        max_abs = max(max_abs, min(extent, MAX_COORD))
    if max_abs > 32768:
        warn.append(f"largest vertex coordinate is {max_abs:.0f} (beyond the 32768 era limit)")
    return hard, warn, max_abs


def check_textures(blob: bytes, path: str) -> tuple[list[str], list[str], dict]:
    """Check the texture directory and every embedded miptex header."""
    hard: list[str] = []
    warn: list[str] = []
    if len(blob) < 4:
        return [f"texture lump is {len(blob)} bytes, needs an int32 count"], warn, {}
    num_miptex, = struct.unpack_from("<i", blob, 0)
    if num_miptex < 0 or num_miptex > 100_000:
        return [f"texture count {num_miptex} is implausible"], warn, {}
    if 4 + num_miptex * 4 > len(blob):
        return [f"texture directory ({num_miptex} entries) overruns {len(blob)} bytes"], warn, {}
    offsets = struct.unpack_from(f"<{num_miptex}i", blob, 4) if num_miptex else ()
    embedded = 0
    external = 0
    for index, offset in enumerate(offsets):
        if offset == -1:
            external += 1
            continue
        if not 0 <= offset <= len(blob) - 40:
            hard.append(f"texture {index}: offset {offset} is out of bounds")
            continue
        name_raw, width, height, *mip_offsets = struct.unpack_from("<16s2I4I", blob, offset)
        if width == 0 or height == 0 or width > MAX_TEXTURE_DIM or height > MAX_TEXTURE_DIM:
            hard.append(f"texture {index} {_text(name_raw)!r}: dimensions {width}x{height}")
            continue
        if width % 16 or height % 16:
            warn.append(f"texture {index} {_text(name_raw)!r}: {width}x{height} not multiple of 16")
        if mip_offsets[0] == 0:
            # Header present, pixels live in a WAD named by texinfo usage.
            external += 1
            continue
        embedded += 1
        pixel_count = width * height
        chain = [pixel_count, pixel_count // 4, pixel_count // 16, pixel_count // 64]
        palette_at = mip_offsets[3] + chain[3]
        end = palette_at + 2 + 768
        if end > len(blob) - offset:
            hard.append(f"texture {index} {_text(name_raw)!r}: mip chain ends at {end}, "
                        f"beyond its {len(blob) - offset} bytes")
    if offsets and offsets[0] not in (4 + num_miptex * 4, 0):
        warn.append(f"first texture offset is {offsets[0]}, expected {4 + num_miptex * 4}")
    facts = {
        "num_miptex": num_miptex,
        "embedded": embedded,
        "external": external,
    }
    return hard, warn, facts


def check_texinfo(blob: bytes, num_miptex: int,
                  used: set[int]) -> tuple[list[str], list[str], Counter]:
    """Check texinfos. Faces tell us which ones matter: dead records may hold anything."""
    hard: list[str] = []
    warn: list[str] = []
    flags_seen: Counter[int] = Counter()
    count = len(blob) // 40
    unpack = TEXINFO.unpack_from
    for index in range(count):
        s_x, s_y, s_z, s_off, t_x, t_y, t_z, t_off, miptex, flags = unpack(blob, index * 40)
        flags_seen[flags] += 1
        if not -1 <= miptex < num_miptex:
            hard.append(f"texinfo {index}: miptex {miptex} outside [-1, {num_miptex})")
        referenced = index in used
        for name, vector in (("s", (s_x, s_y, s_z, s_off)), ("t", (t_x, t_y, t_z, t_off))):
            norm = (vector[0] ** 2 + vector[1] ** 2 + vector[2] ** 2) ** 0.5
            if any(v != v for v in vector):
                hard.append(f"texinfo {index}: {name} vector has a NaN component")
            elif norm == 0:
                # Measured on de_fastline_cz: zero-norm texinfos exist but are never
                # referenced by a face. A referenced one would divide by zero.
                if referenced:
                    hard.append(f"texinfo {index}: referenced by a face with a zero "
                                f"{name} vector")
                else:
                    warn.append(f"texinfo {index}: dead record with a zero {name} vector")
            elif norm > MAX_TEXINFO_NORM:
                state = "referenced" if referenced else "unreferenced"
                warn.append(f"texinfo {index}: {state} {name} vector norm {norm:.3f}")
    return hard, warn, flags_seen


def check_faces(blob: bytes, num_planes: int, num_texinfo: int, num_surfedges: int,
                lighting_len: int) -> tuple[list[str], list[str], Counter, int, set[int]]:
    hard: list[str] = []
    warn: list[str] = []
    styles_used: Counter[int] = Counter()
    lightmaps = 0
    used_texinfos: set[int] = set()
    count = len(blob) // 20
    unpack = FACE.unpack_from
    for index in range(count):
        values = unpack(blob, index * 20)
        planenum, side, firstedge, numedges, texinfo = values[0:5]
        styles = values[5:9]
        lightofs = values[9]
        if planenum >= num_planes:
            hard.append(f"face {index}: plane {planenum} >= {num_planes}")
        if side not in (0, 1):
            hard.append(f"face {index}: side {side}")
        if numedges < 0:
            hard.append(f"face {index}: negative edge count {numedges}")
        elif firstedge + numedges > num_surfedges:
            hard.append(f"face {index}: surfedge run [{firstedge}, {firstedge + numedges}) "
                        f"> {num_surfedges}")
        if not -1 <= texinfo < num_texinfo:
            hard.append(f"face {index}: texinfo {texinfo} outside [-1, {num_texinfo})")
        else:
            used_texinfos.add(texinfo)
        for style in styles:
            styles_used[style] += 1
        if lightofs == -1:
            continue
        if lighting_len == 0:
            # Measured on cz_worldmap.bsp: a map can ship with an empty lighting
            # lump while faces still carry lightmap offsets. Those offsets are
            # meaningless without light data; the renderer goes fullbright.
            warn.append(f"face {index}: lightmap offset {lightofs} but the lighting "
                        f"lump is empty")
            continue
        if not 0 <= lightofs < lighting_len:
            hard.append(f"face {index}: lightmap offset {lightofs} outside lighting "
                        f"({lighting_len} bytes)")
        else:
            lightmaps += 1
    return hard, warn, styles_used, lightmaps, used_texinfos


def check_nodes(blob: bytes, num_planes: int, num_leafs: int, num_faces: int,
                model_roots: list[int]) -> tuple[list[str], list[str], dict]:
    hard: list[str] = []
    warn: list[str] = []
    count = len(blob) // 24
    unpack = NODE.unpack_from
    for index in range(count):
        planenum, child_a, child_b, *rest = unpack(blob, index * 24)
        mins, maxs = rest[0:3], rest[3:6]
        firstface, numfaces = rest[6], rest[7]
        if not 0 <= planenum < num_planes:
            hard.append(f"node {index}: plane {planenum} >= {num_planes}")
        for child in (child_a, child_b):
            if child >= 0:
                if child >= count:
                    hard.append(f"node {index}: child {child} >= {count}")
            elif -1 - child >= num_leafs:
                hard.append(f"node {index}: child {child} points at leaf {-1 - child} "
                            f">= {num_leafs}")
        if firstface + numfaces > num_faces:
            hard.append(f"node {index}: face run [{firstface}, {firstface + numfaces}) "
                        f"> {num_faces}")
        if any(lo > hi for lo, hi in zip(mins, maxs)):
            warn.append(f"node {index}: inverted box")
    # Walk every model's hull-0 tree; a healthy map reaches every node and leaf.
    reachable: set[int] = set()
    leaf_refs: set[int] = set()
    for root in model_roots:
        stack = [root] if 0 <= root < count else []
        while stack:
            index = stack.pop()
            if index in reachable:
                continue
            reachable.add(index)
            _, child_a, child_b = unpack(blob, index * 24)[:3]
            for child in (child_a, child_b):
                if child >= 0:
                    stack.append(child)
                else:
                    leaf_refs.add(-1 - child)
    unreachable = count - len(reachable)
    if unreachable:
        warn.append(f"{unreachable} of {count} nodes unreachable from model roots")
    if leaf_refs and len(leaf_refs) < num_leafs:
        warn.append(f"{num_leafs - len(leaf_refs)} of {num_leafs} leafs never referenced")
    facts = {"reachable_nodes": len(reachable), "referenced_leafs": len(leaf_refs)}
    return hard, warn, facts


def check_clipnodes(blob: bytes, num_planes: int) -> tuple[list[str], list[str]]:
    hard: list[str] = []
    count = len(blob) // 8
    unpack = CLIPNODE.unpack_from
    for index in range(count):
        planenum, child_a, child_b = unpack(blob, index * 8)
        if not 0 <= planenum < num_planes:
            hard.append(f"clipnode {index}: plane {planenum} >= {num_planes}")
        for child in (child_a, child_b):
            if child >= 0:
                if child >= count:
                    hard.append(f"clipnode {index}: child {child} >= {count}")
            elif not CONTENTS_MIN <= child <= CONTENTS_MAX:
                hard.append(f"clipnode {index}: child {child} is not a content type")
    return hard, []


def check_leafs(blob: bytes, vis_len: int, num_marksurfaces: int) -> tuple[
        list[str], list[str], Counter, Counter]:
    hard: list[str] = []
    warn: list[str] = []
    contents_seen: Counter[int] = Counter()
    ambient_seen: Counter[bytes] = Counter()
    count = len(blob) // 28
    unpack = LEAF.unpack_from
    for index in range(count):
        contents, visofs, *rest = unpack(blob, index * 28)
        mins, maxs = rest[0:3], rest[3:6]
        firstmark, nummark = rest[6], rest[7]
        ambient = bytes(rest[8:12])
        contents_seen[contents] += 1
        ambient_seen[ambient] += 1
        if not CONTENTS_MIN <= contents <= CONTENTS_MAX:
            hard.append(f"leaf {index}: contents {contents}")
        if visofs != -1 and not 0 <= visofs < vis_len:
            hard.append(f"leaf {index}: visofs {visofs} outside visibility ({vis_len} bytes)")
        if firstmark + nummark > num_marksurfaces:
            hard.append(f"leaf {index}: marksurface run [{firstmark}, {firstmark + nummark}) "
                        f"> {num_marksurfaces}")
        if any(lo > hi for lo, hi in zip(mins, maxs)):
            # Measured on de_airstrip, cz_fastline4, cz_sandstorm2: inverted
            # boxes occur on EMPTY leafs (contents -1), all of them referenced
            # by nodes. It is the unset-box convention for empty space.
            warn.append(f"leaf {index}: inverted box on contents {contents} leaf")
    return hard, warn, contents_seen, ambient_seen


def check_edges(blob: bytes, num_vertices: int) -> list[str]:
    hard: list[str] = []
    count = len(blob) // 4
    unpack = EDGE.unpack_from
    for index in range(count):
        a, b = unpack(blob, index * 4)
        if a >= num_vertices or b >= num_vertices:
            hard.append(f"edge {index}: vertex index ({a}, {b}) >= {num_vertices}")
    return hard


def check_surfedges(blob: bytes, num_edges: int) -> list[str]:
    hard: list[str] = []
    count = len(blob) // 4
    unpack = SURFEDGE.unpack_from
    for index in range(count):
        value, = unpack(blob, index * 4)
        if not 0 <= abs(value) < num_edges:
            hard.append(f"surfedge {index}: edge index {value} outside [0, {num_edges})")
    return hard


def check_marksurfaces(blob: bytes, num_faces: int) -> list[str]:
    hard: list[str] = []
    count = len(blob) // 2
    unpack = MARKSURFACE.unpack_from
    for index in range(count):
        value, = unpack(blob, index * 2)
        if value >= num_faces:
            hard.append(f"marksurface {index}: face {value} >= {num_faces}")
    return hard


def check_models(blob: bytes, num_nodes: int, num_clipnodes: int,
                 num_faces: int) -> tuple[list[str], list[str], list[int]]:
    hard: list[str] = []
    warn: list[str] = []
    roots: list[int] = []
    count = len(blob) // 64
    unpack = MODEL.unpack_from
    for index in range(count):
        values = unpack(blob, index * 64)
        mins, maxs, origin = values[0:3], values[3:6], values[6:9]
        head_nodes = values[9:13]
        visleafs, firstface, numfaces = values[13], values[14], values[15]
        roots.append(head_nodes[0])
        if any(lo > hi + 0.001 for lo, hi in zip(mins, maxs)):
            warn.append(f"model {index}: inverted bounds")
        if head_nodes[0] >= num_nodes:
            hard.append(f"model {index}: hull-0 root {head_nodes[0]} >= {num_nodes} nodes")
        for hull, root in enumerate(head_nodes[1:], start=1):
            if root > num_clipnodes:
                hard.append(f"model {index}: hull-{hull} root {root} > {num_clipnodes} clipnodes")
            elif root == num_clipnodes:
                # Measured on Condition Zero maps (de_aztec_cz and friends): a hull
                # root equal to the clipnode count is the empty-tree sentinel — the
                # model has no collision for that hull.
                warn.append(f"model {index}: hull-{hull} root == clipnode count "
                            f"(empty-hull sentinel)")
        if visleafs < -1:
            hard.append(f"model {index}: visleafs {visleafs}")
        if firstface < 0 or numfaces < 0 or firstface + numfaces > num_faces:
            hard.append(f"model {index}: face run [{firstface}, {firstface + numfaces}) "
                        f"> {num_faces}")
    return hard, warn, roots


def validate_map(path: Path, relative: str) -> dict:
    version, lumps, file_size, used_ranges = read_lumps(path)
    if version != 30:
        raise BSPError(f"version {version}, expected 30")

    layout, entities_index = detect_layout(lumps)
    names = BSHIFT_LAYOUT if layout == "bshift" else STANDARD_LAYOUT
    blob = {name: lumps[index] for index, name in names.items()}
    planes_layout = "planes-first" if layout == "bshift" else "standard"

    for name, size in SIZES.items():
        if len(blob[name]) % size:
            raise BSPError(f"{name} lump is {len(blob[name])} bytes, not a multiple of {size}")

    hard: list[str] = []
    warn: list[str] = []
    distributions: dict[str, Counter] = {}

    ent_hard, ent_warn, entity_count = check_entities(blob["entities"])
    hard += ent_hard
    warn += ent_warn

    plane_hard, plane_warn, plane_types, _worst_normal = check_planes(blob["planes"])
    hard += plane_hard
    distributions["plane_types"] = plane_types

    vertex_hard, vertex_warn, _max_coord = check_vertices(blob["vertices"])
    hard += vertex_hard
    warn += vertex_warn

    tex_hard, tex_warn, tex_facts = check_textures(blob["textures"], relative)
    hard += tex_hard
    warn += tex_warn

    counts = {name: len(blob[name]) // size for name, size in SIZES.items()}
    counts["entities_bytes"] = len(blob["entities"])
    counts["entities"] = entity_count
    counts["visibility_bytes"] = len(blob["visibility"])
    counts["lighting_bytes"] = len(blob["lighting"])
    counts["textures"] = tex_facts.get("num_miptex", 0)

    faces_hard, faces_warn, styles_used, lightmap_faces, used_texinfos = check_faces(
        blob["faces"], counts["planes"], counts["texinfo"], counts["surfedges"],
        counts["lighting_bytes"])
    hard += faces_hard
    warn += faces_warn
    distributions["face_styles"] = styles_used

    texinfo_hard, texinfo_warn, texinfo_flags = check_texinfo(
        blob["texinfo"], counts["textures"], used_texinfos)
    hard += texinfo_hard
    warn += texinfo_warn
    distributions["texinfo_flags"] = texinfo_flags

    models_hard, models_warn, model_roots = check_models(
        blob["models"], counts["nodes"], counts["clipnodes"], counts["faces"])
    hard += models_hard
    warn += models_warn

    nodes_hard, nodes_warn, node_facts = check_nodes(
        blob["nodes"], counts["planes"], counts["leafs"], counts["faces"], model_roots)
    hard += nodes_hard
    warn += nodes_warn

    clip_hard, _clip_warn = check_clipnodes(blob["clipnodes"], counts["planes"])
    hard += clip_hard

    leaf_hard, leaf_warn, leaf_contents, leaf_ambient = check_leafs(
        blob["leafs"], counts["visibility_bytes"], counts["marksurfaces"])
    hard += leaf_hard
    warn += leaf_warn
    distributions["leaf_contents"] = leaf_contents
    distributions["leaf_ambient"] = Counter(
        {value.hex(): count for value, count in leaf_ambient.items()})

    hard += check_edges(blob["edges"], counts["vertices"])
    hard += check_surfedges(blob["surfedges"], counts["edges"])
    hard += check_marksurfaces(blob["marksurfaces"], counts["faces"])

    # File layout hygiene: lumps should tile the file without gaps or overlaps.
    used_ranges.sort()
    cursor = HEADER_SIZE
    padding_gaps = 0
    padding_bytes = 0
    for start, end in used_ranges:
        if start < cursor:
            warn.append(f"lump region [{start}, {end}) overlaps the previous one")
        elif start > cursor:
            gap = start - cursor
            # 1-3 byte gaps are 4-byte alignment padding: every lump is aligned
            # and the compiler pads to it. Measured on all 397 maps; larger
            # holes are unreferenced data and stay warnings.
            if gap > 3:
                warn.append(f"{gap} unreferenced bytes between lumps at {cursor}")
            else:
                padding_gaps += 1
                padding_bytes += gap
        cursor = max(cursor, end)
    if cursor != file_size:
        warn.append(f"{file_size - cursor} unreferenced bytes at end of file")

    return {
        "path": relative,
        "size": file_size,
        "layout": layout,
        "plane_order": planes_layout,
        "counts": counts,
        "embedded_textures": tex_facts.get("embedded", 0),
        "external_textures": tex_facts.get("external", 0),
        "lightmap_faces": lightmap_faces,
        "node_facts": node_facts,
        "padding_gaps": padding_gaps,
        "padding_bytes": padding_bytes,
        "distributions": {
            key: {str(value): count for value, count in counter.most_common(12)}
            for key, counter in distributions.items()
        },
        "hard_errors": hard,
        "warnings": warn,
    }


def validate(root: Path, games) -> dict:
    files: list[dict] = []
    failed: list[dict] = []

    for game in games:
        game_root = root / game.folder
        for path in sorted(game_root.rglob("*.bsp"), key=lambda item: str(item).lower()):
            relative = f"{game.folder}/{path.relative_to(game_root).as_posix()}"
            try:
                files.append(validate_map(path, relative))
            except (BSPError, OSError, struct.error) as exc:
                failed.append({"path": relative, "error": str(exc)})

    hard = sum(len(record["hard_errors"]) for record in files) + len(failed)
    warnings = sum(len(record["warnings"]) for record in files)
    layouts = Counter(record["layout"] for record in files)

    totals: Counter[str] = Counter()
    for record in files:
        for name, count in record["counts"].items():
            totals[name] += count

    merged: dict[str, Counter] = {}
    for record in files:
        for key, values in record["distributions"].items():
            merged.setdefault(key, Counter()).update(values)

    return {
        "format": "bsp30",
        "validator": "tools/verify/validate_bsp30.py",
        "corpus_root_name": root.name,
        "games": sorted({game.folder for game in games}, key=str.lower),
        "summary": {
            "files": len(files) + len(failed),
            "validated": len(files),
            "hard_errors": hard,
            "warnings": warnings,
            "layouts": dict(layouts),
            "bytes": sum(record["size"] for record in files),
            "padding_gaps": sum(record["padding_gaps"] for record in files),
            "padding_bytes": sum(record["padding_bytes"] for record in files),
            "records": dict(totals),
        },
        "distributions": {
            key: dict(counter) for key, counter in sorted(merged.items())
        },
        "failed": failed,
        "files": files,
    }


def render_report(result: dict, limit: int) -> str:
    summary = result["summary"]
    records = summary["records"]
    lines = [
        f"BSP30 corpus validation: {summary['validated']:,} of {summary['files']:,} maps",
        f"  hard errors: {summary['hard_errors']:,}",
        f"  warnings:    {summary['warnings']:,}",
        f"  layouts:     " + ", ".join(f"{k} {v:,}" for k, v in sorted(summary["layouts"].items())),
        f"  bytes:       {summary['bytes']:,}",
        "",
        "Record totals: " + ", ".join(
            f"{name} {count:,}" for name, count in sorted(records.items(), key=lambda kv: -kv[1])
            if count
        ),
    ]

    if result["failed"]:
        lines += ["", f"Unreadable maps ({len(result['failed']):,}):"]
        for record in result["failed"][:limit]:
            lines.append(f"  {record['path']} - {record['error']}")

    hard_records = [record for record in result["files"] if record["hard_errors"]]
    if hard_records:
        lines += ["", f"Maps with hard errors ({len(hard_records):,}):"]
        for record in hard_records[:limit]:
            lines.append(f"  {record['path']}: {len(record['hard_errors'])}")
            for error in record["hard_errors"][:4]:
                lines.append(f"    - {error}")

    warning_records = [record for record in result["files"] if record["warnings"]]
    if warning_records:
        lines += ["", f"Maps with warnings ({len(warning_records):,}):"]
        for record in warning_records[:limit]:
            lines.append(f"  {record['path']}: {len(record['warnings'])}")

    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate BSP30 maps in the GoldSrc corpus.")
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
