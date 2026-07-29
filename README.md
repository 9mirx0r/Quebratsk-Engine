<p align="center">
  <img src="logo.jpg" alt="Quebratsk Engine Subsystem" width="100%" style="border-radius: 8px;"/>
</p>

<h1 align="center">Quebratsk Engine Subsystem</h1>

<p align="center">
  Universal asset ingestion subsystem for Godot 4 built with GDExtension and C++20. Reads assets from GoldSrc, Source Engine 1, Real Virtuality / Enfusion, Source Engine 2, Bohemia Enfusion, and Unity Engine in real time with zero-copy RAM mapping.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="MIT License"/></a>
  <a href="https://godotengine.org"><img src="https://img.shields.io/badge/Godot-4.x-blueviolet.svg?logo=godotengine&logoColor=white" alt="Godot 4"/></a>
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=c%2B%2B&logoColor=white" alt="C++20"/></a>
</p>

---

## 🎯 Why Quebratsk Engine Exists

<p align="center">
  <img src="mission_v2.jpg" alt="Why Quebratsk Engine Exists" width="100%" style="border-radius: 8px;"/>
</p>

### The Problem: Decades of Great Content Trapped in Legacy Formats
Over the past 25 years, game modding communities have built millions of incredible 3D models, maps, weapons, vehicles, and animations across legendary engines like **GoldSrc** (*Half-Life*, *Counter-Strike*), **Source Engine 1** (*Garry's Mod*, *Team Fortress 2*), and **Real Virtuality** (*DayZ*, *Arma*).

However, independent developers moving to modern open-source engines like **Godot 4** face a frustrating barrier:
- **Painful Manual Re-Authoring:** Importing legacy assets requires hundreds of hours of manual work in Blender to fix broken UVs, inverted winding orders, incorrect $Z$-Up axes, and proprietary texture formats (`.paa`, `.vtf`).
- **No Native Modding / UGC Support:** Modern games cannot easily let players load classic Garry's Mod `.gma` packages or Arma `.pbo` mods on the fly without writing custom C++ parsers from scratch.

### The Solution: A Zero-Copy Universal Memory Bridge
**Quebratsk Engine** solves this problem at the C++ subsystem level. Instead of forcing developers to manually convert files on disk, Quebratsk mounts container archives (`.wad`, `.gma`, `.pbo`, `.vpk`, `.pak`, `.bundle`) directly into memory using cross-platform memory mapping (`mmap`).

It translates binary formats into a standardized **Intermediate Representation (IR)** and converts them in real time into native Godot 4 nodes (`ArrayMesh`, `Skeleton3D`, `StandardMaterial3D`, `HeightMapShape3D`).

**Key Goals:**
1. **Accelerate Indie Prototyping:** Build games in Godot 4 using existing asset libraries immediately without initial modeling bottlenecks.
2. **Enable Player Modding (UGC):** Allow your game's players to load custom community mods directly inside your compiled game.
3. **Digital Preservation:** Keep iconic game modding assets usable and alive in modern open-source game development.

---

### ❓ Frequently Asked Questions

#### Q1: Why should I use Quebratsk Engine instead of existing standalone scripts or single-format tools?
**Answer:** Standalone scripts are fragmented, written in high-overhead scripting languages, or abandoned. Quebratsk Engine is a unified, high-performance C++20 GDExtension subsystem. Instead of managing a dozen incompatible plugins for WAD, GMA, and PBO files, Quebratsk provides a single virtual filesystem (`vfs://`), zero-copy memory mapping, automatic $Z$-Up to $Y$-Up coordinate remapping, skeletal retargeting, and real-time Godot 4 node generation under a single unified architecture.

#### Q2: Is Quebratsk Engine a serious, production-ready engineering project?
**Answer:** Absolutely. Quebratsk Engine is engineered with strict C++20 standards. It uses zero-copy memory mapping (`mmap` / `CreateFileMapping`), `#pragma pack(push, 1)` binary layouts validated at compile-time with `static_assert`, zero-allocation streaming pipelines, a decoupled Intermediate Representation (IR), and strict Semantic Versioning. Every feature is audited line-by-line against authoritative engine specifications.

#### Q3: What is the long-term maintenance commitment and expansion vision?
**Answer:** Quebratsk Engine is actively maintained with a clear, long-term technical roadmap. Beyond classic engines, we have expanded into modern Next-Gen engines (Source 2 / CS2, Bohemia Enfusion / Arma Reforger, Unity / Tarkov) and are actively implementing advanced subsystems including V-HACD 4.0 convex physics decomposition, automated lightmap UV2 packing, SIMD vectorization, async multi-threaded loading, audio streaming, and GLTF export capabilities.

---

## About This Project & AI Transparency

This repository is developed in pair-programming mode using **Claude Code** and **Google Antigravity IDE**. 

We choose to be 100% transparent about AI assistance. Open-source developers are rightly skeptical of low-effort "AI slop" projects that generate unverified code and flood repositories with broken boilerplate. 

Quebratsk Engine takes the opposite path:
- **Architected by humans**: Format specs, memory layouts, and conversion math are verified against authoritative engine references.
- **Strict C++20 standards**: Code uses memory-mapped I/O, `#pragma pack(push, 1)` structs validated with `static_assert`, zero-copy `std::span` buffers, and zero-allocation pipelines.
- **Active and iterative**: Features ship incrementally with clear commits, tested binary parsers, and explicit CHANGELOG records.

---

## 🗺️ Project Roadmap & Future Vision

<p align="center">
  <img src="roadmap.jpg" alt="Quebratsk Engine Visual Roadmap" width="100%" style="border-radius: 8px;"/>
</p>

### Phase Breakdown & Feature Roadmap

```
[ Phase 1: VFS Core (Done) ] ──► [ Phase 2: IR & Math (Done) ] ──► [ Phase 3: Classic Engine Parsers (Done) ]
                                                                                   │
                                                                                   ▼
[ Advanced Subsystems ] ◄── [ Modern Next-Gen Engines (Done) ] ◄── [ Phase 4-6: Converters & API (Done) ]
```

#### Core Engine Milestones
- [x] **Phase 1: VFS Zero-Copy Core** — Memory mapping (`mmap`), URI scheme (`vfs://`), container mounting (`.wad`, `.gma`, `.pbo`), LZSS CPRS decompressor.
- [x] **Phase 2: Intermediate Representation & Spatial Math** — `IRMeshData`, `IRSkeletonData`, `IRMaterialData`, $Z$-Up $\to$ $Y$-Up axis remapping, Hammer Unit scaling, CW $\to$ CCW winding inversion.
- [x] **Phase 3: Classic Engine Parsers** — GoldSrc (`.wad`, `.bsp`, `.mdl`, `.spr`), Source 1 (`.gma`, `.bsp`, `.mdl`, `.vtf`, `.vmt`), Real Virtuality (`.pbo`, `.p3d`, `.wrp`, `.paa`, `.rvmat`).
- [x] **Phase 4: Native Godot 4 Converters** — `MeshConverter`, `SkeletonConverter`, `MaterialConverter`, `AnimationConverter`, `CollisionConverter`, `TerrainConverter`.
- [x] **Phase 5: Unified GDScript API** — `UnifiedAssetImporter` class bound to Godot ClassDB (`load_mesh`, `load_material`, `load_terrain`).
- [x] **Phase 6: Skeletal Retargeting Engine** — `SkeletalRetargeter` translating legacy bones to Godot 4 `SkeletonProfileHumanoid` standard.

#### Next-Gen Engine Expansion Tier
- [x] **Source Engine 2 / Counter-Strike 2 (CS2)** — `.vpk` (v2), `.vmdl_c` (KV3 / NTRO).
- [x] **Bohemia Enfusion / Arma Reforger** — `.pak` (EPAK), `.xob` (Multi-LOD).
- [x] **Unity Engine / Escape from Tarkov** — `.bundle` (UnityFS), Serialized 3D `Mesh`.

#### 🚀 5 Advanced Quality & Performance Subsystems (In Progress)
1. **V-HACD 4.0 Physics & Collision Decomposition Subsystem**
   - Automatically decomposes complex 3D meshes into compound `godot::CollisionShape3D` convex bodies.
   - Extracts BSP entity lumps (`LUMP_ENTITIES`) into interactive `godot::Area3D` triggers (`trigger_once`, `func_door`, `water`).
2. **Lightmap UV2 Bin-Packing & Shader Bridge**
   - C++ 2D bin-packing algorithm to generate UV2 lightmap coordinates for baked global illumination (`LightmapGI`).
   - Custom `godot::ShaderMaterial` templates for animated GoldSrc water, `{` blue-key transparency, and Source `$selfillum` emissive maps.
3. **Async Multi-Threaded Ingestion Pipeline & SIMD Vectorization**
   - Background thread loading (`load_mesh_async`) with `std::jthread` thread pool to prevent main thread rendering freezes.
   - AVX2 / ARM Neon SIMD vectorization in `axis_remap.cpp` for 4x–8x throughput speedups on large vertex buffers.
4. **Asset Export & Interoperability Tools**
   - Native GLTF / OBJ exporter (`export_to_gltf`) for saving mounted assets back to disk.
   - Audio VFS decoding for `.wav`, `.ogg`, and `.soundbank` files returning native `godot::AudioStreamWAV`.
5. **Godot Editor Dock Plugin & Texture Cache Manager**
   - GDScript editor plugin (`addons/quebratsk_editor/`) with interactive VFS file browser and 3D preview dock inside Godot Editor.
   - In-memory texture & material cache manager preventing duplicate texture decodes across instances.

---

## Supported Engine Formats (Current Scope)

| Engine | Archive / VFS | Models & Animations | Textures & Materials | Physics & Terrains |
| :--- | :--- | :--- | :--- | :--- |
| **GoldSrc** *(Half-Life 1, CS 1.6)* | `.wad` (WAD3) | `.mdl` (StudioMDL v10), `.spr` | 8-bit palette textures, `.spr` frames | `.bsp` v30 ClipNodes |
| **Source Engine 1** *(GMod, HL2, CS:S)* | `.gma` (GMAD), `.vpk` | `.mdl` (v44–49), `.vtx`, `.vvd` | `.vtf` (v7.0–7.5), `.vmt` (KeyValues) | BSP Brushes, V-HACD |
| **Real Virtuality** *(DayZ SA, Arma 2/3)* | `.pbo` (CPRS LZSS) | `.p3d` (ODOL v40–v75+, MLOD) | `.paa` / `.pac` (DXT/ARGB), `.rvmat` | `.wrp` Heightmaps, P3D Geometry LODs |
| **Source Engine 2** *(Counter-Strike 2)* | `.vpk` (v2) | `.vmdl_c` (KV3 / NTRO) | `.vtex_c` (BC7 / DXT5), `.vmat_c` | Physics KV3 Collision |
| **Bohemia Enfusion** *(Arma Reforger)* | `.pak` (EPAK) | `.xob` (Multi-LOD) | `.edds` (DirectDraw Surface) | Heightmap Terrains |
| **Unity Engine** *(Escape from Tarkov)* | `.bundle` (UnityFS) | Serialized 3D `Mesh` | Serialized `Texture2D` | MeshColliders |

---

## GDScript Example

```gdscript
extends Node3D

var vfs: VFSManager
var importer: UnifiedAssetImporter

func _ready() -> void:
    vfs = VFSManager.new()
    importer = UnifiedAssetImporter.new()
    importer.set_vfs(vfs)

    # Mount archives
    vfs.mount_container("cs16", "res://assets/packages/cstrike.wad")
    vfs.mount_container("gmod", "res://assets/packages/weapon_pack.gma")
    vfs.mount_container("dayz", "res://assets/packages/weapons_firearms.pbo")

    # Load weapon mesh from Garry's Mod
    var weapon_mesh: ArrayMesh = importer.load_mesh("vfs://gmod/models/weapons/w_snip_awp.mdl")
    var weapon_instance := MeshInstance3D.new()
    weapon_instance.mesh = weapon_mesh
    add_child(weapon_instance)
```

---

## License

Quebratsk Engine is released under the [MIT License](LICENSE).
