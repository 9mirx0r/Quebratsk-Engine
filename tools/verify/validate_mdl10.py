#!/usr/bin/env python3
"""Validate every StudioMDL file in the retail GoldSrc corpus.

A .mdl is one of three things, told apart by the magic at offset 0:

    IDST  a model: skeleton, sequences, meshes, and usually its textures
    IDST  a texture container: a companion "<name>T.mdl" (written in lower case
          on disk, resolved case-insensitively) that a texture-less model
          "<name>.mdl" loads instead of carrying its own pixels -- all 204 of
          them, every one with zero bones, zero sequences, zero bodyparts
    IDSQ  a sequence sidecar: the "<name>NN.mdl" file a sequence group points
          at, holding that group's bone tracks (382, ten of them empty stubs)

The layout below was measured against all 3,312 files, not remembered. The
header is 244 bytes for v10 (236 for the one v9 file): magic@0, version@4,
name[64]@8, length@72 -- which equals the file size in every file -- then
thirteen (count, index) table pairs ending at 244. Record strides:

    bone 112 (name[32], parent@32, parents-first), controller 24, hitbox 32,
    sequence 176 (num_blends@120, anim_index@124, seq_group@156),
    seq group 104 (label[32], name[64]), texture 80 (name[64], flags@64,
    w@68, h@72, index@76), bodypart 76 (num_models@64, base@68,
    model_index@72), submodel 112 (type@64, num_mesh@72, mesh_index@76,
    num_verts@80, vert_info@84, verts@88, num_norms@92, norm_info@96,
    norms@100), mesh 20 (num_tris, tri_index, skin_ref, num_norms,
    norm_index), attachment 88 (type@32, bone@36), event 76, anim 12,
    anim value 2.

Conventions the corpus settles:

    - Triangle commands count VERTICES: a mesh's triangles are
      sum(|count| - 2) over its commands, and it matches mesh.num_tris
      exactly in all 10,863 meshes. Every stream terminates with a 0.
    - Vertex and normal references in the stream are absolute within their
      SUBMODEL's arrays, not the mesh's slices: per-mesh num_norms sums to
      the submodel's count in 5,021/5,021 submodels, but references exceed
      the mesh slice (they never exceed the submodel's array).
    - A model without embedded textures (num_textures == 0) resolves against
      its companion <name>T.mdl, whose skin table always has one row of
      num_textures entries. The companion is the texture authority; the
      model's own skin table, when it has one, maps into the companion's
      texture table.
    - texturedataindex is align4(texture table end + skin table): the first
      pixel byte, in all 2,731 textured files.
    - transitionindex is simply the offset after the sequence group table;
      only the four tentacle models put transitions there.
    - A sequence group's sidecar must begin with IDSQ; its offsets count
      from the sidecar's own start, header included.

Hard errors are defects a reader must reject: out-of-bounds tables and
cross-references, unterminated triangle streams, tracks that leave the file
holding them. Warnings are facts the engine tolerates: the three models with
no texture authority at all, fps of zero (clamped to 30), inverted hitboxes,
the v9 file, empty sidecars. Exit code is 1 when any hard error exists.

Usage:
    python tools/verify/validate_mdl10.py
    python tools/verify/validate_mdl10.py --json data/validation/mdl10.json
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

HEADER_SIZE = 244          # IDST v10; the v9 header stops at 236
SEQ_HEADER_SIZE = 76       # IDSQ

BONE = 112
CONTROLLER = 24
HITBOX = 32
SEQDESC = 176
SEQGROUP = 104
TEXTURE = 80
BODYPART = 76
SUBMODEL = 112
MESH = 20
ATTACHMENT = 88
EVENT = 76
ANIM = 12

PALETTE_BYTES = 768
MAX_TEXTURE_DIM = 4096
MAX_FRAMES = 4096          # the parser refuses to animate past this


class MDLError(Exception):
    """A hard defect: the file cannot be read as a StudioMDL."""


def _text(raw: bytes, offset: int, size: int) -> str:
    return raw[offset:offset + size].split(b"\x00", 1)[0].decode("latin-1")


def _u32(raw: bytes, offset: int) -> int:
    return struct.unpack_from("<i", raw, offset)[0]


def walk_tri_stream(data: bytes, pos: int, limit: int, num_verts: int,
                    num_norms: int) -> tuple[bool, str, int, int, int | None, int | None]:
    """Walk a mesh's triangle command stream.

    Each command is an int16 count followed by |count| records of four int16
    (vertex, normal, s, t). Negative counts are fans, positive strips, and a
    count of zero terminates. Commands count vertices, so a mesh's triangles
    are sum(|count| - 2) -- which equals mesh.num_tris in every mesh measured.

    Returns (ok, reason, triangles, records, first_bad_vertex, first_bad_norm).
    """
    triangles = 0
    records = 0
    bad_vert: int | None = None
    bad_norm: int | None = None
    while True:
        if pos + 2 > limit:
            return False, "unterminated", triangles, records, bad_vert, bad_norm
        count, = struct.unpack_from("<h", data, pos)
        pos += 2
        if count == 0:
            return True, "ok", triangles, records, bad_vert, bad_norm
        run = abs(count)
        if pos + run * 8 > limit:
            return False, "records_overrun", triangles, records, bad_vert, bad_norm
        for i in range(run):
            vertex, normal, _s, _t = struct.unpack_from("<4h", data, pos + i * 8)
            if bad_vert is None and not 0 <= vertex < num_verts:
                bad_vert = vertex
            if bad_norm is None and not 0 <= normal < num_norms:
                bad_norm = normal
        records += run
        triangles += max(run - 2, 0)
        pos += run * 8


def walk_anim_channel(data: bytes, pos: int, num_frames: int) -> tuple[str, int]:
    """Walk one RLE channel far enough to cover every frame of its sequence.

    A run is a {valid, total} header plus `valid` samples; `total` frames are
    covered, frames past the samples repeating the last one. A zero total ends
    the walk without covering anything more (the parser then holds the bone's
    rest value from that frame on).

    Returns (status, covered_frames): ok, static_end (a zero total before the
    sequence's last frame), sample_overrun (the run holding the last frame
    extends past the end of data), or unterminated.
    """
    covered = 0
    while covered < num_frames:
        if pos + 2 > len(data):
            return "unterminated", covered
        valid, total = data[pos], data[pos + 1]
        if total == 0:
            return "static_end", covered
        end = pos + (valid + 1) * 2
        covered += total
        pos = end
        if covered >= num_frames:
            return ("ok" if end <= len(data) else "sample_overrun"), covered
    return "ok", covered


def load_sidecar_factory():
    """Sidecars are read repeatedly (one per referencing sequence); cache them."""
    cache: dict[Path, bytes | None] = {}

    def load(game_root: Path, name: str) -> bytes | None:
        path = game_root / name.replace("\\", "/")
        path = path.resolve()
        if path not in cache:
            cache[path] = path.read_bytes() if path.is_file() else None
        return cache[path]

    return load


def validate_seq_sidecar(raw: bytes, relative: str) -> tuple[dict, list[str], list[str]]:
    """Standalone checks for an IDSQ file. Its contents are only interpretable
    through the model that references it, so this is header work only."""
    hard: list[str] = []
    warn: list[str] = []
    version, = struct.unpack_from("<i", raw, 4)
    length, = struct.unpack_from("<i", raw, 72)
    if version != 10:
        hard.append(f"version {version}, expected 10")
    if length != len(raw):
        hard.append(f"declared length {length:,} != file size {len(raw):,}")
    stub = len(raw) == SEQ_HEADER_SIZE
    if stub:
        warn.append("empty sidecar: header only, no bone tracks")
    return {
        "path": relative,
        "size": len(raw),
        "kind": "seq-sidecar",
        "counts": {},
        "stub": stub,
    }, hard, warn


def validate_model(raw: bytes, relative: str, game: str, game_root: Path,
                   load_sidecar, referenced: set[tuple[str, str]]
                   ) -> tuple[dict, list[str], list[str]]:
    """Full checks for an IDST model (or texture container)."""
    hard: list[str] = []
    warn: list[str] = []
    counts: Counter[str] = Counter()
    distributions: dict[str, Counter] = {}

    version, = struct.unpack_from("<i", raw, 4)
    length, = struct.unpack_from("<i", raw, 72)
    name = _text(raw, 8, 64)

    if version == 9:
        # Measured on valve/models/doctor.mdl: same header shape (length still
        # exact) but bones without their controller channel array (88 bytes,
        # hitbox_index == 236 + num_bones * 88). Not worth a second reader.
        warn.append("version 9 model; not parsed beyond the header "
                    f"(name {name!r}, {len(raw):,} bytes)")
        return {
            "path": relative,
            "size": len(raw),
            "kind": "v9-model",
            "counts": {},
        }, hard, warn, {}
    if version != 10:
        raise MDLError(f"version {version}, expected 10")

    nb, bi = _u32(raw, 140), _u32(raw, 144)
    nbc, bci = _u32(raw, 148), _u32(raw, 152)
    nhb, hbi = _u32(raw, 156), _u32(raw, 160)
    ns, si_ = _u32(raw, 164), _u32(raw, 168)
    nsg, sgi = _u32(raw, 172), _u32(raw, 176)
    nt, ti, tdi = _u32(raw, 180), _u32(raw, 184), _u32(raw, 188)
    nsr, nsf, ski = _u32(raw, 192), _u32(raw, 196), _u32(raw, 200)
    nbp, bpi = _u32(raw, 204), _u32(raw, 208)
    na, ai_ = _u32(raw, 212), _u32(raw, 216)
    sound_fields = struct.unpack_from("<4i", raw, 220)
    ntrans, transidx = _u32(raw, 236), _u32(raw, 240)

    container = nb == 0 and ns == 0 and nbp == 0
    kind = "texture-container" if container else "model"

    counts.update(bones=nb, bone_controllers=nbc, hitboxes=nhb, sequences=ns,
                  seq_groups=nsg, textures=nt, skin_refs=nsr * nsf,
                  bodyparts=nbp, attachments=na)

    # ------------------------------------------------ table bounds (hard) ----
    tables = (
        ("bones", nb, bi, BONE), ("bone_controllers", nbc, bci, CONTROLLER),
        ("hitboxes", nhb, hbi, HITBOX), ("sequences", ns, si_, SEQDESC),
        ("seq_groups", nsg, sgi, SEQGROUP), ("textures", nt, ti, TEXTURE),
        ("bodyparts", nbp, bpi, BODYPART), ("attachments", na, ai_, ATTACHMENT),
    )
    for table, count, index, size in tables:
        if count < 0 or index < 0 or index + count * size > length:
            hard.append(f"{table} [{index}, {index + count * size}) is out of bounds")
    if nsr < 0 or nsf < 0 or ski < 0 or ski + nsr * nsf * 2 > length:
        hard.append(f"skin table [{ski}, {ski + nsr * nsf * 2}) is out of bounds")

    # ------------------------------------------------------------ bones ----
    for i in range(nb):
        parent, = struct.unpack_from("<i", raw, bi + i * BONE + 32)
        if not -1 <= parent < nb:
            hard.append(f"bone {i}: parent {parent} outside [-1, {nb})")
        elif parent >= i:
            # The parser walks bones in order and asks each parent for its
            # already-built rest transform; ordering is load-bearing.
            warn.append(f"bone {i}: parent {parent} is not declared before it")

    # --------------------------------- controllers, hitboxes, attachments ----
    controller_types: Counter[int] = Counter()
    for i in range(nbc):
        bone, ctype, _start, _end = struct.unpack_from("<2i2f", raw, bci + i * CONTROLLER)
        controller_types[ctype] += 1
        if not 0 <= bone < nb:
            hard.append(f"bone controller {i}: bone {bone} outside [0, {nb})")
    distributions["controller_types"] = controller_types

    hitbox_groups: Counter[int] = Counter()
    for i in range(nhb):
        bone, group = struct.unpack_from("<2i", raw, hbi + i * HITBOX)
        bbmin = struct.unpack_from("<3f", raw, hbi + i * HITBOX + 8)
        bbmax = struct.unpack_from("<3f", raw, hbi + i * HITBOX + 20)
        hitbox_groups[group] += 1
        if not 0 <= bone < nb:
            hard.append(f"hitbox {i}: bone {bone} outside [0, {nb})")
        if group < 0:
            warn.append(f"hitbox {i}: group {group}")
        if any(lo > hi for lo, hi in zip(bbmin, bbmax)):
            warn.append(f"hitbox {i}: inverted box")
    distributions["hitbox_groups"] = hitbox_groups

    attachment_types: Counter[int] = Counter()
    for i in range(na):
        atype, abone = struct.unpack_from("<2i", raw, ai_ + i * ATTACHMENT + 32)
        attachment_types[atype] += 1
        if not 0 <= abone < nb:
            hard.append(f"attachment {i}: bone {abone} outside [0, {nb})")
    distributions["attachment_types"] = attachment_types

    # ---------------------------------------------------------- textures ----
    texture_flags: Counter[int] = Counter()
    texture_dims: Counter[tuple[int, int]] = Counter()

    def check_texture_table(source: bytes, table_at: int, count: int, tag: str,
                            count_stats: bool = True) -> None:
        limit = len(source)
        if table_at < 0 or (count and table_at + count * TEXTURE > limit):
            hard.append(f"{tag} texture table [{table_at}, {table_at + count * TEXTURE}) "
                        f"is out of bounds")
            return
        for i in range(count):
            flags, width, height, index = struct.unpack_from(
                "<4i", source, table_at + i * TEXTURE + 64)
            if count_stats:
                texture_flags[flags] += 1
            if not 1 <= width <= MAX_TEXTURE_DIM or not 1 <= height <= MAX_TEXTURE_DIM:
                hard.append(f"{tag} texture {i}: dimensions {width}x{height}")
                continue
            if count_stats:
                texture_dims[(width, height)] += 1
            end = index + width * height + PALETTE_BYTES
            if index < 0 or end > limit:
                hard.append(f"{tag} texture {i}: pixels [{index}, {end}) are out of bounds")

    tex_limit: int | None
    if nt == 0:
        # Texture authority lives in the companion <name>T.mdl: the model's own
        # skin table, when present, maps into the companion's texture table.
        in_game = relative.split("/", 1)[1]
        companion_name = in_game[:-4] + "T.mdl"
        companion_bytes = load_sidecar(game_root, companion_name)
        if companion_bytes is None:
            warn.append("no embedded textures and no <name>T.mdl companion; "
                        "meshes have no texture authority")
            tex_limit = None
        elif len(companion_bytes) < HEADER_SIZE or companion_bytes[0:4] != b"IDST":
            warn.append(f"companion {companion_name} is not an IDST model")
            tex_limit = None
        else:
            c_length, = struct.unpack_from("<i", companion_bytes, 72)
            if c_length != len(companion_bytes):
                hard.append(f"companion {companion_name}: declared length {c_length:,} != "
                            f"file size {len(companion_bytes):,}")
            comp_nt = _u32(companion_bytes, 180)
            comp_nsr = _u32(companion_bytes, 192)
            comp_nsf = _u32(companion_bytes, 196)
            comp_ski = _u32(companion_bytes, 200)
            if nb == 0 and ns == 0 and nbp == 0:
                warn.append("texture container also declares bones, sequences "
                            "or bodyparts")
            check_texture_table(companion_bytes, _u32(companion_bytes, 184), comp_nt,
                                "companion", count_stats=False)
            if comp_nsr > 0 and comp_nsf > 0:
                if comp_ski < 0 or comp_ski + comp_nsr * comp_nsf * 2 > len(companion_bytes):
                    hard.append(f"companion skin table [{comp_ski}, "
                                f"{comp_ski + comp_nsr * comp_nsf * 2}) is out of bounds")
                else:
                    refs = struct.unpack_from(f"<{comp_nsr * comp_nsf}h",
                                              companion_bytes, comp_ski)
                    bad = [r for r in refs if not -1 <= r < comp_nt]
                    if bad:
                        hard.append(f"companion skin refs outside [-1, {comp_nt})")
            tex_limit = comp_nt
    else:
        check_texture_table(raw, ti, nt, "embedded")
        tex_limit = nt
        if not 0 <= tdi <= length:
            warn.append(f"texturedataindex {tdi} is out of bounds")
        else:
            expected = (ti + nt * TEXTURE + nsr * nsf * 2 + 3) & ~3
            if tdi != expected:
                warn.append(f"texturedataindex {tdi}, expected the aligned pixel "
                            f"start {expected}")

    distributions["texture_flags"] = texture_flags
    distributions["texture_dims"] = texture_dims

    # ------------------------------------------------------------- skins ----
    if nsr > 0 and nsf > 0 and ski >= 0 and ski + nsr * nsf * 2 <= length \
            and tex_limit is not None:
        refs = struct.unpack_from(f"<{nsr * nsf}h", raw, ski)
        bad = [r for r in refs if not -1 <= r < tex_limit]
        if bad:
            hard.append(f"skin refs outside [-1, {tex_limit})")
    # With a skin table the mesh's skin_ref is a column into it; without one it
    # indexes the texture table directly (the companion's, when merged).
    skinref_limit = nsr if nsr > 0 else (tex_limit or 0)

    # --------------------------------- bodyparts, submodels, meshes, tris ----
    mesh_norm_sum_warn = 0
    for b in range(nbp):
        num_models, _base, model_index = struct.unpack_from("<3i", raw, bpi + b * BODYPART + 64)
        if num_models < 0 or model_index < 0 or model_index + num_models * SUBMODEL > length:
            hard.append(f"bodypart {b}: models [{model_index}, "
                        f"{model_index + num_models * SUBMODEL}) are out of bounds")
            continue
        counts["submodels"] += num_models
        for m in range(num_models):
            mb = model_index + m * SUBMODEL
            _type, _radius, num_mesh, mesh_index, num_verts, vert_info, vert_at, \
                num_norms, norm_info, norm_at = struct.unpack_from("<if8i", raw, mb + 64)
            counts["meshes"] += num_mesh
            counts["verts"] += num_verts
            counts["norms"] += num_norms
            if num_verts < 0 or vert_at < 0 or vert_at + num_verts * 12 > length:
                hard.append(f"bodypart {b} submodel {m}: vertices [{vert_at}, "
                            f"{vert_at + num_verts * 12}) are out of bounds")
            if num_norms < 0 or norm_at < 0 or norm_at + num_norms * 12 > length:
                hard.append(f"bodypart {b} submodel {m}: normals [{norm_at}, "
                            f"{norm_at + num_norms * 12}) are out of bounds")
            if num_mesh < 0 or mesh_index < 0 or mesh_index + num_mesh * MESH > length:
                hard.append(f"bodypart {b} submodel {m}: meshes [{mesh_index}, "
                            f"{mesh_index + num_mesh * MESH}) are out of bounds")
                continue
            if num_verts:
                if vert_info < 0 or vert_info + num_verts > length:
                    hard.append(f"bodypart {b} submodel {m}: vertinfo [{vert_info}, "
                                f"{vert_info + num_verts}) is out of bounds")
                else:
                    worst = max(raw[vert_info:vert_info + num_verts])
                    if worst >= nb:
                        hard.append(f"bodypart {b} submodel {m}: vertinfo references "
                                    f"bone {worst} outside [0, {nb})")
            if num_norms:
                if norm_info < 0 or norm_info + num_norms > length:
                    hard.append(f"bodypart {b} submodel {m}: norminfo [{norm_info}, "
                                f"{norm_info + num_norms}) is out of bounds")
                else:
                    worst = max(raw[norm_info:norm_info + num_norms])
                    if worst >= nb:
                        hard.append(f"bodypart {b} submodel {m}: norminfo references "
                                    f"bone {worst} outside [0, {nb})")

            mesh_norm_sum = 0
            for k in range(num_mesh):
                num_tris, tri_at, skin_ref, mesh_norms, _mesh_norms_at = \
                    struct.unpack_from("<5i", raw, mesh_index + k * MESH)
                mesh_norm_sum += mesh_norms
                if not 0 <= skin_ref < skinref_limit:
                    # The engine falls back to an untextured surface.
                    warn.append(f"bodypart {b} submodel {m} mesh {k}: skin_ref "
                                f"{skin_ref} outside [0, {skinref_limit})")
                if tri_at <= 0:
                    if num_tris > 0:
                        warn.append(f"bodypart {b} submodel {m} mesh {k}: no triangle "
                                    f"stream but num_tris {num_tris}")
                    continue
                ok, reason, triangles, _records, bad_vert, bad_norm = walk_tri_stream(
                    raw, tri_at, length, num_verts, num_norms)
                if not ok:
                    hard.append(f"bodypart {b} submodel {m} mesh {k}: triangle stream "
                                f"{reason} after {triangles} triangles")
                elif triangles != num_tris:
                    warn.append(f"bodypart {b} submodel {m} mesh {k}: stream emits "
                                f"{triangles} triangles, num_tris says {num_tris}")
                if bad_vert is not None:
                    hard.append(f"bodypart {b} submodel {m} mesh {k}: triangle references "
                                f"vertex {bad_vert} outside [0, {num_verts})")
                if bad_norm is not None:
                    hard.append(f"bodypart {b} submodel {m} mesh {k}: triangle references "
                                f"normal {bad_norm} outside [0, {num_norms})")
            if num_norms and mesh_norm_sum != num_norms:
                warn.append(f"bodypart {b} submodel {m}: mesh normal slices sum to "
                            f"{mesh_norm_sum}, submodel has {num_norms}")
                mesh_norm_sum_warn += 1

    # ---------------------------------------------------------- sequences ----
    blends_seen: Counter[int] = Counter()
    event_ids: Counter[int] = Counter()
    group0_labels: Counter[str] = Counter()

    # Group 0 is the model itself; groups 1..nsg-1 are sidecars.
    sidecars: dict[int, bytes] = {}
    for g in range(nsg):
        label = _text(raw, sgi + g * SEQGROUP, 32)
        gname = _text(raw, sgi + g * SEQGROUP + 32, 64)
        if g == 0:
            group0_labels[label] += 1
            if label != "default" or gname:
                warn.append(f"seq group 0 is labelled {label!r} named {gname!r}; "
                            "the corpus writes 'default' and an empty path")
        else:
            referenced.add((game, Path(gname.replace("\\", "/")).name.lower()))
            data = load_sidecar(game_root, gname)
            if data is None:
                warn.append(f"seq group {g} points at {gname!r}, which does not exist")
            elif data[0:4] != b"IDSQ":
                warn.append(f"seq group {g} points at {gname!r}, which is not a "
                            "sequence sidecar")
            else:
                sidecars[g] = data

    for s in range(ns):
        soff = si_ + s * SEQDESC
        fps, = struct.unpack_from("<f", raw, soff + 32)
        num_frames, = struct.unpack_from("<i", raw, soff + 56)
        blends, anim_at = struct.unpack_from("<2i", raw, soff + 120)
        seq_group, = struct.unpack_from("<i", raw, soff + 156)
        blends_seen[blends] += 1
        counts["frames"] += num_frames
        if fps <= 0:
            warn.append(f"sequence {s}: fps {fps:.3g}; the engine plays it at 30")
        if num_frames > MAX_FRAMES:
            warn.append(f"sequence {s}: {num_frames} frames; the engine animates "
                        f"at most {MAX_FRAMES}")
        if num_frames < 0:
            warn.append(f"sequence {s}: negative frame count {num_frames}")
        if not 0 <= seq_group < max(nsg, 1):
            hard.append(f"sequence {s}: seq_group {seq_group} outside [0, {max(nsg, 1)})")
            continue

        num_events, event_at = struct.unpack_from("<2i", raw, soff + 48)
        if num_events:
            if event_at <= 0 or event_at + num_events * EVENT > length:
                hard.append(f"sequence {s}: events [{event_at}, "
                            f"{event_at + num_events * EVENT}) are out of bounds")
            else:
                counts["events"] += num_events
                for e in range(num_events):
                    frame, eid = struct.unpack_from("<2i", raw, event_at + e * EVENT)
                    event_ids[eid] += 1
                    if not 0 <= frame < max(num_frames, 1):
                        warn.append(f"sequence {s} event {e}: frame {frame} outside "
                                    f"[0, {num_frames})")

        if seq_group == 0:
            holder = raw
        else:
            holder = sidecars.get(seq_group)
            if holder is None:
                continue  # already reported as missing
        if anim_at == 0:
            warn.append(f"sequence {s}: no bone tracks (anim_index 0)")
            continue
        if anim_at < 0:
            hard.append(f"sequence {s}: negative anim_index {anim_at}")
            continue
        blob_end = anim_at + blends * nb * ANIM
        if anim_at < 0 or blob_end > len(holder):
            hard.append(f"sequence {s}: anim table [{anim_at}, {blob_end}) leaves the "
                        f"file that holds it ({len(holder):,} bytes)")
            continue
        statuses: Counter[str] = Counter()
        for blend in range(blends):
            for bone in range(nb):
                here = anim_at + (blend * nb + bone) * ANIM
                offsets = struct.unpack_from("<6H", holder, here)
                for channel, offset in enumerate(offsets):
                    if offset == 0:
                        continue
                    status, _covered = walk_anim_channel(holder, here + offset, num_frames)
                    if status != "ok":
                        statuses[status] += 1
        for status, hits in statuses.items():
            text = f"sequence {s}: {hits} channel(s) {status}"
            if status == "unterminated":
                hard.append(text)
            else:
                warn.append(text + ("; the engine holds the rest value from there"
                                    if status == "static_end" else
                                    "; the engine reads its last sample clipped"))

    distributions["blends"] = blends_seen
    distributions["event_ids"] = event_ids
    if container and ns > 0:
        warn.append("texture container declares sequences")

    # ------------------------------------------------ sound and transitions ----
    # Measured: all four sound fields are zero everywhere; the transition table
    # sits right after the seq group table and only the tentacles use it.
    for offset, value in zip((220, 224, 228, 232), sound_fields):
        if value:
            warn.append(f"header sound field at {offset} is {value}; always zero in "
                        "the corpus")
    if ntrans:
        if transidx < 0 or transidx + ntrans > length:
            hard.append(f"transition table [{transidx}, {transidx + ntrans}) is out "
                        "of bounds")
        else:
            warn.append(f"{ntrans} transitions at {transidx}")
    if nsg > 0 and transidx != sgi + nsg * SEQGROUP:
        warn.append(f"transitionindex {transidx}, expected the offset after the seq "
                    f"group table ({sgi + nsg * SEQGROUP})")
    elif nsg == 0 and transidx != 0:
        warn.append(f"transitionindex {transidx} with no sequence groups")

    return {
        "path": relative,
        "size": len(raw),
        "kind": kind,
        "counts": dict(counts),
        "mesh_norm_slice_mismatch": mesh_norm_sum_warn,
    }, hard, warn, distributions


def validate(root: Path, games) -> dict:
    files: list[dict] = []
    failed: list[dict] = []
    referenced: set[tuple[str, str]] = set()
    existing_sidecars: set[tuple[str, str]] = set()
    load_sidecar = load_sidecar_factory()
    merged: dict[str, Counter] = {}
    warnings_count = 0

    for game in games:
        game_root = root / game.folder
        for path in sorted(game_root.rglob("*.mdl"), key=lambda item: str(item).lower()):
            relative = f"{game.folder}/{path.relative_to(game_root).as_posix()}"
            game_name = game.folder
            try:
                raw = path.read_bytes()
                if len(raw) < SEQ_HEADER_SIZE:
                    raise MDLError(f"file is {len(raw)} bytes, a header needs {SEQ_HEADER_SIZE}")
                magic = raw[0:4]
                if magic == b"IDSQ":
                    existing_sidecars.add((game_name, path.name.lower()))
                    record, hard, warn = validate_seq_sidecar(raw, relative)
                    distributions = {}
                elif magic == b"IDST":
                    record, hard, warn, distributions = validate_model(
                        raw, relative, game_name, game_root, load_sidecar, referenced)
                else:
                    raise MDLError(f"magic {magic.decode('latin-1', 'replace')!r}; "
                                   "not a StudioMDL")
                record["hard_errors"] = hard
                record["warnings"] = warn
                warnings_count += len(warn)
                files.append(record)
                for key, counter in distributions.items():
                    merged.setdefault(key, Counter()).update(counter)
            except (MDLError, OSError, struct.error) as exc:
                failed.append({"path": relative, "error": str(exc)})

    orphans = sorted(existing_sidecars - referenced)
    warnings_count += len(orphans)
    hard = sum(len(record["hard_errors"]) for record in files) + len(failed)

    totals: Counter[str] = Counter()
    for record in files:
        for field, count in record["counts"].items():
            totals[field] += count

    kinds = Counter(record["kind"] for record in files)
    stubs = sum(1 for record in files if record.get("stub"))

    return {
        "format": "mdl10",
        "validator": "tools/verify/validate_mdl10.py",
        "corpus_root_name": root.name,
        "games": sorted({game.folder for game in games}, key=str.lower),
        "summary": {
            "files": len(files) + len(failed),
            "validated": len(files),
            "hard_errors": hard,
            "warnings": warnings_count,
            "kinds": dict(kinds),
            "seq_sidecar_stubs": stubs,
            "orphan_sidecars": len(orphans),
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
        "orphan_sidecars": [f"{game}/{name}" for game, name in orphans],
        "failed": failed,
        "files": files,
    }


def render_report(result: dict, limit: int) -> str:
    summary = result["summary"]
    records = summary["records"]
    lines = [
        f"MDL10 corpus validation: {summary['validated']:,} of {summary['files']:,} files",
        f"  hard errors: {summary['hard_errors']:,}",
        f"  warnings:    {summary['warnings']:,}",
        "  kinds:       " + ", ".join(
            f"{kind} {count:,}" for kind, count in sorted(summary["kinds"].items())),
        f"  bytes:       {summary['bytes']:,}",
        "",
        "Record totals: " + ", ".join(
            f"{name} {count:,}" for name, count in sorted(records.items(), key=lambda kv: -kv[1])
            if count
        ),
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

    if result["orphan_sidecars"]:
        lines += ["", f"Orphan sidecars ({len(result['orphan_sidecars']):,}):"]
        for name in result["orphan_sidecars"][:limit]:
            lines.append(f"  {name}")

    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate StudioMDL files in the GoldSrc corpus.")
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
