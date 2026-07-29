# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Intermediate Representation (IR) Layer**: Complete C++20 IR headers (`IRMeshData`, `IRSkeletonData`, `IRMaterialData`, `IRAnimationData`, `IRCollisionData`, `IRTerrainData`) decoupling format parsers from Godot 4 generators.
- **Coordinate Conversion & Axis Remap Module**:
  - `axis_remap.h/.cpp`: Vector, normal, quaternion, and 4x4 matrix transforms for $Z$-Up $\to$ $Y$-Up conversion.
  - `unit_scale.h`: Precise scale factors ($1\text{ HU} = 0.0254\text{m}$).
  - `winding_order.h`: Clockwise to Counter-Clockwise index swapping for proper back-face culling.
  - `tangent_space.h`: 4-component tangent space transformation with bitangent sign flip.
- **Multi-Engine Container Indexing**: Full support for `.wad` (GoldSrc WAD3), `.gma` (Source 1 GMAD), and `.pbo` (Real Virtuality / Enfusion) in `VFSManager::mount_container()`.
- **`GMAHeader` & `PBOEntryFields` Binary Structs**: Memory-packed C++20 structures for GMA and PBO containers.
- **`LZSSDecompressor`**: Bohemia Interactive LZSS CPRS block decompressor implementation in `src/core/vfs/decompressors/lzss_decompressor.h/.cpp`.
- **`VFSManager` Class**: Central Virtual File System manager supporting container mounting, WAD3 texture indexing, case-insensitive VFS URI resolution, and Godot 4 ClassDB bindings in `src/core/vfs/vfs_manager.h/.cpp`.
- **`MemoryMappedFile` Class**: Native cross-platform zero-copy RAM file mapping wrapper (`mmap` on POSIX, `CreateFileMapping`/`MapViewOfFile` on Windows) in `src/core/vfs/memory_mapped_file.h/.cpp`.
- **`VFSUri` Parser**: Custom URI scheme parser (`vfs://prefix/path/to/asset`) with engine namespace auto-detection.

## [0.1.0-alpha] - 2026-07-29

### Added
- **Core Subsystem Architecture**: Technical design for universal asset interoperability in Godot 4 via GDExtension C++20.
- **Virtual File System (VFS) Specification**: Zero-copy RAM layer with on-the-fly LZSS/LZO decompressors.
