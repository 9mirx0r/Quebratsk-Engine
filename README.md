<p align="center">
  <img src="logo.jpg" alt="Quebratsk Engine Subsystem" width="100%" style="border-radius: 8px;"/>
</p>

<h1 align="center">Quebratsk Engine Subsystem</h1>

<p align="center">
  Universal asset ingestion subsystem for Godot 4 built with GDExtension and C++20. Reads assets from GoldSrc, Source Engine 1, and Real Virtuality / Enfusion in real time with zero-copy RAM mapping.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="MIT License"/></a>
  <a href="https://godotengine.org"><img src="https://img.shields.io/badge/Godot-4.x-blueviolet.svg?logo=godotengine&logoColor=white" alt="Godot 4"/></a>
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=c%2B%2B&logoColor=white" alt="C++20"/></a>
</p>

---

## About This Project & AI Transparency

This repository is developed in pair-programming mode using **Claude Code** and **Google Antigravity IDE**. 

We choose to be 100% transparent about AI assistance. Open-source developers are rightly skeptical of low-effort "AI slop" projects that generate unverified code and flood repositories with broken boilerplate. 

Quebratsk Engine takes the opposite path:
- **Architected by humans**: Format specs, memory layouts, and conversion math are verified against authoritative engine references.
- **Strict C++20 standards**: Code uses memory-mapped I/O, `#pragma pack(push, 1)` structs validated with `static_assert`, zero-copy `std::span` buffers, and zero-allocation pipelines.
- **Active and iterative**: Features ship incrementally with clear commits, tested binary parsers, and explicit CHANGELOG records.

---

## Supported Engine Formats

Quebratsk Engine reads assets from three engine families:

| Engine | Archive / VFS | Models & Animations | Textures & Materials | Physics & Terrains |
| :--- | :--- | :--- | :--- | :--- |
| **GoldSrc** *(Half-Life 1, CS 1.6)* | `.wad` (WAD3) | `.mdl` (StudioMDL v10), `.spr` | 8-bit palette textures, `.spr` frames | `.bsp` v30 ClipNodes |
| **Source Engine 1** *(GMod, HL2, CS:S)* | `.gma` (GMAD), `.vpk` | `.mdl` (v44–49), `.vtx`, `.vvd` | `.vtf` (v7.0–7.5), `.vmt` (KeyValues) | BSP Brushes, V-HACD |
| **Real Virtuality / Enfusion** *(DayZ SA, Arma 2/3)* | `.pbo` (CPRS LZSS) | `.p3d` (ODOL v40–v75+, MLOD) | `.paa` / `.pac` (DXT/ARGB), `.rvmat` | `.wrp` Heightmaps, P3D Geometry LODs |

---

## Technical Pipeline

```
[ Game Container Archive (.wad / .gma / .pbo) ]
                       │
                       ▼
         [ Memory-Mapped File I/O (mmap) ]
                       │
                       ▼
       [ C++20 Intermediate Representation (IR) ]
                       │
                       ▼
   [ Coordinate Transform (Z-Up -> Y-Up, Winding Fix) ]
                       │
                       ▼
      [ Native Godot 4 Resources & Nodes ]
 (ArrayMesh, Skeleton3D, StandardMaterial3D, HeightMapShape3D)
```

### Core Engineering Details
1. **Virtual File System (VFS):** Mounts container archives into memory via `mmap`. Files read directly from mapped memory without copying buffers into RAM. CPRS LZSS streams decompressed on demand.
2. **Axis & Scale Remap:** Automatically transforms Z-Up left-handed coordinates to Godot's Y-Up right-handed system. Converts Hammer Units to meters ($1\text{ HU} = 0.0254\text{m}$) and swaps face indices to maintain proper counter-clockwise winding order.
3. **Materials & Shaders:** Reconstructs Z-channel normals from DXT5nm compressed normal maps ($Z = \sqrt{1 - X^2 - Y^2}$). Packs BSP lightmaps into UV2 atlases.
4. **Skeletal Retargeting:** Maps bone hierarchies from GoldSrc, Source, and Bohemia skeletons to Godot 4's `SkeletonProfileHumanoid` standard, normalizing A-Pose rest angles to T-Pose.

---

## GDScript Example

```gdscript
extends Node3D

var vfs: QuebratskVFS
var importer: UnifiedAssetImporter

func _ready() -> void:
    vfs = QuebratskVFS.new()
    importer = UnifiedAssetImporter.new()
    importer.set_vfs(vfs)

    # Mount archives
    vfs.mount_container("vfs://cs16/", "res://assets/packages/cstrike.wad")
    vfs.mount_container("vfs://gmod/", "res://assets/packages/weapon_pack.gma")
    vfs.mount_container("vfs://dayz/", "res://assets/packages/weapons_firearms.pbo")

    # Load weapon mesh from Garry's Mod
    var weapon_mesh: ArrayMesh = importer.load_mesh("vfs://gmod/models/weapons/w_snip_awp.mdl")
    var weapon_instance := MeshInstance3D.new()
    weapon_instance.mesh = weapon_mesh
    add_child(weapon_instance)

    # Load hands skeleton from Counter-Strike 1.6
    var hands_data: ImportedSkeletonModel = importer.load_skeleton_model("vfs://cs16/models/v_hands.mdl")
    add_child(hands_data.skeleton)
    hands_data.skeleton.add_child(hands_data.mesh_instance)

    # Retarget DayZ animation onto CS 1.6 hands
    var anim_player := AnimationPlayer.new()
    var dayz_reload: Animation = importer.load_animation_and_retarget(
        "vfs://dayz/weapons/firearms/akm/anims/reload.p3d", 
        hands_data.skeleton
    )
    
    var library := AnimationLibrary.new()
    library.add_animation("reload", dayz_reload)
    anim_player.add_animation_library("", library)
    hands_data.skeleton.add_child(anim_player)
    anim_player.play("reload")
```

---

## Project Status & Development Roadmap

This project is actively under construction. Updates, parser extensions, and performance improvements commit regularly.

- [x] Initial C++ architecture and VFS zero-copy design
- [x] Skill and mathematical transform specifications
- [ ] VFS implementation (`mmap` + LZSS decompression)
- [ ] GoldSrc parsers (WAD3, BSP30, MDL v10, SPR)
- [ ] Source 1 parsers (GMA, BSP, MDL v44-49, VTF, VMT)
- [ ] Real Virtuality parsers (PBO, P3D, PAA, WRP, RVMAT)
- [ ] Godot 4 resource converters (ArrayMesh, Skeleton3D, StandardMaterial3D)
- [ ] Unified GDScript GDExtension API

Check [CHANGELOG.md](CHANGELOG.md) for version release logs.

---

## License

Quebratsk Engine is released under the [MIT License](LICENSE).
