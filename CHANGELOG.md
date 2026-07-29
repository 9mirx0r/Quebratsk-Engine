# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **`LZSSDecompressor`**: Bohemia Interactive LZSS CPRS block decompressor implementation in `src/core/vfs/decompressors/lzss_decompressor.h/.cpp`.
- **`WAD3Header` / `WAD3Lump` Binary Structs**: Packed memory-aligned C++20 structures with `static_assert` validation in `src/parsers/goldsrc/structs/wad3_structs.h`.
- **`VFSManager` Class**: Central Virtual File System manager supporting container mounting, WAD3 texture indexing, case-insensitive VFS URI resolution, and Godot 4 ClassDB bindings in `src/core/vfs/vfs_manager.h/.cpp`.
- **`MemoryMappedFile` Class**: Native cross-platform zero-copy RAM file mapping wrapper (`mmap` on POSIX, `CreateFileMapping`/`MapViewOfFile` on Windows) in `src/core/vfs/memory_mapped_file.h/.cpp`.
- **`VFSUri` Parser**: Custom URI scheme parser (`vfs://prefix/path/to/asset`) with engine namespace auto-detection in `src/core/vfs/vfs_uri.h/.cpp`.
- **GDExtension Entry Point**: Scaffolded `register_types.h/.cpp` and `quebratsk.gdextension` library manifest.
- **CMake Build System**: C++20 CMakeLists.txt configuration.

## [0.1.0-alpha] - 2026-07-29

### Added
- **Core Subsystem Architecture**: Initial technical design for universal asset interoperability in Godot 4 via GDExtension C++20.
- **Virtual File System (VFS) Specification**: Memory-mapped I/O (`mmap`) zero-copy RAM layer with on-the-fly LZSS/LZO decompressors and URI path scheme (`vfs://`).
- **Intermediate Representation (IR)**: Decoupled C++ data layer defining unified schemas for meshes, skeletons, materials, animations, physics hulls, and heightmap terrains.
- **Coordinate & Spatial Normalization Pipeline**: $Z$-Up to $Y$-Up axis remapping, Hammer Unit scale conversions, and CCW face winding order corrections.
- **Format Support Specs**: GoldSrc (`.wad`, `.bsp`, `.mdl`), Source Engine 1 (`.gma`, `.bsp`, `.mdl`, `.vtf`, `.vmt`), and Real Virtuality / Enfusion (`.pbo`, `.p3d`, `.wrp`, `.paa`, `.rvmat`).
