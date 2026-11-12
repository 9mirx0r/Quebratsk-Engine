---
id: wad3.container
status: confirmed
confidence: high
games: [hl, op4, bs, cs16, czero, czeror, dod, dmc, tfc, ricochet]
sources: [observed]
verified_by: tools/verify/validate_wad3.py
see_also: [formats.miptex, formats.qpic]
---

# WAD3 container

## Claim

Every WAD3 in the retail corpus is a 12-byte header (magic, lump count,
directory offset — there is no version field), a 32-byte-per-entry directory,
and lumps of three kinds: miptex (`0x43`) with four mip levels and a 256-entry
palette, qpic (`0x42`) with the pixels *before* the palette, and font (`0x46`).
All 126 WAD files validate with zero hard errors.

## Evidence

Measured on the 22 game folders installed on this machine:

| Fact | Value |
|---|---:|
| WAD files | 126 |
| Distinct files by SHA-256 | 120 |
| Total bytes | 591,476,100 |
| Miptex lumps | 21,021 |
| Unique miptex names | 11,710 |
| Masked textures (`{` prefix) | 2,896 |
| Animated textures (`+` prefix) | 1,123 |
| qpic lumps | 50 |
| Font lumps (`0x46`) | 15 |
| WAD2 files | 0 |
| Duplicate texture names inside one WAD | 0 |
| Dimensions not a multiple of 16 | 0 |
| Mip chains that do not match `w*h`, `w*h/4`, `w*h/16`, `w*h/64` | 0 |

WADs are not distributed evenly: `dod` ships 25, `cstrike`, `czero` and
`czeror` ship 23 each, `bshift` and `valve` ship 8, `tfc` 5, `gearbox` 4, and
`dmc` and `ricochet` 2.

### Layout

```
header      4  magic "WAD3"
            4  int32  numlumps
            4  int32  diroffset

directory   4  int32  filepos
(32 bytes)  4  int32  disksize
            4  int32  size            (uncompressed; equal to disksize here)
            1  uint8  type            (0x43 miptex, 0x42 qpic, 0x46 font)
            1  uint8  compression     (always 0)
            2  uint8  padding
           16  char   name            (NUL-terminated, not necessarily terminated)

miptex     16  char   name
(40 bytes)  4  uint32 width
            4  uint32 height
           16  uint32 offsets[4]      (relative to the lump start)
               ...  mip0, mip1, mip2, mip3
            2  uint16 palette count    (256 in every lump)
          768  uint8  palette[256][3]

qpic        4  int32  width
            4  int32  height
        w*h     uint8  palette indices
          768   uint8  palette[256][3]
            4   uint8  trailer          (purpose unknown, present in all 50)
```

### What the corpus corrects

- **`disksize` is not the texture size.** 1,137 miptex lumps declare more bytes
  than their mip chain plus palette needs. In 968 of them the surplus contains
  the next lump's name byte for byte: these are stale copies of the following
  lump's header left inside the previous lump's declared size. A reader must
  compute the end from the mip offsets, never from `disksize`. Verified on
  `cstrike/de_vertigo.wad`: `basebeamtop` carries a 40-byte copy of
  `crete33`'s header 658 bytes before `crete33` officially starts.
- **The qpic palette comes last.** Pixels sit at offset 8 for `width*height`
  bytes; the 768-byte palette follows. Decoding `bshift/gfx.wad`'s `LAMBDA`
  lump this way produces a legible image; the opposite order produces noise.
- **Dimensions are multiples of 16, not powers of two.** 112x112, 48x48,
  96x128, 144x144 and 240x240 all occur. The most common sizes are 256x256
  (3,337), 128x128 (3,077), 64x64 (1,877), 32x32 (1,073) and 16x16 (1,021).
  The largest texture in the corpus is `tl_gry_clean05`, 1024x512, in
  `czeror/fastline.wad`.
- **Directory and header names can disagree in case.** `bshift/halflife.wad`
  stores `!TOXICGRN` in the directory and `!toxicgrn` in the miptex header.
  Lookups must be case-insensitive.
- **Font lumps are not decoded.** 15 `0x46` lumps exist in 7 files; their first
  16 bytes are four int32 (for `CONCHARS`: 12, 128, 6, 12; for `CREDITSFONT`:
  19, 64, 3, 19) followed by data that is neither a plain qpic nor a miptex.
  Only their bounds are checked. See open questions.

## How to check

```powershell
python tools/verify/validate_wad3.py --json data/validation/wad3.json
python tools/verify/validate_corpus.py --all
```

The first exits 1 on any hard error and writes the per-file result, including
each WAD's texture list. The second is the Stage 1 gate: it currently reports
`GATE PARTIAL`, because the small formats have no validator yet.
When they do, `--all` is the single command that proves the gate.

## Open questions

- What are the four trailing bytes of a qpic? They are present in all 50 lumps,
  vary per image (0x00004646 for `bshift`'s `LAMBDA`, 0x0000B4C4 for
  `cstrike`'s `CONBACK`), and are neither palette nor pixel data.
- What is the `0x46` font lump structure? 15 samples, all in `gfx.wad` or
  `valve/fonts.wad`.
- Why do some map WADs contain stale header copies of the next lump? It looks
  like a builder that rewrote a lump without shrinking the previous entry.
- The engine's real texture limit is a Stage 2.5 question: the container holds
  1024x512, but the software renderer may downsample.
