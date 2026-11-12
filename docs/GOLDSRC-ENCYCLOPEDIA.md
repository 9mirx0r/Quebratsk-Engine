# The GoldSrc Encyclopedia — Implementation Plan

**Status:** proposed, not started
**Owner:** Quebratsk Engine
**Created:** 2026-11-11
**Depends on:** `AUDIT-2026-11-11.md`

---

## 1. What this is

A evidence-first body of knowledge about GoldSrc: its file formats, its engine
semantics, and the content of every GoldSrc game installed on this machine — written so
that each claim can be checked, and so that the engine can consume the result.

It is **not** a wiki of things we remember about Half-Life, and it is **not** a copy of
anyone's source code. Every entry must survive the question *"how do you know?"*.

The encyclopedia has three consumers, in this order:

1. **Quebratsk's parsers and lab** — the data and the constants become code and tests.
2. **The next contributor** — someone who has never opened a `.bsp` can implement a
   reader from the docs alone.
3. **The project's credibility** — a claim in the README backed by a test, not a memory.

---

## 2. Principles

### 2.1 Four evidence classes

Every factual entry carries its sources, and the class matters:

| Class | What it is | Example | Trust |
|---|---|---|---|
| `observed` | Measured from retail assets or the running game on this machine | `arctic.mdl` has 494 sequences | Highest |
| `sdk` | Public, sanctioned source: Valve's HL SDK, the FGDs, Sven Co-op SDK, Steam's Half-Life SDK app | "`pm_shared.c`: `flTimeStepSound = 400` at walk" | High |
| `reference` | A third-party reimplementation, read for facts only | "OGS says hull 1 is 32×32×72" | Hypothesis until verified |
| `decompiled` | A retail binary inspected with REA (Ghidra/IDA/Hopper); facts only | "`hw.dll` computes lightmap extents from face bounds" | Lead, not proof |

A `reference` or `decompiled` claim that has not been promoted to `observed` or `sdk`
is marked `status: hypothesis` and is never used to justify a code change on its own.
The decompiled expression itself — pseudocode, strings, comments — never enters `src/`
or the published docs.

### 2.2 Falsifiability

An entry without a way to be wrong is not an entry. Each one names a `verified_by`:
a script in `tools/verify/` or a scene in `demo/tests/` that fails when the claim is
false. If we cannot write that check, the entry stays `hypothesis`.

### 2.3 Licensing hygiene

Quebratsk is MIT. Four rules, non-negotiable:

- **Facts are not copyrightable; expression is.** We may learn that a footstep falls
  every 400 ms. We may not paste the function that says so.
- **Code under GPL (Xash3D-FWGS) must never be copied into `src/`.** Reading it for
  behaviour is fine and must be recorded in `CREDITS.md`.
- **Decompiled code (reGS, or anything REA/Ghidra produces) never enters `src/` or the
  published docs.** REA is a tool, not a source tree: using it does not automatically
  make the reGS sentence false, but the first REA-derived entry must add a `CREDITS.md`
  paragraph naming the tool, the binaries analysed, and the facts-only policy. If reGS
  itself is read, the existing paragraph changes in the same commit. See §11.2.
- **Every `decompiled` claim is a lead until `observed` or `sdk` confirms it.** A
  decompiler tells you where to look; it does not get to be the last word.

Every entry lists `sources:` so the provenance of a fact is auditable years later.

### 2.4 The corpus is the oracle

When a reference implementation and a retail file disagree, the retail file wins.
When two retail games disagree, both are right and the difference is the finding.

---

## 3. The corpus

### 3.1 Retail games (all installed, `C:\Steam}\steamapps\common\Half-Life`)

| App | Game | Folder | Why it matters |
|---|---|---|---|
| 70 | Half-Life | `valve` | the baseline: 1998 content, 379 maps across the tree |
| 50 | Opposing Force | `gearbox` | Gearbox entities, new models, different WAD set |
| 130 | Blue Shift | `bshift` | HD-pack-first content, different skyboxes |
| 10 | Counter-Strike 1.6 | `cstrike` | the lab's target, 25 maps |
| 80 | Condition Zero | `czero` | 494-sequence player models, newer MDL content |
| 100 | CZ: Deleted Scenes | `czeror` | single-player maps, different entity mix |
| 30 | Day of Defeat | `dod` | outdoor maps, different ambience |
| 40 | Deathmatch Classic | `dmc` | Quake-derived content in a GoldSrc container |
| 20 | Team Fortress Classic | `tfc` | class-based models, `info_tfdetect` |
| 60 | Ricochet | `ricochet` | tiny, weird, good edge cases |

**Also on disk and easy to miss:** `valve_hd`, `cstrike_hd`, `bshift_hd`, `gearbox_hd`,
`valve_spanish`, `cstrike_spanish`, `czero_spanish`, `czeror_spanish`, `dod_spanish`,
`dmc_spanish`. These are **A/B pairs of the same asset** — the single cheapest source of
ground truth about what is content and what is convention. Diffing `models/player/arctic.mdl`
against `cstrike_hd/models/player/arctic.mdl` answers questions no spec can.

### 3.2 SDKs

- **Half-Life SDK** (Steam app 254430, installed) — `pm_shared.c`, `weapons.h`, the FGDs.
- **Sven Co-op** (game 1.3 GB, SDK 10.5 MB, dedicated server 1.8 GB) — a sanctioned,
  open mod SDK on a modified GoldSrc build. Two uses: a third content corpus, and a
  game-side reference for entity behaviour. **Caveat:** Sven Co-op raises engine limits
  and patches behaviour; it is a *variant*, never canonical.

### 3.3 Reference implementations (read-only, facts only)

Candidates, in order of preference:

1. **OGS** (`BlackPhrase/OGS`) — clean reimplementation, no decompiled code. Best fit
   for a MIT project.
2. **Xash3D-FWGS** — complete and well documented, but GPL. Read for behaviour only.
3. **reGS** (`hzqst/GameEngine-reGoldSrc`) — decompiled lineage. See §11.

### 3.4 Binary analysis with REA (tool, not source)

[REA](https://github.com/morluto/rea) — "Reverse Engineer Anything" — is an MIT MCP
server and CLI that lets the agent inspect native binaries through Ghidra, IDA, Hopper
or Kuna. It does not decompile on its own: it drives the backend and gathers the
evidence. The toolchain is installed and verified on this machine (2026-11-11):

- **Node 26.13.0**, npm 11.20.0.
- **`rea-agents` 6.8.0.** 6.0.0 added native x86 PE support on Windows, and every
  GoldSrc engine binary is 32-bit PE.
- **Ghidra 12.1.4** at `C:\tools\ghidra_12.1.4_PUBLIC` (`GHIDRA_INSTALL_DIR`), with
  **Temurin JDK 21.0.13** (`JAVA_HOME`). Ghidra 12.2 requires JDK 25, so the pair is
  not interchangeable.
- `rea doctor --provider ghidra` reports `healthy: true`; a real `tier0.dll` run
  (PE x86, 586 procedures) proved the pipeline end to end.

On Windows the Ghidra path is still experimental. IDA and Kuna are the alternatives if
already licensed. Ghidra is the default free backend.

**Use it on the engine, not on the assets.** The parsers and the corpus are the oracle
for `.bsp`, `.wad`, `.mdl` and `.spr`; there is nothing to gain from decompiling a
texture. Targets, in order:

1. `hw.dll` — hardware renderer: lightmaps, textures, vis, sprites. This is the engine.
2. `sw.dll` — software renderer, the reference the hardware path must match.
3. `tier0.dll`, `vgui2.dll`, `FileSystem_Stdio.dll`, `DemoPlayer.dll` — only when a
   question requires them.
4. `hl.exe` — only 39 KB with `KERNEL32`/`WS2_32` imports: it is a launcher, not the
   engine. There is very little to learn there.
5. Game DLLs (`valve/dlls/hl.dll`, `cstrike/dlls/mp.dll`, …) only where the SDK and
   the corpus disagree.

**Rules.** Every finding is `decompiled` provenance. No pseudocode or decompiled code
enters `src/` or the published docs. A finding is a lead until `observed` or `sdk`
confirms it. Record the binary's SHA-256 and the REA command in the entry's `sources:`.
The analysis stays local; REA reports are not committed.

**What not to do.** Do not try to decompile all ten games end to end. The engine is
shared, the game DLLs repeat what the SDK already provides, and the output is weeks of
pseudocode with low signal. Ask a question, point REA at the function that answers it,
write the fact, confirm it, move on.

---

## 4. Shape of the deliverable

### 4.1 Documents

```
docs/encyclopedia/
  README.md                 how to read an entry, how to add one
  formats/                  bsp30.md, wad3.md, mdl10.md, spr.md, pal.md, lmp.md, dem.md
  engine/                   movement.md, collision.md, lightmaps.md, visibility.md,
                            sound.md, decals.md, sprites.md, animation.md, limits.md
  entities/                 one file per classname, generated index
  games/                    per-game inventory and deltas
  method/                   how to observe, how to measure, how to use REA,
                            how to record evidence
```

### 4.2 Entry format

Markdown with YAML front matter, so the same file is readable by a human and parseable
by a test:

```yaml
---
id: bsp30.lump8.luxel-grid
status: confirmed          # hypothesis | confirmed | refuted
confidence: high
games: [hl, cs16, czero]
sources: [observed, sdk]
verified_by: tools/verify/lightmap_extent.py
see_also: [engine.lightmaps]
# only for decompiled claims:
# binary: hw.dll@sha256:...
# rea: rea inspect hw.dll --provider ghidra
---
```

Body: **Claim**, **Evidence**, **How to check**, **Open questions**. A `decompiled`
entry without the binary hash and the REA command is not reproducible and does not
pass review.

### 4.3 Machine-readable data

Anything the engine can consume is generated, never hand-edited:

```
demo/addons/quebratsk_editor/data/   entities, sounds, weapons (already exists)
data/corpus/<game>.json             file inventory, hashes, versions
data/deltas/<a>-vs-<b>.json         cross-game differences
```

Generated by `tools/`, committed, and checked by a test that regenerates and compares.

### 4.4 Tests

Two kinds, both real:

- `tools/verify/*.py` — fast corpus checks (does every face reference a valid edge?).
- `demo/tests/verify_*.tscn` — in-engine checks, **with `quit(1)` on failure** and a
  watchdog that exits non-zero after a fixed deadline. A harness that hangs is a failure:
  `verify_api` already proves the pattern, because its early `return` on a missing model
  skips `quit()` and leaves Godot running forever.

---

## 5. Stages

Each stage ships something usable on its own. Gates are binary: pass or do not start
the next stage.

### Stage 0 — Corpus inventory and tooling (2–3 sessions)

**Why first:** every later claim is measured against this. Without it we cannot tell a
parser bug from a file we have never seen.

Tasks:
- `tools/corpus_scan.py`: walk every game folder (including `_hd` and `_spanish`),
  record path, size, SHA-256, format, and version field for `.bsp`, `.wad`, `.mdl`,
  `.spr`, `.wav`, `.tga`, `.pak`, `.res`, `.cfg`, `.txt`, `.lst`.
- Duplicate detection by hash across games and variants.
- Per-game summary: counts, bytes, format versions, maps without `worldspawn`, models
  without a sidecar, WADs referenced but missing.
- Resolve the library root through `SteamLibraryDetector` or `QUEBRATSK_GAME_ROOT`;
  never hardcode `C:/Program Files (x86)/Steam/...`. The literal `C:\Steam}` on this
  machine is the regression test for that.
- `data/corpus/<game>.json` + `docs/encyclopedia/games/<game>.md` (generated prose).

**Gate:** one command answers *"which games contain `models/player/arctic.mdl`, and are
they byte-identical?"* and *"which files differ between `cstrike` and `cstrike_hd`?"*

### Stage 1 — Formats, verified by the corpus (6–10 sessions)

**Why:** a format spec that has never been run against 10,000 files is a guess.

Order, by dependency:
1. **WAD3** — everything else needs textures. Directory entries, miptex, mips, palette,
   `{` transparency, font entries.
2. **BSP30** — 15 lumps. For each: struct, invariants, counts across the corpus.
   Lumps 0, 1, 2, 3, 6, 7, 9, 12, 13 and 14 are already read; 4, 5, 8, 10 and 11 are
   the work.
3. **MDL10** — header, bodyparts, skin families, sequences, RLE bone tracks, sidecars,
   attachments, hitboxes, sound table/groups.
4. **SPR** — header, frame types (rp/rle), palette, render modes.
5. **PAL / LMP / DEM / RES / CFG** — small, cheap, closes the "what is this file" gap.

For each format: a `validate` pass over the entire corpus, and a round-trip where the
format allows it. Every struct in `src/parsers/**/structs/` gets a `static_assert` and a
line in the doc.

**Gate:** `tools/verify/validate_corpus.py --all` reports 0 hard errors on every game,
including the `_hd` and `_spanish` variants.

### Stage 2 — Engine semantics from the SDK (3–5 sessions)

Movement, collision hulls, ladders, water, footsteps, doors, buttons, triggers,
damage, and the sound tables — from `pm_shared.c`, `weapons.h`, the FGDs and the Sven
Co-op SDK, with a constant table per system.

**Gate:** every constant the lab currently uses is traceable to a source line and has a
test. Anything that cannot be traced is either removed or marked `hypothesis`.

**Carry-over:** questions the SDK cannot trace go to Stage 2.5, not to a guess.

### Stage 2.5 — Targeted binary analysis with REA (2–4 sessions)

**Why:** the SDK covers the game DLL, not the engine. Some questions — lightmap
allocation, vis leaf rejection, renderer limits, CVar defaults — only exist in
`hl.exe`, `hw.dll` and `sw.dll`. REA gives the agent a way to inspect those binaries
with evidence instead of guessing.

**Method:** one question at a time, from the fifteen in §6 plus whatever Stage 2 could
not trace. Open the binary in REA, find the function, read the constant or the branch,
write the claim as `decompiled`, then confirm it with an asset measurement or a
runtime observation. If it cannot be confirmed, it stays `hypothesis`.

**Tooling:** already installed: `rea-agents` 6.8.0, Ghidra 12.1.4, Temurin JDK 21 in
`C:\tools\ghidra_12.1.4_PUBLIC` and `C:\Program Files\Eclipse Adoptium`, with
`GHIDRA_INSTALL_DIR` and `JAVA_HOME` set. `rea doctor --provider ghidra` passes. If it
ever stops passing, record the failure and skip the stage rather than guess.

**Gate:** at least ten engine questions that the SDK could not answer are resolved or
refuted, each with the binary's SHA-256, the REA command, and a confirmation path.
Zero decompiled code in the tree.

### Stage 3 — Dynamic observation (4–6 sessions)

Static files cannot settle everything: luxel scale, attenuation, animation blending and
render modes are behaviour. This stage builds the protocol to watch the retail engine:

- a fixed cvar set (`developer 1`, `r_speeds`, `cl_showpos`, `sv_gravity`, `fps_max`),
- demo recording (`record`) as the evidence format,
- screenshot comparison for lighting and sprites,
- optionally, a minimal HLSDK mod that logs entity state from inside the game — the
  sanctioned instrumentation path.

**Gate:** at least ten claims that static analysis could not settle are confirmed or
refuted by observation, each with a recorded artifact.

### Stage 4 — Content atlas (6–10 sessions, parallelizable)

Per game and per map: entities, models, sounds, textures, sprites, sequences, skins.
Cross-game deltas. This is where the encyclopedia becomes directly useful to the lab:
"`cs_siege` uses these 17 classes; `de_aztec` uses those 21; here is what only CZ has."

**Gate:** the lab can answer "what does this map use that we have never implemented?"
from data, without reading the map again.

### Stage 5 — Close the engine gaps (10–20 sessions)

Driven by the encyclopedia, in this order:

| # | Feature | Visual impact | Cost | Confidence |
|---|---|---|---|---|
| 1 | Lightmaps (lump 8, styles, UV2, shader) | 5/5 | 3/5 | 4/5 |
| 2 | `.spr` wired to `env_sprite`, `env_glow`, blood, muzzle flash | 4/5 | 2/5 | 5/5 |
| 3 | Animated textures (`+0`…`+9`, `+A`…`+J`, 10 fps) | 3/5 | 1/5 | 5/5 |
| 4 | Skin families and bodygroups | 2/5 | 1/5 | 5/5 |
| 5 | Vis/leaf culling (lumps 4/5/10/11) | 2/5 (perf) | 3/5 | 3/5 |
| 6 | Hitboxes | 3/5 (gameplay) | 2/5 | 4/5 |

Each one: encyclopedia entry first, then the test that fails, then the code.

**Gate:** the per-map visual checklist passes on `cs_siege` and `de_aztec`, and no
earlier test regresses.

### Stage 6 — Publish (3–5 sessions)

Static site or a `docs/` build, with the entry index, the corpus report, and a changelog
of claims that were refuted. The last one matters: a knowledge base that never admits
being wrong is a wiki.

**Gate:** someone who has never seen the repo can write a BSP30 reader from the docs.

---

## 6. First fifteen questions

Concrete, high-value, and answerable with the corpus. These seed the encyclopedia and
double as the first tests.

1. What is the exact lightmap luxel grid per face, including the border texel?
2. How are multiple light styles (`styles[1..3]`) laid out after the base samples?
3. Which retail maps use more than one light style, and which entities create them?
4. What are the exact standing / crouch / large collision hull sizes?
5. What are the real engine limits (`MAX_EDICTS`, models, sounds, lightmaps, planes)?
6. How do `rendermode` / `renderamt` map onto sprite and brush render modes?
7. What are the exact animated-texture chain rules (numbering, fps, alternate frames)?
8. Which miptex flags exist (`TURBULENT`, `WAVY`, …), and which surfaces set them?
9. How does `skin` select a skin family, and how do `body`/`bodygroup` select submodels?
10. What are the 29 `env_sound` room types, and can their parameters be measured rather
    than guessed? (The current table is derived from names — see `CREDITS.md`.)
11. What decides footstep timing and which material a texture maps to?
12. What is the decal table order, and which decal does each material use?
13. How is the PVS encoded and which leaves does the engine actually reject?
14. How does a rotating brush get its pivot (`ORIGIN` brush), and how is it applied?
15. What differs between `cstrike` and `cstrike_hd` beyond textures — geometry,
    sequences, sounds — and what does that say about which layer owns what?

---

## 7. Layout to create

```
tools/
  corpus_scan.py
  verify/
    validate_corpus.py
    validate_bsp30.py
    validate_wad3.py
    validate_mdl10.py
    lightmap_extent.py
data/
  corpus/
  deltas/
docs/encyclopedia/
  README.md
  formats/  engine/  entities/  games/  method/
```

Nothing under `data/` or `docs/encyclopedia/games/` is hand-written; a test regenerates
it and fails on drift.

---

## 8. Definition of done, per entry

An entry is done when all six are true:

1. It has an `id` and lives in the right folder.
2. It states one falsifiable claim.
3. It cites at least one `observed` or `sdk` source, with a path and offset or line.
   A `decompiled` lead may be recorded, but the entry is not done until confirmed.
4. `verified_by` exists and fails when the claim is broken.
5. Any code that depends on it links back to the entry.
6. If it cites `decompiled`, it names the binary's SHA-256, the REA command, and the
   confirmation path.

---

## 9. Risks

| Risk | Mitigation |
|---|---|
| Overfitting to CS 1.6 | Every validator runs on all ten games plus `_hd` and `_spanish` |
| Prose from memory | Front matter requires a source; unsourced text is deleted in review |
| Reference-implementation contamination | Four evidence classes; GPL/decompiled code never enters `src/` |
| REA output mistaken for source | `decompiled` is a lead, never proof; binary hash + confirmation path required |
| Decompiling everything instead of answering questions | Stage 2.5 is targeted and capped; engine binaries first, assets never |
| Scope creep into gameplay | Stage 5 is a fixed, ordered list; new ideas go to a backlog, not the stage |
| Docs drifting from code | Generated data + regeneration test |
| "It compiles" as proof | Every claim has a runtime or corpus check |

---

## 10. What this buys Quebratsk

- Lightmaps stop being a guess and become a spec with a test.
- Sprites, animated textures and skins become a weekend each, not a research project.
- The entity catalogue stops being a vendored FGD dump and becomes measured usage per game.
- The lab can say *why* a map looks wrong, not just that it does.
- The engine's undocumented constants stop being folklore: they have a binary hash, a
  function, and a confirmation test.
- The README can claim what the tests prove.

---

## 11. Open decisions: references and binary analysis

### 11.1 Reference implementations

The plan works with any of them, but the choice changes what `CREDITS.md` is allowed to
say.

- **OGS only (recommended).** Clean-room, compatible with MIT, keeps the current
  statement true. Slower on obscure behaviour, but the corpus is the better oracle.
- **OGS + Xash3D-FWGS (read-only).** Faster on engine internals; requires a note that a
  GPL project was read for behaviour, and absolute discipline about not copying code.
- **reGS.** The fastest route to "what the binary does", and the one the project
  currently promises not to take. Using it means rewriting the `CREDITS.md` paragraph,
  accepting that some knowledge is derived from decompilation, and never copying a line.

### 11.2 Binary analysis with REA

REA is **not** a reference implementation and not a source tree to copy from. It is the
tool that drives Ghidra, IDA, Hopper or Kuna over the retail binaries. Its licence is
MIT; the binaries it reads are Valve's, and Valve's EULA is a separate question. The
project's rule is the same either way: facts yes, expression no, and every `decompiled`
fact is confirmed before it changes code.

**Decided:** [`morluto/rea`](https://github.com/morluto/rea) ("Reverse Engineer
Anything") is the tool, and it is installed: `rea-agents` 6.8.0, Node 26.13.0, Ghidra
12.1.4 at `C:\tools\ghidra_12.1.4_PUBLIC` with Temurin JDK 21.0.13. On Windows the
Ghidra path is experimental, but `rea doctor --provider ghidra` is green and a real
32-bit PE (`tier0.dll`) was analyzed. IDA or Kuna only if already licensed.
`mytechnotalent/rea` is a different project with a similar name and is not part of this
plan.

**Pending:** Stage 0 does not need REA. When Stage 2.5 starts, record the toolchain
versions and the binary SHA-256 in the first entry. If the backend ever fails,
`rea doctor --provider ghidra` is the first diagnostic; if it cannot be fixed, use a
licensed IDA/Kuna or skip Stage 2.5 and record why.
