# Quebratsk Engine — GDScript API Contract

**Audience:** whoever builds the editor addon, the docs and the cosmetic assets, in
parallel with work on the C++ parsers.

**Status:** valid as of 2.0. Every signature below was read out of
`src/register_types.cpp` and the `_bind_methods()` bodies, not from memory. If something
here disagrees with the code, the code is right and this file is a bug.

**2.0 removed seven registered classes and ten parser stubs** that did not do what their
names said — see *Removed in 2.0* at the end of §4 before assuming an old call still
works.

---

## 1. Ground rules

This document is the boundary. It exists so UI work and parser work can proceed at the
same time without either blocking or overwriting the other.

**Directory ownership.** Stay inside these, and there are no merge conflicts:

| Owner | Directories |
|---|---|
| Addon / docs / art | `demo/addons/quebratsk_editor/**`, `docs/**`, `art/**` |
| Engine | `src/**`, `tests/**`, `CMakeLists.txt` |

Shared, coordinate before touching: `demo/project.godot`, `README.md`, `CHANGELOG.md`.

**Three rules.**

1. **Everything registered now does what its name says.** That was not true before 2.0:
   nine classes returned placeholder data, and a dock built on
   `VFSFileTree.get_mounted_file_tree()` would have displayed one hardcoded row forever.
   They were deleted rather than documented. Keep it that way — do not register a class
   before it works.
2. **Do not write binary format parsing.** `.mdl`, `.bsp`, `.vpk`, `.vtf` struct layouts
   stay on the engine side. A wrong field offset does not crash, it silently yields empty
   results, and that has already cost this project days.
3. **Claims about formats or third-party libraries get verified before they land.** An
   earlier third-party survey for this project had all five of its highlighted entries
   wrong — wrong licence, wrong language, wrong capability. Cite a file and a line.

**If you need an API that does not exist yet, ask for it — do not work around it in
GDScript.** Section 6 already lists the ones I know are missing.

---

## 2. Status legend

| | Meaning |
|---|---|
| ✅ | Implemented and exercised against retail game assets in Godot 4.7.1 |
| ⚠️ | Works, but with a limitation stated inline that will affect UI design |
| ❌ | Not implemented. Since 2.0 nothing registered is marked this way; the label is kept for the format table |

---

## 3. The shape of a session

Everything the plugin does is three steps: **mount → discover → import.**

```gdscript
var vfs := VFSManager.new()
add_child(vfs)

var importer := UnifiedAssetImporter.new()
add_child(importer)
importer.set_vfs(vfs)

# 1. mount — an archive, or a folder of loose files
vfs.mount_container("hl2", "C:/.../half-life 2/hl2/hl2_misc_dir.vpk")
vfs.mount_directory("maps", "C:/.../cstrike/maps")

# 2. discover
for path in vfs.list_files("vfs://hl2/models/"):
    print(path)

# 3. import
var soldier := importer.load_model("vfs://hl2/models/police.mdl", "idle_smg1")
add_child(soldier)
```

**URI form:** `vfs://<prefix>/<path/inside/the/mount>`. The prefix is whatever string was
passed to `mount_container()` / `mount_directory()`. Paths are indexed lowercase with
forward slashes; lookups are case-insensitive.

`VFSManager` and `UnifiedAssetImporter` both extend `Node`. Add them to the tree, or free
them yourself.

---

## 4. Reference

### `VFSManager` ✅ — extends `Node`

The mount table and file index. One instance can hold many mounts.

```gdscript
bool             mount_container(vfs_prefix: String, real_path: String)
int              mount_directory(vfs_prefix: String, real_dir: String)
void             unmount(vfs_prefix: String)
bool             file_exists(vfs_uri: String)
PackedStringArray list_files(prefix: String = "")
Dictionary       find_files(needle: String, extensions := PackedStringArray(),
                            exclude := PackedStringArray(), limit := 0)
PackedByteArray  read_file(vfs_uri: String)
int              get_file_size(vfs_uri: String)   # -1 when not found
Array            get_mounts_info()
Dictionary       scan_game_directory(real_dir: String)
```

**`get_mounts_info()`** returns one Dictionary **per prefix you mounted**, not per real
file on disk:

```gdscript
{ "prefix": "hl2", "real_path": ".../hl2_misc_dir.vpk",
  "engine": "Source1",       # GoldSrc | Source1 | RealVirtuality | BSP | Custom
  "file_count": 18796,       # entries reachable under this prefix
  "archive_count": 5,        # real files backing it (a _dir.vpk plus its side archives)
  "is_directory": false }    # true when mounted with mount_directory()
```

Mounting one `_dir.vpk` places several containers internally — the directory plus each
numbered side archive — all under the same prefix. They are grouped here, because the
prefix is the unit a user mounted and can `unmount()`. Summing `file_count` over the array
equals `list_files().size()`.

`mount_directory()` mounts appear here too, with `is_directory` set. Persist that flag if
you save mounts across sessions: a directory must be restored with `mount_directory()` and
an archive with `mount_container()`, and once the game is uninstalled the path alone can
no longer tell you which it was.

**`scan_game_directory(real_dir)`** answers "what is in this folder?" for a setup wizard:

```gdscript
{ "total_archives": 6, "archives": [ ...absolute paths... ],
  "loose_models": 1, "loose_maps": 2, "loose_textures": 237 }
# or { "error": "..." } if the directory is missing or unreadable
```

Two things to design around:

- The `loose_*` counts are **files sitting on disk, not archive contents**. A modern Source
  game keeps everything inside VPKs, so scanning `GarrysMod/` honestly reports 1 loose
  model. The answer to "what can I import here" is the `archives` list — mount those to
  see inside. Do not present `loose_models` as "models available".
- Only `_dir.vpk` files are listed, since side archives are not separately mountable.
- It is **synchronous and recursive**. 44 ms over `garrysmod/`, but a whole Steam library
  is far larger. Run it off the main thread or show a spinner.

**`mount_container()`** — recognised by content, not by extension:

| Format | Extension | Notes |
|---|---|---|
| GoldSrc WAD3 | `.wad` | Half-Life / Counter-Strike 1.6 texture archives |
| Source VPK v1 & v2 | `.vpk` | **Mount the `_dir.vpk` only.** It pulls in its own numbered side archives (`hl2_misc_000.vpk`, `_001`, …) automatically. Mounting a side archive directly does nothing useful |
| Garry's Mod addon | `.gma` | Workshop downloads |
| Real Virtuality PBO | `.pbo` | Arma / DayZ |

Returns `false` if the file is missing, unreadable, or not one of the above.

**`mount_directory()`** returns the **number of files indexed**, not a bool — `0` means the
directory was missing or empty. Use it for extracted asset folders, which is the common
case for modders.

**`list_files(prefix)`** returns full `vfs://` URIs. With no argument it returns
*everything* — a Half-Life 2 install is ~100,000 entries, so always pass a prefix in UI
code or you will freeze the editor building a `Tree`.

**`find_files()`** is what to use when the prefix is not the filter. It searches the index
in place and returns only what you are going to draw:

```gdscript
var hit := vfs.find_files("police", PackedStringArray(["mdl"]),
                          PackedStringArray(["vvd", "vtx", "ani", "phy"]), 400)
hit["files"]   # PackedStringArray, at most `limit` URIs (0 means no limit)
hit["total"]   # how many matched in full, so you can say "showing 400 of 4,264"
```

Results come back **sorted by filename**, with the full path as the tiebreak, and `total`
counts every match rather than the page. Sorting on the filename rather than the whole URI
is what puts Garry's Mod's `police.mdl` next to Half-Life 2's: ordering by URI groups
by mount prefix instead, so a 400-row page would come entirely from whichever game sorts
first and the other one's matches would never be seen.

The order matters more than it looks. The index is a hash map, so before this the page was
whichever entries the bucket layout happened to yield, it changed whenever the map was
resized, and "showing the first 400 of 41,969" named a first that did not exist.

`needle` matches anywhere in the URI, case-insensitively; an empty one matches everything.
`extensions` are lowercase and without the dot. `exclude` is subtracted first, which is the
point of having it: filtering the returned page yourself would both shrink the page and
leave `total` counting rows you removed. The companion files of a Source model
(`.vvd`, `.vtx`, `.ani`, `.phy`) are the usual thing to exclude, since the importer pulls
them in by itself and a user picking one gets nothing.

Measured on a 60,584-entry index (Half-Life 2 plus Garry's Mod), producing the same 102
results both ways: **8.8 ms** here against **55.5 ms** for `list_files()` followed by the
equivalent GDScript loop. Those are release-build figures, the ones a user gets. A debug
build narrows it to 33.8 against 64.3, since MSVC's checked iterators tax the C++ side and
leave the GDScript side alone — worth knowing if you profile your own work against a debug
extension and wonder where the difference went.

**`unmount()`** invalidates the prefix. Anything already imported stays valid; Godot
resources are independent copies once built.

---

### `UnifiedAssetImporter` ✅ — extends `Node`

Turns a VFS entry into a Godot object. Call `set_vfs()` once before anything else.

```gdscript
void                set_vfs(vfs: VFSManager)
ArrayMesh           load_mesh(vfs_uri: String)
Node3D              load_model(vfs_uri: String, pose_name := "",
                               animations := PackedStringArray())
StandardMaterial3D  load_material(vfs_uri: String)
HeightMapShape3D    load_terrain(vfs_uri: String)
Texture2D           load_texture(texture_ref: String)
PackedStringArray   list_poses(vfs_uri: String)
int                 get_last_error_code()
```

**`load_model(uri, pose, animations)`** — `animations` names sequences to bring in as
playable animation rather than a single frozen frame:

```gdscript
var poses := importer.list_poses(uri)          # 426 on a Garry's Mod player model
var npc := importer.load_model(uri, "idle_all_01",
                               PackedStringArray(["walk_all", "idle_all_01"]))
var player := npc.get_node("AnimationPlayer")
player.play("walk_all")
```

The returned `Skeleton3D` gains an `AnimationPlayer` child holding one `Animation` per name
that decoded, under the label the game uses. Its `root_node` is the skeleton, so the node
can be reparented anywhere without the tracks going stale.

Nothing is decoded unless it is named. A sequence is one keyframe per bone per frame — a
nine-second idle is 280 of them — and a model carries hundreds of sequences, so importing
them all would cost far more than any caller wants. `list_poses()` is how you find out what
there is to ask for.

Two things worth knowing:

- **A track whose value never changes is stored as one key.** Most bones hold still through
  any one sequence. A "ragdoll" or "reference" sequence is a held stance and comes back with
  a single key on every track, which is correct and not a truncated decode.
- **The animation's length is what was decoded, not what the header claims.** If a sequence's
  data cannot be reached part way through, the animation is shorter and the parser says so on
  stderr, rather than reporting a full duration the model would be frozen through.

**`get_last_error_code()`** — why the last `load_*` call ended as it did. Set on **both**
the success and the failure paths of every entry point, so it always describes the most
recent call rather than a stale one:

| Constant | Value | Meaning |
|---|---|---|
| `UnifiedAssetImporter.ERR_OK` | 0 | succeeded |
| `UnifiedAssetImporter.ERR_VFS_NOT_SET` | 1 | `set_vfs()` was never called |
| `UnifiedAssetImporter.ERR_ASSET_UNREADABLE` | 2 | URI not in the VFS, or the read failed |
| `UnifiedAssetImporter.ERR_PARSE_FAILED` | 3 | decoded to nothing usable |

```gdscript
var node := importer.load_model(uri)
if node == null:
    match importer.get_last_error_code():
        UnifiedAssetImporter.ERR_ASSET_UNREADABLE: show("Not found — is the archive mounted?")
        UnifiedAssetImporter.ERR_PARSE_FAILED:     show("Recognised, but nothing could be decoded.")
        UnifiedAssetImporter.ERR_VFS_NOT_SET:      show("Internal: VFS not attached.")
```

**`load_mesh()`** — geometry only, materials attached per surface. Accepts `.bsp` (GoldSrc
BSP30), `.mdl` (GoldSrc v10 and Source v44–49), `.p3d` (Real Virtuality MLOD). Returns
`null` on failure.

**`load_model()`** — the one a UI should prefer for characters and props. Returns a
`Skeleton3D` with a skinned `MeshInstance3D` child when the asset has bones, otherwise a
bare `MeshInstance3D`. **The returned node is unparented and owned by the caller** — add it
to the tree or `queue_free()` it.

Companion files are resolved automatically through the VFS, so the caller names one file
and gets a complete model:

| Asset | Pulled in silently |
|---|---|
| Source `.mdl` | `.vvd` (all vertex data), `.dx90.vtx` (indices), `.ani` (animation blocks) |
| Source `.mdl` | every model named in its `includemodel` table, plus *their* `.ani` |
| GoldSrc `.mdl` | `<name>T.mdl` (textures) |

A Source `.mdl` contains **no vertex data at all** — if the `.vvd` is not mounted you get a
skeleton and nothing else. That is a mounting problem, not a parse failure, and it is worth
saying so in the UI.

**`pose_name`** selects one of the model's animation sequences to stand in. Matched by
**exact label first**, then as a substring. Left empty, an idle-like sequence is chosen.

The full catalogue is published on the returned node:

```gdscript
var node := importer.load_model(uri)                  # auto-pick
var labels: PackedStringArray = node.get_meta("quebratsk_poses", PackedStringArray())
# a Garry's Mod player model yields 341 labels: idle_smg1, idle_ar2, crouch_walk_pistol, …
```

`quebratsk_poses` is **absent** when the model has no sequences — always pass a default to
`get_meta()`. This is the natural backing for a pose dropdown in the inspector.

Exact-match-first matters: `"idle_smg1"` must not resolve to `cidle_smg1`, its crouched
namesake. Do not reimplement the matching in GDScript.

**`load_texture(texture_ref)`** takes a full `vfs://` URI *or* a bare legacy reference
(`"metal/metalwall001a"`, a WAD3 lump name), resolved by suffix search across every mount.

---

### `AsyncAssetImporter` ✅ — extends `Node`

```gdscript
void load_mesh_async(importer: UnifiedAssetImporter, vfs_uri: String, callback: Callable)
void load_model_async(importer: UnifiedAssetImporter, vfs_uri: String, pose_name: String, callback: Callable)
```

Decodes on a worker thread, then invokes `callback(mesh: ArrayMesh)` or
`callback(model_node: Node3D)` on the main thread. The node arrives unparented and owned
by you, exactly as with `load_model()`.

```gdscript
async_importer.load_model_async(importer, uri, "idle_smg1", _on_ready)

func _on_ready(node: Node3D) -> void:
    if node == null:
        push_error("import failed: %d" % importer.get_last_error_code())
        return
    add_child(node)
```

**What actually runs where.** Reading the archive stays on the main thread — the VFS is
main-thread-owned — so the *I/O* is not moved off it; what the worker takes is the decode,
which is the expensive half (~100 ms of the ~170 ms for a Garry's Mod player model).
Godot Object allocation happens in the main-thread continuation. Do not try to build nodes
from a thread yourself.

`callback` may receive `null`; check it, and read `get_last_error_code()` on the importer
you passed in.

---

### `SteamLibraryDetector` ✅ — static

```gdscript
Dictionary SteamLibraryDetector.detect_installed_games()
# { "Half-Life 2": "C:/.../steamapps/common/Half-Life 2", ... }
```

Reads `SteamPath` from the registry (`HKCU\Software\Valve\Steam`) and walks `steamapps/libraryfolders.vdf`. Target titles include Half-Life, CS 1.6, **Half-Life 2**, **HL2 Episode 1 & 2**, **Portal**, **Portal 2**, **Team Fortress 2**, **Left 4 Dead 2**, Garry's Mod, CS2, Arma 3, and DayZ.

---

### `VFSDropHandler` ✅ — static

```gdscript
bool VFSDropHandler.handle_dropped_files(files: PackedStringArray, vfs: VFSManager)
```

Mounts every recognised archive in `files`, using each file's basename as the prefix.
Returns `true` if at least one mounted.

Accepts `.wad`, `.gma`, `.pbo`, `.vpk`, `.pak`, `.bundle`. **Silently ignores everything
else**, including directories and individual assets like a dropped `.bsp` — so a user
dragging a map file gets no feedback. Check the return value and say something.

---

### `TaskProgressTracker` ✅ — extends `Node`

```gdscript
void   set_progress(task_name: String, progress: float)   # progress clamped to 0..1
float  get_progress()
String get_status_text()
void   reset()
```

Thread-safe: name and value are published together under one lock, so a reader never sees
a new percentage paired with the previous task's label. Safe to poll from `_process` while
a worker writes.

Nothing in the engine writes to it yet — today it is a container your own UI drives.

---

### `QuebratskSettings` ✅ — project settings

One setting, and the engine reads it:

| Key | Default | Effect |
|---|---|---|
| `quebratsk/performance/max_background_threads` | cores − 1, clamped to 1–8 | Ceiling on concurrent `load_*_async` workers |

`AsyncAssetImporter` consults it before spawning, so importing a folder in a loop no
longer creates one thread per asset, each holding a decoded copy. Read at the point of
use, so changing it in Project Settings takes effect without a restart.

An `ImportPresets` class used to write three keys here —
`vram_eviction_timeout_msec`, `max_background_threads` and `enable_shader_precaching`.
Nothing read any of them: `TextureCache` has no eviction and there is no shader
precaching. They appeared in the Project Settings UI, could be changed and saved, and did
nothing. The two unimplementable ones were removed and the third was made real. A setting
with no effect is worse than a missing one, because the user believes they configured
something.

---

### `MaterialHeuristics` ✅ — static

```gdscript
StandardMaterial3D MaterialHeuristics.translate_material(
    material_name: String, shader_type: String,
    default_roughness: float = 0.5, default_metallic: float = 0.0)
```

A keyword heuristic on the material name: `metal`/`steel`/`chrome` → metallic 0.85,
`water`/`glass` → alpha, `concrete`/`stone` → rough, and so on. Describe it as "PBR guess
from the material name".

Called `NeuralMaterialTranslator` until 2.0. There is no machine learning in it and never
was.

---

### `MapPreviewViewport` ✅ — extends `SubViewport`

```gdscript
void set_camera_pose(position: Vector3, rotation_deg: Vector3)
```

Creates a `PreviewCamera` child on `_ready()` unless the scene already contains a node by
that name, in which case yours is adopted. Suitable for a thumbnail or preview pane.

---

### `WindingVisualizer` ✅ · `FuzzyMaterialFixer` ✅ · `BatchingManager` ✅

```gdscript
void   WindingVisualizer.apply_debug_winding_material(mesh_instance: MeshInstance3D)
void   WindingVisualizer.flip_normals_and_winding(mesh_instance: MeshInstance3D)
String FuzzyMaterialFixer.find_best_matching_texture(missing_texture_name: String, vfs: VFSManager)
void   BatchingManager.register_instance(vfs_path: String, transform: Transform3D, mesh_ref: ArrayMesh)
void   BatchingManager.flush(parent_node: Node)
void   BatchingManager.clear()
```

Diagnostic and utility helpers. `FuzzyMaterialFixer` is the useful one for UX: given a
texture name that failed to resolve, it suggests the closest thing actually mounted —
exactly what a "missing textures" panel should offer.

---

### Removed in 2.0

These were registered and callable, and none of them did what its name said. A registered
class is a promise, so they were deleted rather than documented:

| Class | What it actually did |
|---|---|
| `UAssetMeshExtractor`, `BundleMeshExtractor` | Returned a mesh with a name and a material and **zero vertices**. Unreal and Unity "support" was that |
| `VulkanRTBuilder` | Reported `is_built: true` for an acceleration structure Godot 4.3 has no API to build |
| `P2PVFSStreamer` | Contained no networking at all |
| `BSPMapRenderer` | Not implemented. Use `UnifiedAssetImporter.load_mesh()` for BSP geometry |
| `VFSFileTree` | Returned one hardcoded row. Build a tree from `VFSManager.list_files(prefix)` instead |
| `DependencyGraphBuilder` | Returned an always-empty dependency list |
| `ImportPresets` | Wrote three settings nothing read (see `QuebratskSettings`) |

Ten unreachable parser stubs went with them — `uasset_parser`, `bundle_parser`,
`vmdl_parser`, `xob_parser`, `paa_decoder`, `audio_decoder` and others, 16 to 32 lines
each. The importer never called any of them; they existed to make a format table look
longer.

**If you had code calling these, it was returning nothing.** The replacement in every case
is either `UnifiedAssetImporter` or "that format is not supported yet".

---

## 5. Format support, honestly

For the docs and the support matrix. Claims below are backed by in-engine runs against
retail installs.

| Engine | Format | State |
|---|---|---|
| GoldSrc | WAD3 textures | ✅ |
| GoldSrc | BSP30 maps | ✅ geometry, UVs, per-face normals, embedded + WAD textures (`cs_assault` 149/149 surfaces textured) |
| GoldSrc | StudioMDL v10 models | ✅ geometry, textures, skinning, `<name>T.mdl` companion |
| Source 1 | VPK v1 / v2 | ✅ including side archives and inline entries |
| Source 1 | MDL v44–49 + VVD + VTX | ✅ geometry, skinning, materials |
| Source 1 | Animation sequences + `.ani` blocks | ✅ 341 poses recovered per Garry's Mod player model |
| Source 1 | GMA addons | ✅ |
| Source 1 | VTF / VMT | ✅ DXT1/BC1, DXT5/BC3, uncompressed |
| Real Virtuality | PBO archives | ✅ (121 entries indexed from a DayZ PBO) |
| Real Virtuality | WRP terrain | ✅ heightmap, with overflow-safe grid bounds |
| Real Virtuality | P3D models | ❌ **not implemented, MLOD or ODOL.** This file previously said "⚠️ MLOD only", which was wrong: the parser validated the magic and returned a *successful* empty model named "BohemiaModel". It reports an error now |
| Real Virtuality | PAA textures | ❌ not implemented |
| Source 2 · Enfusion · Unity · Unreal | — | ❌ not implemented. Their stub parsers were deleted in 2.0 |

Not implemented anywhere yet: sound extraction, LOD generation, collision decomposition.

Nothing reaches this table until it has been run against a retail install. `P3D` is the
cautionary tale — it was documented from the parser's shape rather than from its
behaviour.

---

## 6. Known gaps

### Closed — all six original gaps, verified in-engine

`load_model_async()`, `list_poses()`, `get_mounts_info()`, `scan_game_directory()`,
`get_last_error_code()` and the Source titles in `SteamLibraryDetector` all exist and are
exercised by `demo/tests/verify_api.gd`. Run that scene against a real Steam install before
trusting any change to them; it prints a pass/fail line per API.

Two carry caveats worth designing around, both stated in §4: `scan_game_directory()`
counts loose files only and blocks the main thread, and `get_mounts_info()` groups by
prefix rather than by file on disk.

### Still open

1. **`list_poses()` is cheaper, not free** — ~7 ms against ~10 ms for a full import on a
   Garry's Mod player model, release build. It skips the `.vvd` and `.vtx` entirely, and
   since it only needs the labels it no longer decodes every bone of all 359 sequences,
   which is what used to make it cost half a full import.

   What is left is almost all one read: the 7.1 MB `m_anm.ani` the model borrows its
   sequences from. 4.4 ms warm, 15 ms the first time. It genuinely has to be read — a
   sequence whose data cannot be reached is not a pose the model can stand in, and that is
   only knowable from the file. Picking a model in the dock reads it twice, once here and
   once for the import, and again on each pose change. Caching the borrowed animation
   model is the obvious next move; ask if a dropdown needs to be instant.
2. **No async form of `list_poses()` or `load_mesh()` for BSP** — a large map still
   stalls; only `load_mesh_async` / `load_model_async` exist.
3. **Failure reasons are still coarse.** Three codes cannot distinguish "the `.vvd` is
   missing" from "this MDL version is unsupported" — both surface as `ERR_PARSE_FAILED`.
   The parsers know the difference internally (`SourceMDLParseError` has
   `MissingCompanionFile`, `VersionMismatch`, `ChecksumMismatch`); it is not yet plumbed
   out. Ask if the UI needs to tell a user *why*.
4. **Steam paths come back malformed but usable** — `C://Program Files (x86)//Steam/...`
   with doubled separators and a mixed backslash, from the `libraryfolders.vdf` parse.
   Windows accepts them, but never string-compare two of these; normalise first.
5. **The editor dock is not covered by the C++ tests.** `demo/tests/verify_dock.gd` drives
   it headlessly against a real Steam install, but it needs games installed, so CI cannot
   run it. Run it before changing the dock.

---

## 7. Changing this file

Additive API changes bump the minor version and get appended here. Signature changes to
anything marked ✅ get announced before they land — the addon is a real consumer.

If you find that something documented here does not behave as described, that is a bug
report worth filing, not something to route around.
