---
id: mdl10.model
status: confirmed
confidence: high
games: [hl, op4, bs, cs16, czero, czeror, dod, dmc, tfc, ricochet]
sources: [observed, sdk]
verified_by: tools/verify/validate_mdl10.py
---

# StudioMDL v10 model

## Claim

Every retail `.mdl` is one of three things, told apart by how it is used: a
model (244-byte `IDST` v10 header, then thirteen count/index table pairs), a
texture container (`<name>T.mdl`, zero bones/sequences/bodyparts, textures
only), or a sequence sidecar (`<name>NN.mdl`, `IDSQ` magic, bone tracks for one
sequence group). Every stride is pinned by cross-referenced bounds and full
walks: all 10,863 triangle streams terminate, their vertex commands reproduce
`num_tris` exactly, every animation track stays inside the file that holds it,
and per-mesh normal slices tile all 5,021 submodels. All 3,312 files validate
with zero hard errors.

## Evidence

Measured on the 22 game folders installed on this machine (15 of them ship
models):

| Fact | Value |
|---|---:|
| `.mdl` files | 3,312 |
| Kinds | 2,725 models, 204 texture containers, 382 sequence sidecars, 1 v9 file |
| Total bytes | 741,152,868 |
| By game | czeror 1,074, dod 356, bshift 353, czero 324, valve 323, gearbox 194, cstrike 179, bshift_hd 117, valve_hd 104, tfc 100, gearbox_hd 89, dmc 71, ricochet 14, cstrike_hd 11, dmc_spanish 3 |
| Bones | 37,846 — every parent declared before its child |
| Sequences | 39,640 — 2,171,095 frames, longest 512 |
| Seq groups | 3,105 |
| Bodyparts / submodels | 3,221 / 5,263 |
| Meshes | 10,863 |
| Vertices / normals | 874,844 / 794,908 |
| Hitboxes / controllers / attachments | 16,978 / 631 / 1,108 |
| Events | 21,728 |
| Textures | 9,242 (1,304 distinct dimension pairs) |
| Skin table entries | 10,753 |

### Layout

```
header        4  magic   "IDST" (models, containers) or "IDSQ" (sidecars)
(244 bytes)   4  int32   version (10; one file is 9)
             64  char    name
              4  int32   length -- equals the file size in all 3,312 files
             64  bounds, eye position, flags
            100  13 x (int32 count, int32 index) table pairs:
                   bones@140, controllers@148, hitboxes@156, seqs@164,
                   seq groups@172, textures@180 (+texturedata@188),
                   skins@192 (num_ref, num_family, index), bodyparts@204,
                   attachments@212, sound tables@220..232, transitions@236
```

Struct strides (every one has a `static_assert` in
`src/parsers/goldsrc/structs/mdl10_structs.h`):

| Struct | Bytes | Fields that matter |
|---|---:|---|
| bone | 112 | name[32], parent@32 (parents-first), 6 controller slots, value[6], scale[6] |
| bone controller | 24 | bone, type, start, end, rest@16, index |
| hitbox | 32 | bone, group, bbmin@8, bbmax@20 |
| sequence | 176 | fps@32 (float), events@48, num_frames@56, num_blends@120, anim_index@124, seq_group@156 |
| seq group | 104 | label[32], name[64] |
| texture | 80 | name[64], flags@64, w@68, h@72, pixel index@76 |
| bodypart | 76 | num_models@64, base@68, model_index@72 |
| submodel | 112 | type@64, boundingradius@68, meshes@72, verts@80..88, norms@92..100 |
| mesh | 20 | num_tris, tri_index, skin_ref, num_norms, norm_index |
| attachment | 88 | name[32], type@32, bone@36 |
| event | 76 | frame, event id, options[64] |
| anim track | 12 | six uint16 offsets (position xyz, rotation xr yr zr) |
| anim value | 2 | {valid, total} run header or int16 sample |

### What the corpus corrects

- **`length` sits at offset 72, not 76**, and equals the file size in every
  file — the only cheap integrity check the format offers.
- **Hitboxes are 32 bytes, controllers 24.** Older reconstructions carry
  68-byte hitboxes and 36-byte controllers; under those strides the tables
  overrun their own file on every model. `num_blends`/`anim_index` are at
  120/124, not 148/152. Texture names are 64 bytes, not 32.
- **Triangle commands count VERTICES.** A mesh's triangles are
  `sum(|count| - 2)` over its commands (negative counts are fans, positive
  strips, 0 terminates) — that equals `mesh.num_tris` in all 10,863 meshes.
  `num_tris` itself is advisory: the engine walks the stream and ignores it.
- **Vertex and normal references are absolute within their SUBMODEL's arrays,
  not the mesh's slices.** Per-mesh `num_norms` sums to the submodel's count
  in 5,021/5,021 submodels, but streams reference normals beyond the mesh's
  slice (never beyond the submodel's array). Bounds a reader against slices
  rejects correct models.
- **A model without embedded textures has no skin table either** (`nt == 0`
  implies `nsr == 0`), and its companion `<name>T.mdl` always carries one row
  of `num_textures` entries. The companion is the texture authority: the
  model's meshes index companion textures directly. All 204 companions are
  pure containers; 198 models have `nt == 0`, 195 of them resolve a companion,
  3 do not (see warnings).
- **`texturedataindex` is `align4(texture table end + skin table)`** in all
  2,731 textured files, the one v9 file included — the first pixel byte.
- **`transitionindex` is just the offset after the seq group table**
  (`seqgroupindex + num_seq_groups * 104`, 2,725/2,725); with no groups it is
  0. Only the four tentacle models actually store transitions there (14 each).
- **A texture container is exactly a model with nothing but textures** —
  `num_seq == 0` iff `num_bones == 0` iff container, in all 2,929 v10 IDST
  files. Seq group 0 is labelled `'default'` with an empty path in all 2,725
  models that have groups.
- **Texture dimensions carry no 16-alignment rule**: 5,641 of 9,242 textures
  are not multiples of 16, and off-by-one heights ship (256x255 215 times,
  128x127 158, 256x254 113). Widest observed is 768, tallest 574.
- **Animation tracks are RLE walks, not arrays.** A run is {valid, total} plus
  `valid` samples; `total` frames pass, past the samples repeating the last
  one. A `total` of 0 ends the channel (the engine then holds the bone's rest
  value); the final run may reach past EOF and the engine clips it.
- **The engine tolerates what the corpus ships**: fps <= 0 (12 sequences)
  plays at 30, sequences over 4,096 frames are not animated (longest here is
  512), a missing sidecar falls back to the rest pose keeping name and sounds,
  a mesh whose `skin_ref` has no texture authority draws untextured.

### Distributions

- Blends per sequence: {1: 31,344, 2: 4,284, 3: 176, 9: 3,836}. Nine-blend
  aim-blend sets are a Counter-Strike/Condition Zero trait (dod ships 600 of
  them too); three-blend sequences appear in dod (132) and ricochet (44).
- Controller types: XR 406, ZR 172, YR 38, Y 2, and MOUTH combined with
  XR/YR/ZR (1/8/4) — 32768 marks the mouth controller.
- Attachment types: 0 in all 1,108.
- Hitbox groups: 0 is generic (11,971 of 16,978); 1..11 are the HL hit zones
  (head 343, chest 699, stomach 532 ...).
- Loudest event ids: 1004 (5,174), 5001 (3,193), 2001 (2,832), 11 (2,824).
- Texture flags: {0: 8,307, 2: 1, 3: 455, 32: 9, 64: 464, 96: 6}.

### Files with warnings

101 warnings in 35 files, every one a fact the engine tolerates:

| Count | Warning |
|---:|---|
| 58 | mesh `skin_ref` outside any texture authority (czeror `null`/`player`, dmc `player` — no embedded textures, no skin table, no companion) |
| 3 | same three models: companion missing |
| 12 | sequences with fps <= 0 (11 files: shells, `door_garage`, `freya_radar1`, `street_lamp`...) |
| 10 | empty sidecars (76-byte IDSQ headers, no tracks) |
| 10 | inverted hitboxes |
| 4 | transition tables (tentacle2/tentacle3 variants) |
| 2 | orphan sidecars (`bshift/scientist11.mdl`, `gearbox/hgrunt_opfor04.mdl` — no sequence group points at them) |
| 1 | `valve/models/doctor.mdl`: v9 file |
| 1 | one channel ends early (`static_end`); the engine holds the rest pose from that frame |

The v9 file is a full model: same header shape, bones without their controller
channel array (88 bytes, `hitbox_index == 236 + num_bones * 88` == 2,524
measured), and the `texturedataindex` convention still holds. The validator
warns and stops there — one sample is not a format.

## How to check

```powershell
python tools/verify/validate_mdl10.py --json data/validation/mdl10.json
python tools/verify/validate_corpus.py --all
```

The first exits 1 on any hard error and writes per-file results, including
record totals and the blends/event/texture distributions. The second is the
Stage 1 gate; it reports `GATE PARTIAL` while SPR and the small formats have
no validator.

## Open questions

- What are the four sound-table header fields (offsets 220..232)? They are
  zero in all 3,312 files; the SDK names them but retail compilers never
  filled them.
- What do a submodel's `num_groups`/`group_index` (offsets 104/108) hold?
  Never populated in a way this validator exercises; unmeasured.
- Nine companion skin tables are not identity maps (186 of 195 are). What do
  the other nine map, and does anything reference those rows?
- Are attachment `vectors` always zero, as the struct comment assumes? Not yet
  measured.
- What do texture flags 2, 32 and 96 mean? 1, 9 and 6 occurrences respectively.
- Is v9's 88-byte bone (no controller channel array) universal for v9 files,
  or specific to this one build of `doctor.mdl`?
