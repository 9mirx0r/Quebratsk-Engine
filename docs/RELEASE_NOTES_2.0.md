# Quebratsk Engine 2.0

**Import Half-Life, Counter-Strike, Half-Life 2 and Garry's Mod assets into Godot 4
straight from the installed game.** Pick a game, search for a thing, press Add.

This is the first release that is not a pre-release, and the standard changed with it.
An alpha says *look what already works*. A 2.0 says *everything in here works* — so most
of this release is about making that sentence true.

---

## The plugin you can click

Every capability in this project used to be reachable only from GDScript. The editor
addon was a `Label` reading "Quebratsk VFS Explorer Dock — Active".

Now it is the product:

| Step | What happens |
|---|---|
| **Add a game** | Lists the Steam games found on your machine, plus *Choose a game folder* and *Open one archive file*. Nothing is copied — a Half-Life 2 install is ~100,000 files across 70 VPK archives, read where they already are |
| **Browse or search** | Ready-made categories — Characters & people, Weapons, Vehicles, Props & scenery, Maps & terrain, Textures, Sounds — because `.mdl` only tells you something is a model, while `models/weapons/` tells you it is a weapon. Against Garry's Mod plus Half-Life 2 that is 333 characters, 234 weapons, 56 vehicles, 2,130 props, 82 maps |
| **Pick** | Each result names the game it came from: `police.mdl / Garry's Mod`. Both games ship a `police.mdl`, and they are different models. Selecting a character offers **342 standing poses**, the same ones the game itself uses |
| **Add to scene** | Undoable, `owner` set on every descendant so it survives a save, and selected for you |

Failures explain themselves. A Source model that decodes to nothing says its shape lives
in companion files that are not mounted, instead of pushing an error to a console you are
not watching.

Available in **English and Spanish**, following the editor's language.

---

## Everything registered does what its name says

This is the change that earns the version number.

Nine registered classes did not. They were callable, they never crashed, and they returned
plausible data that was fabricated:

| Class | What it actually did |
|---|---|
| `UAssetMeshExtractor`, `BundleMeshExtractor` | Returned a mesh with a name and a material and **zero vertices**. Unreal and Unity "support" was that |
| `VulkanRTBuilder` | Reported `is_built: true` for an acceleration structure Godot 4.3 has no API to build |
| `P2PVFSStreamer` | Contained no networking whatsoever |
| `BSPMapRenderer` | Not implemented |
| `VFSFileTree` | Returned one hardcoded row |
| `DependencyGraphBuilder` | Returned an always-empty dependency list |
| `ImportPresets` | Wrote three Project Settings nothing read |

All removed, along with ten parser stubs the importer never called — 16 to 32 lines each,
existing only to make a format table look longer. **Registered classes: 23 → 15.**

`NeuralMaterialTranslator` became `MaterialHeuristics`. There is no machine learning in it
and never was; it matches keywords in the material name.

### The one that hid the longest

`P3DMLODParser` checked the file magic and returned a **successful** parse of a model
named `"BohemiaModel"` carrying zero surfaces and zero bones. Every caller saw a valid
empty model with no way to tell that nothing had been read. It also accepted ODOL, a
different container entirely.

The documentation listed it as "MLOD only" — written from the parser's shape rather than
from running it. Both are fixed: it reports an error, and Arma and DayZ models are
correctly listed as unsupported.

---

## Supported formats

Nothing is listed until it has been run against a retail install.

| Engine | Archives | Models | Maps | Textures |
|---|---|---|---|---|
| **GoldSrc** <br/><sub>Half-Life, CS 1.6</sub> | `.wad` | `.mdl` v10 — geometry, skinning, `T.mdl` | `.bsp` — geometry, UVs, normals, embedded + WAD textures | WAD3, `.spr` |
| **Source 1** <br/><sub>HL2, Garry's Mod, CS:S, TF2</sub> | `.vpk` v1 & v2 with side archives, `.gma` | `.mdl` v44–49 + `.vvd` + `.vtx` — geometry, skinning, animation sequences, external `.ani`, `includemodel` | `.bsp` | `.vtf` (DXT1/BC1, DXT5/BC3, uncompressed), `.vmt` |
| **Real Virtuality** <br/><sub>Arma, DayZ</sub> | `.pbo` | — | `.wrp` heightmaps | — |

**Not supported:** Real Virtuality models (`.p3d`) and textures (`.paa`), Source 2, Bohemia
Enfusion, Unity, Unreal. Earlier releases listed these as done. See the
[roadmap](../README.md#roadmap) for where they sit.

---

## Fixed

- **`AsyncAssetImporter` spawned an unbounded thread per call.** Importing 200 models in a
  loop created 200 threads, each holding a decoded copy of its asset. It honours
  `max_background_threads` now — which until this release was a setting nothing read.
- **`load_model_async()` always delivered `null`.** The continuation looked the model up by
  its internal header name instead of its VFS URI, and threw away the work the background
  thread had just done.
- **`get_last_error_code()` reported OK after a failed import**, so a UI checking it was
  told a failure had succeeded.
- **`mount_directory()` registered no container**, so loose files were attributed to
  whichever archive occupied slot 0, could not be unmounted, and were silently dropped
  when mounts were saved. For Counter-Strike 1.6, whose maps are loose `.bsp` files, the
  maps disappeared on every editor restart.
- **`scan_game_directory()` could take the editor down** — it iterated with a range-`for`
  whose `operator++` throws, and godot-cpp is built with exceptions disabled.
- **The release DLL shipped as a byte-identical copy of the debug build**, twice. CI now
  fails the build if that recurs.

---

## The repository

`.git` went from **95 MB to 3.3 MB**.

Seven packaged releases and the compiled extension had been committed; between them they
were 186 MB of history, because every rebuild stored a fresh 7 MB DLL. Both are purged and
ignored going forward. The release archives are still downloadable from their tags, which
is where they always belonged.

**A fresh clone contains no binary — build before opening `demo/` in Godot.**

Also new: CI that builds Debug and Release, runs the tests, and checks the two defects that
actually shipped (a release DLL identical to debug, and a `.gdextension` that silently
fails to parse); the five test suites as CMake targets so `ctest` runs them all; community
issue forms; and `CONTRIBUTING.md`.

---

## Install

1. Extract the archive into your Godot 4 project root — you should get `res://bin/` and
   `res://addons/quebratsk_editor/`.
2. Re-open the project.
3. **Project → Project Settings → Plugins → enable "Quebratsk Engine".**
4. Open the **Quebratsk** tab in the left dock and press **Add a game**.

Godot 4.3+, Windows x86_64. Built against godot-cpp `godot-4.3-stable`.

New to Godot? [Beginner's guide](TUTORIAL.md) · Driving it from code? [API reference](API.md)

---

## Breaking changes

Upgrading from 0.x: the nine removed classes are listed above. If you called any of them,
it was returning nothing. `NeuralMaterialTranslator` is now `MaterialHeuristics`.
`load_model()` takes an optional pose name. `ImportPresets` and two Project Settings are
gone.

---

**Full changelog:** [CHANGELOG.md](../CHANGELOG.md)

This plugin ships no game content. It reads files you already own, from where you already
installed them.
