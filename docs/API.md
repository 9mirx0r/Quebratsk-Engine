# Quebratsk Engine — GDScript API Contract

**Audience:** whoever builds the editor addon, the docs and the cosmetic assets, in
parallel with work on the C++ parsers.

**Status:** valid as of `v0.6.0-alpha` (commit `29b1369`). Every signature below was read
out of `src/register_types.cpp` and the `_bind_methods()` bodies, not from memory. If
something here disagrees with the code, the code is right and this file is a bug.

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

1. **Do not build on anything marked ❌ below.** Those classes are registered and callable
   — they will not crash — but they return placeholder data. A dock built on
   `VFSFileTree.get_mounted_file_tree()` would display a single hardcoded row forever.
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
| ❌ | **Stub.** Registered and callable, returns placeholder data. Do not build on it |

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
PackedByteArray  read_file(vfs_uri: String)
int              get_file_size(vfs_uri: String)   # -1 when not found
Array            get_mounts_info()                # returns [{prefix, real_path, engine, file_count}]
Dictionary       scan_game_directory(real_dir: String)
```

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

**`unmount()`** invalidates the prefix. Anything already imported stays valid; Godot
resources are independent copies once built.

---

### `UnifiedAssetImporter` ✅ — extends `Node`

Turns a VFS entry into a Godot object. Call `set_vfs()` once before anything else.

```gdscript
void                set_vfs(vfs: VFSManager)
ArrayMesh           load_mesh(vfs_uri: String)
Node3D              load_model(vfs_uri: String, pose_name: String = "")
StandardMaterial3D  load_material(vfs_uri: String)
HeightMapShape3D    load_terrain(vfs_uri: String)
Texture2D           load_texture(texture_ref: String)
PackedStringArray   list_poses(vfs_uri: String)
int                 get_last_error_code()         # 0=OK, 1=VFS Missing, 2=Asset Unreadable, 3=Parse Error
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

Decodes on worker threads, invoking `callback(mesh: ArrayMesh)` or `callback(model_node: Node3D)` on the main thread. Supports non-blocking model and mesh loading.

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

### `ImportPresets` ⚠️ — static

```gdscript
void ImportPresets.apply_preset(preset_index: int)
ImportPresets.MAX_PERFORMANCE   # 0
ImportPresets.RETRO_FIDELITY    # 1
ImportPresets.MAX_QUALITY       # 2
```

Writes three keys under `quebratsk/performance/` in `ProjectSettings`.

**Be careful exposing this.** Those settings are written but **not read by anything in the
engine today** — `vram_eviction_timeout_msec`, `max_background_threads` and
`enable_shader_precaching` currently have no effect. A polished UI for controls that do
nothing is worse than no UI. Leave it out until the engine consumes them.

---

### `NeuralMaterialTranslator` ✅ — static

```gdscript
StandardMaterial3D NeuralMaterialTranslator.translate_material(
    material_name: String, shader_type: String,
    default_roughness: float = 0.5, default_metallic: float = 0.0)
```

Despite the name there is **no machine learning here** — it is a keyword heuristic on the
material name (`metal`/`steel`/`chrome` → metallic 0.85, `water`/`glass` → alpha, and so
on). Accurate naming in user-facing copy, please: "PBR guess from material name".

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

### ❌ Do not build on these

Registered and callable, but they return placeholder data:

| Class | What it actually does |
|---|---|
| `VFSFileTree.get_mounted_file_tree()` | Returns an `Array` with **one hardcoded `{name: "vfs://", type: "directory"}`**. Build the dock tree from `VFSManager.list_files(prefix)` instead |
| `DependencyGraphBuilder.build_dependency_graph()` | Returns the asset name and an **always-empty** `dependencies` array. Parses nothing |
| `BSPMapRenderer.load_map()` | Explicitly not implemented — pushes an error and returns `false`. Use `UnifiedAssetImporter.load_mesh()` for BSP geometry |
| `BSPMapRenderer.perform_pvs_culling()` | No-op. Culls nothing |
| `TextureUpscalerPipeline`, `VulkanRTBuilder`, `P2PVFSStreamer`, `ObsidianDocExporter`, `UAssetMeshExtractor`, `BundleMeshExtractor` | Skeletons, 28–51 lines each including boilerplate. Unreal `.uasset` and Unity `.bundle` import **do not work** |

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
| Real Virtuality | P3D MLOD | ⚠️ MLOD only; ODOL v40/v48 not supported |
| Real Virtuality | PAA textures | ❌ not implemented |
| Unreal | `.uasset` | ❌ stub |
| Unity | `.bundle` | ❌ stub |

Not implemented anywhere yet: sound extraction, LOD generation, collision decomposition.

---

## 6. Known gaps — ask, don't work around

Missing API I already know the addon will want. Request these rather than reimplementing
them in GDScript, because the answers live in the binary formats:

1. **`load_model_async()`** — only `load_mesh` has an async form. A 7 MB `.ani` on the main
   thread is a visible stall.
2. **Pose list without a full import.** Today the only way to see the 341 labels is to
   import the model and read `quebratsk_poses`. A dropdown that populates on selection
   needs a cheap header-only `list_poses(vfs_uri)`.
3. **Structured mount info.** No way to ask "what is mounted, at which prefix, from which
   real file, with how many entries" — needed for the dock's mount list.
4. **A "what can I import here?" scan.** Given a game folder, report which archives exist
   and how many importable assets each holds. This is the wizard's core screen.
5. **Machine-readable failures.** Errors currently go to the Godot console as
   `push_error`. A UI cannot catch those. Distinguishing "file not found" from "missing
   `.vvd` companion" from "unsupported version" needs real return values.
6. **Half-Life 2 and the rest of Source in `SteamLibraryDetector`** (see §4).

---

## 7. Changing this file

Additive API changes bump the minor version and get appended here. Signature changes to
anything marked ✅ get announced before they land — the addon is a real consumer.

If you find that something documented here does not behave as described, that is a bug
report worth filing, not something to route around.
