---
id: spr.sprite
status: confirmed
confidence: high
games: [hl, op4, bs, cs16, czero, czeror, dod, dmc, tfc, ricochet]
sources: [observed]
verified_by: tools/verify/validate_spr.py
---

# SPR sprite

## Claim

Every retail sprite is a 40-byte header, a 768-byte palette (256 entries,
counted by a `uint16` at offset 40), and `num_frames` entries that each begin
with a 4-byte frame type and continue with a 16-byte frame header plus
`width*height` raw palette indices. The walk lands exactly on the file size in
all 1,361 sprites; the header's width/height equal the largest frame's
dimensions and its radius is the half diagonal in all 1,361. All sprites
validate with zero hard errors and zero warnings.

## Evidence

Measured on the 22 game folders installed on this machine (12 of them ship
sprites):

| Fact | Value |
|---|---:|
| Sprite files | 1,361, all `IDSP` version 2 |
| Total bytes | 65,684,466 |
| Frames (top-level entries) | 7,634 in 1,361 files |
| Frame members (after groups) | 7,634 — no group occurs |
| Pixel bytes | 64,429,376 — every frame is raw 8-bit indices |
| By game | valve 384, dod 269, dmc 189, bshift 165, czeror 161, gearbox 82, cstrike 71, tfc 18, ricochet 13, bshift_hd / gearbox_hd / valve_hd 3 each |
| Frames per sprite | 1 to 50; 921 sprites have exactly one |
| Widest / tallest | 1,024 / 384 |
| Palette count | 256 in 1,361/1,361 |
| Radius = `hypot(w, h) / 2` | 1,361/1,361 |
| Header size = largest frame | 1,361/1,361 |

### Layout

```
header        4  magic "IDSP"
(40 bytes)    4  int32   version (2)
              4  int32   type          0 parallel upright, 1 facing upright,
                                        2 parallel, 3 oriented, 4 parallel oriented
              4  int32   tex_format    0 normal, 1 additive, 2 indexalpha, 3 alphatest
              4  float   bounding radius (half diagonal of width x height)
              4  int32   width         largest frame width
              4  int32   height        largest frame height
              4  int32   num_frames
              4  float   beam length   (zero everywhere)
              4  int32   sync type     0 sync, 1 random

palette       2  uint16  count (256)
            768  rgb

frames       for each of num_frames:
              4  int32   frame type (0 single, 1 group)
             16  4 x int32 origin x, origin y, width, height
            w*h  uint8   palette indices

             a group is: 4-byte member count, then that many frames stored
             exactly like a single one
```

Structs and offsets are pinned in
`src/parsers/goldsrc/structs/spr_structs.h`.

### What the corpus corrects

- **The header is 40 bytes, not 36.** This is not Quake's `dsprite_t`: a
  `tex_format` field sits at offset 12, before the bounding radius. The palette
  count is 256 at offset 40 in every file and never at 36. Reading the header
  with the 36-byte order shifts width/height one field early (it reads the
  radius and the width), and every frame walk fails.
- **Every frame is preceded by its 4-byte type.** The first frame starts at
  810 (40 + 2 + 768), and skipping the type int32 misplaces origin and
  dimensions by four bytes. The corpus only uses type 0 (single); type 1
  (group: a member count, then that many frames) never occurs, but a reader
  must handle it.
- **There is no RLE in GoldSrc sprites.** Every frame stores `width*height`
  raw palette indices; the walk consumes the file exactly. All 1,361 files
  need no decompression step.
- **The bounding radius is exactly the half diagonal**, `hypot(width, height)
  / 2`, matching 1,361/1,361; the header dimensions always equal the largest
  frame's dimensions.
- **`palette[255]` is not a colour key.** Its RGB is arbitrary — black in 634
  sprites, white in 417, blue in 65, magenta in 26. Transparency is the
  palette index 255 itself, not the colour stored there.
- **Orientation and render modes are skewed.** Type 2 (parallel) covers 1,319
  sprites; type 4 (parallel oriented) 29, type 0 (parallel upright) 10, type 3
  (oriented) 3. Type 1 (facing upright) never appears. Render modes: additive
  945, alphatest 241, indexalpha 91, normal 84.
- **Most sprites are random-synced.** `sync_type` is 1 (random) in 1,147
  sprites and 0 (sync) in 214. `beam_length` is zero in all 1,361.

## How to check

```powershell
python tools/verify/validate_spr.py --json data/validation/spr.json
python tools/verify/validate_corpus.py --all
```

The first exits 1 on any hard error and writes per-file results, including
frame counts, pixel bytes and the type/render-mode distributions. The second
is the Stage 1 gate; it reports `GATE PARTIAL` while the small formats have no
validator.

## Open questions

- Why does orientation type 1 (facing upright) never appear? The renderer
  implements it, but no shipped sprite uses it.
- What does `sync_type` 1 (random) change for the 1,147 sprites that use it?
  Animation timing is engine behaviour, not file structure — Stage 2.
- Are grouped frames (type 1) legal input the shipped `spritegen` never
  produced? No file in the corpus uses them.
- Why is `origin_x` negative in every frame (range -512..-4)? It looks like
  the anchor is stored relative to a top-left pivot, but the exact convention
  is unmeasured.
