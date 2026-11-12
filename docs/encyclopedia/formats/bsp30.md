---
id: bsp30.map
status: confirmed
confidence: high
games: [hl, op4, bs, cs16, czero, czeror, dod, dmc, tfc, ricochet]
sources: [observed, sdk]
verified_by: tools/verify/validate_bsp30.py
see_also: [formats.wad3]
---

# BSP30 map

## Claim

Every retail map is a 124-byte header with a 15-entry lump table under one of
two layouts: the standard one, and the Blue Shift one that swaps lumps 0 and 1
(planes first, entities second). Every struct size is pinned by cross-referenced
bounds checks and by walking every model's hull-0 tree, which reaches all
1,702,282 nodes and all 1,106,790 leafs of the corpus. All 397 maps validate
with zero hard errors.

## Evidence

Measured on the 22 game folders installed on this machine:

| Fact | Value |
|---|---:|
| Maps | 397 (360 standard + 37 Blue Shift) |
| Total bytes | 897,882,461 (headers included) |
| Models (lump 14) | 53,765 |
| Entities (top-level blocks, lump 0) | 160,516 |
| Planes (lump 1) | 5,336,470 |
| Vertices (lump 3) | 3,790,869 |
| Nodes (lump 5) | 1,702,282 — every one reachable from a model root |
| Texinfo (lump 6) | 823,910 |
| Faces (lump 7) | 2,858,756 — 2,595,233 carry a lightmap |
| Clipnodes (lump 9) | 5,550,673 |
| Leafs (lump 10) | 1,106,790 — every one referenced by a node |
| Marksurfaces (lump 11) | 3,588,081 |
| Edges (lump 12) | 6,845,499 |
| Surfedges (lump 13) | 13,120,473 |
| SOLID leafs | exactly 1 per map (397 total) |
| Inter-lump padding gaps | 1,094 gaps of 1-3 bytes (2,189 bytes) |

### Layout

```
header        4  int32  version            (30)
(124 bytes)  60  15 x LumpEntry     (int32 offset, int32 length)
              ...   lumps follow, each 4-byte aligned (1-3 padding bytes)

standard:     0 entities   1 planes     2 textures   3 vertices
              4 visibility 5 nodes      6 texinfo    7 faces
              8 lighting   9 clipnodes 10 leafs     11 marksurfaces
             12 edges     13 surfedges 14 models

blue shift:   0 planes     1 entities   (the other thirteen lumps are identical)
```

Struct sizes (every one has a `static_assert` in
`src/parsers/goldsrc/structs/bsp30_structs.h`):

| Struct | Bytes | Fields that matter |
|---|---:|---|
| model | 64 | headnode[4] at 36: hull 0 renders, hulls 1-3 collide |
| node | 24 | planenum, children[2], mins[3], maxs[3], firstface, numfaces |
| leaf | 28 | contents, visofs, mins[3], maxs[3], marksurface run, ambient[4] |
| clipnode | 8 | planenum, children[2] (children ARE contents: -23..-1) |
| face | 20 | planenum, side, firstedge, numedges, texinfo, styles[4], lightofs |
| texinfo | 40 | s/t projection vectors, miptex index, flags (0 or 1) |
| plane | 20 | normal[3], distance, type (only 0..5 occur) |
| vertex / edge / surfedge / marksurface | 12 / 4 / 4 / 2 | |

### What the corpus corrects

- **Nodes are 24 bytes, not 36.** The plan assumed Quake's 36-byte node. With
  24 bytes, walking every model's `headnode[0]` tree visits 100% of nodes and
  100% of leafs on every map; with 28, 32 or 36 the walk breaks apart. The
  children are Quake-style: `child < 0` encodes leaf `-1 - child`.
- **Blue Shift swaps lumps 0 and 1.** All 37 `bshift` maps store planes first.
  Detection is by content: the entities lump is the one that starts with `{`.
  Nothing else differs.
- **Clipnode children are content types, and `numclipnodes` is a sentinel.**
  Hull roots equal to the clipnode count mean "no collision for this hull"
  (138 hulls across the Condition Zero maps, e.g. `de_aztec_cz` model 106).
- **Leaf contents reach below -16.** dod maps use -17 (CURRENT_90 water
  currents, 113 leafs); the validator accepts -23..-1. Observed: -1 EMPTY
  (1,055,328), -6 (42,083), -3 (8,869), -2 SOLID (397), -17 (113).
- **There is exactly one SOLID leaf per map.** 397 maps, 397 SOLID leafs; every
  node path ends there, which is why leafs (1.1M) are far fewer than nodes
  (1.7M). EMPTY leafs may ship with inverted boxes (271 of them) — the
  unset-box convention; all are referenced by nodes.
- **Leaf ambient bytes are not always zero.** 101,792 leafs carry 255 in the
  first or second channel — only in maps built with the older compiler (valve,
  tfc, gearbox, dmc, bshift, cstrike, ricochet). czero, czeror and dod maps are
  all zero.
- **Texinfo vector norms have no upper bound.** Referenced texinfos measure
  0.01 (`ikgrndj` ground) up to 256 (czero `lite1_off`/`wood_misc_plnk1`);
  unreferenced dead records carry anything (66666.664 on `valve/c1a0b.bsp`
  texinfo 1292). Zero-norm texinfos exist only as dead records.
- **Entity braces are quote-aware.** Masked texture names like `{hrpoint`
  inside infodecal values make naive brace counting fail on maps such as
  `cs_office.bsp` (247 `{` vs 245 `}`, all balanced once quotes are respected).
- **A map can ship without lighting but with lightmap offsets.**
  `cz_worldmap.bsp` has an empty lighting lump and 18 faces with `lightofs 0`;
  a reader must treat them as fullbright, not index with the offsets.

## How to check

```powershell
python tools/verify/validate_bsp30.py --json data/validation/bsp30.json
python tools/verify/validate_corpus.py --all
```

The first exits 1 on any hard error and writes per-map results, including the
tree-walk facts and value distributions. The second is the Stage 1 gate; it
reports `GATE PARTIAL` while the small formats have no validator.

## Open questions

- Why do Condition Zero maps emit the empty-hull sentinel (`headnode == numclipnodes`)
  for some func_* models while identical models elsewhere get real clipnode
  trees? Does the engine treat `==` and `>` differently?
- What are the four ambient channels semantically? The SDK suggests per-ambient-
  sound levels; the corpus only ever uses the first two, at 0 or 255.
- The visibility lump is checked only through leaf `visofs` bounds (35.3 MB) and
  lighting only through face offsets (333.4 MB); their internal formats are
  Stage 1.5 work.
- Plane types 6-8 are never observed. Do later compilers emit them, and what
  would they mean?
