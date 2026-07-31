<p align="center">
  <img src="docs/images/logo.jpg" alt="Quebratsk Engine" width="100%" style="border-radius: 8px;"/>
</p>

<h1 align="center">Quebratsk Engine</h1>

<p align="center">
  Import Half-Life, Counter-Strike, Half-Life 2 and Garry's Mod assets into Godot 4
  straight from the installed game. No extraction step, no conversion tools, no Blender
  round-trip.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="MIT License"/></a>
  <a href="https://godotengine.org"><img src="https://img.shields.io/badge/Godot-4.3%2B-blueviolet.svg?logo=godotengine&logoColor=white" alt="Godot 4.3+"/></a>
  <a href="https://en.cppreference.com/w/cpp/23"><img src="https://img.shields.io/badge/C%2B%2B-23-00599C.svg?logo=c%2B%2B&logoColor=white" alt="C++23"/></a>
  <a href="../../actions/workflows/build.yml"><img src="../../actions/workflows/build.yml/badge.svg" alt="Build"/></a>
  <a href="docs/TUTORIAL.md"><img src="https://img.shields.io/badge/Guide-for%20beginners-green.svg" alt="Beginner's guide"/></a>
</p>

---

## What it does

Point it at a game you own. It mounts the game's archives where they are — a Half-Life 2
install is about 100,000 files across 70 VPK archives, and none of them are copied — then
lets you search that content by name and drop a model or a map into your scene.

<p align="center">
  <img src="docs/images/mission.jpg" alt="Why Quebratsk Engine exists" width="100%" style="border-radius: 8px;"/>
</p>

The usual route for legacy game assets is a chain of one-off community tools, then hours in
Blender fixing UVs, winding order, Z-up axes and proprietary textures. Quebratsk reads the
formats directly in C++ and hands Godot a finished `ArrayMesh`, `Skeleton3D` and
`StandardMaterial3D`.

Two details it gets right that most converters do not:

- **Models arrive in a real pose, not a T-pose.** A bind pose is a modelling artefact the
  game never shows. Quebratsk decodes the animation sequences — including the external
  `.ani` blocks a Garry's Mod player model borrows from a shared 7 MB animation model — and
  offers all **341 stances** the game itself uses.
- **Companion files resolve themselves.** A Source `.mdl` contains no vertex data at all;
  it lives in a `.vvd` and a `.dx90.vtx`. You name one file and get a complete model.

## Install

1. Download the latest archive from [Releases](../../releases).
2. Extract it into your Godot 4 project root, so you end up with `res://bin/` and
   `res://addons/quebratsk_editor/`.
3. Re-open the project.
4. **Project → Project Settings → Plugins → enable "Quebratsk Engine".**
5. Open the **Quebratsk** tab in the left dock and press **Add game content**.

Requires Godot 4.3 or newer, Windows x86_64.

New to Godot? Start with the [beginner's guide](docs/TUTORIAL.md).

## From code

The dock is a thin layer over an API you can drive yourself. Full reference in
[docs/API.md](docs/API.md).

```gdscript
var vfs := VFSManager.new()
add_child(vfs)

var importer := UnifiedAssetImporter.new()
add_child(importer)
importer.set_vfs(vfs)

# Mount the _dir.vpk only — it pulls in its own numbered side archives.
vfs.mount_container("hl2", "C:/.../half-life 2/hl2/hl2_misc_dir.vpk")

for path in vfs.list_files("vfs://hl2/models/"):
    print(path)

var npc := importer.load_model("vfs://hl2/models/police.mdl", "idle_smg1")
add_child(npc)
print(npc.get_meta("quebratsk_poses"))   # every stance this model can hold
```

## Supported formats

Everything below is exercised against retail game installs, in Godot 4.7.1, by the
harnesses in `demo/tests/`. Nothing is listed until it works end to end.

| Engine | Archives | Models | Maps | Textures |
|---|---|---|---|---|
| **GoldSrc** <br/><sub>Half-Life, Counter-Strike 1.6</sub> | `.wad` (WAD3) | `.mdl` (StudioMDL v10) — geometry, skinning, `T.mdl` textures | `.bsp` (BSP30) — geometry, UVs, per-face normals, embedded + WAD textures | WAD3 lumps, `.spr` |
| **Source 1** <br/><sub>Half-Life 2, Garry's Mod, CS:S, TF2</sub> | `.vpk` v1 & v2 (with side archives), `.gma` | `.mdl` v44–49 + `.vvd` + `.vtx` — geometry, skinning, animation sequences, external `.ani`, `includemodel` | `.bsp` (BSP30) | `.vtf` (DXT1/BC1, DXT5/BC3, uncompressed), `.vmt` |
| **Real Virtuality** <br/><sub>Arma, DayZ</sub> | `.pbo` | — | `.wrp` heightmaps | — |

**Not supported.** Real Virtuality models (`.p3d`, MLOD and ODOL alike) and textures
(`.paa`); Source 2 (`.vmdl_c`); Bohemia Enfusion (`.pak`, `.xob`); Unity (`.bundle`);
Unreal Engine (`.uasset`, `.pak`). Earlier versions of this README listed these as done and
shipped classes named after them that returned an empty mesh with a material name attached.
They were removed in 2.0 — see the [changelog](CHANGELOG.md).

## Building

Windows, Visual Studio 2022, CMake. godot-cpp is fetched automatically.

```bash
cmake -S . -B build
cmake --build build --config Debug
```

Debug and Release must use **separate build trees**. godot-cpp 4.3 reads
`CMAKE_BUILD_TYPE`, which is empty under a multi-config generator, so it defaults to Debug
and bakes `/MDd` into what you asked to be a release build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release
```

Both write the extension into `demo/bin/`, which is where `quebratsk.gdextension` expects
it.

## Testing

```bash
cd build && ctest -C Debug --output-on-failure
```

Five suites, no framework — each is a plain executable that prints its checks and exits
non-zero on failure. They cover the bounds-checked reader's invariants against adversarial
overflow, DXT block decoding, the quantised animation formats and their run-length tracks,
and the GoldSrc and Source model pipelines.

Unit tests are not the whole story here, because most defects in this project were not the
kind a unit test finds. `demo/tests/` holds harnesses that drive the real API and the real
dock against an actual Steam install and print what each call returns:

```bash
godot --headless --path demo res://tests/verify_api.tscn
godot --headless --path demo res://tests/verify_dock.tscn
```

## Repository layout

```
src/                        C++ engine — parsers, VFS, converters, GDScript API
tests/                      C++ test suites, run by ctest
demo/                       Godot project: loads the extension, hosts the addon
  addons/quebratsk_editor/    the plugin users install
  tests/                      end-to-end harnesses against real game installs
docs/                       API reference and beginner's guide
```

## Roadmap

<p align="center">
  <img src="docs/images/roadmap.jpg" alt="Roadmap" width="100%" style="border-radius: 8px;"/>
</p>

The goal is full coverage of the engines above. The three on the left import today; the
three on the right are where the work is heading. Everything in *Planned* is intended and
actively worked toward — none of it is implemented yet, and this list says so plainly
rather than shipping a class named after it that returns nothing.

**Shipping now**

- GoldSrc — archives, maps, models, textures
- Source 1 — VPK and GMA archives, maps, models with skinning and animation poses, VTF/VMT
- Real Virtuality — PBO archives, WRP terrain
- Editor plugin — browse by category and import without writing code

**Planned, in the order it is likely to land**

| | Why it is next |
|---|---|
| **3D preview before importing** | The one thing still missing from the dock: today you import, look, and undo if it was the wrong model |
| **Real Virtuality models** — `.p3d` MLOD then ODOL | The archive and terrain half already works, so Arma and DayZ are one format away from being complete |
| **Real Virtuality textures** — `.paa` | DXT decoding is already in the engine; this is mostly container work |
| **Sound extraction** | Every mounted game is full of audio that is currently listed but not usable |
| **Source 2** — `.vmdl_c`, `.vtex_c` (CS2, Half-Life: Alyx) | The VPK v2 reader is already shared with Source 1 |
| **Bohemia Enfusion** — `.pak`, `.xob` (Arma Reforger) | |
| **Unity** — `.bundle` (UnityFS) | |
| **Unreal Engine 4/5** — `.pak`, `.uasset` | |
| **Linux and macOS binaries** | The C++ is portable; only the build and CI are Windows-only today |

Progress is tracked in the [changelog](CHANGELOG.md). A format moves from *Planned* to
*Shipping* only once it has been run against a retail game install, not when the parser
compiles.

## License

MIT — see [LICENSE](LICENSE).

This project ships no game content. It reads files you already own, from where you already
have them installed.
